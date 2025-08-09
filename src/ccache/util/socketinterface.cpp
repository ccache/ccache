#include "socketinterface.hpp"

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <iostream>

Stream::Stream(socket_t sock)
  : m_sock(sock)
{
}

int
Stream::select_read(time_t sec,
                    time_t usec,
                    bool read_possible,
                    bool write_possible) const
{
  fd_set read_fds;
  fd_set write_fds;
  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);

  if (read_possible) {
    FD_SET(m_sock, &read_fds);
  }
  if (write_possible) {
    FD_SET(m_sock, &write_fds);
  }

  timeval tv;
  tv.tv_sec = static_cast<long>(sec);
  tv.tv_usec = static_cast<long>(usec);

  return ::select(static_cast<int>(m_sock + 1),
                  read_possible ? &read_fds : nullptr,
                  write_possible ? &write_fds : nullptr,
                  nullptr, // Exceptfds
                  &tv);
}

size_t
Stream::read(const nonstd::span<uint8_t> writable_span)
{
  try {
    // Check if the buffer is actually ready to write into.
    // If prepare_write_span returned an empty span or threw, this might fail.
    if (writable_span.empty()) {
      // This case implies writable span is invalid. Either n=0 or too large!
      // CHECK StreamBuffer::prepare
      return 0;
    }

    // Wait for the socket to be ready for reading.
    // We pass the size of the span we prepared as the amount we *want* to read.
    // The select timeout is handled by constants.
    int event_result = select_read(
      std::chrono::duration_cast<std::chrono::seconds>(READ_TIMEOUT_SECOND)
        .count(),
      std::chrono::duration_cast<std::chrono::microseconds>(
        READ_TIMEOUT_USECOND)
          .count()
        % 1000000,
      true,
      false);

    if (event_result < 0) {
      // Select error
      return -1;
    } else if (event_result == 0) {
      // Timeout
      return 0; // Indicate no data received due to timeout
    }
    // If event_result > 0, the socket is ready for reading.

    // Now, read directly into the span provided by the buffer.
    // The actual number of bytes to attempt to read is the size of the span.
    ssize_t bytes_read = ::recv(m_sock,
                                writable_span.data(),
                                static_cast<int>(writable_span.size()),
                                0); // MSG_NOSIGNAL on send usually, not recv

    if (bytes_read > 0) {
      return static_cast<int>(bytes_read);
    } else if (bytes_read == 0) {
      // Connection closed by peer.
      return 0;
    } else { // bytes_read < 0
      // Handle errors. EAGAIN/EWOULDBLOCK mean no data *yet*.
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // No data available right now, but socket is still usable.
        // This might happen if non-blocking was used, or if select timed out.
        // If we just timed out on select, this is expected.
        return 0;
      } else {
        // Real error.
        return -1;
      }
    }
  } catch (const std::out_of_range& e) {
    // Buffer preparation failed (e.g., exceeded MAX_MSG_SIZE)
    std::cerr << "Stream read error: " << e.what() << "\n";
    return -1; // Indicate an error
  } catch (const std::logic_error& e) {
    std::cerr << "Stream read buffer error: " << e.what() << "\n";
    return -1;
  }
}

size_t
Stream::write(nonstd::span<const uint8_t> ptr) const
{
  return ::send(m_sock, ptr.data(), static_cast<int>(ptr.size()), MSG_NOSIGNAL);
}

StreamReader::StreamReader(Stream& strm, StreamBuffer& buffer)
  : m_stream(strm),
    m_buffer(buffer)
{
  // Buffer's state (clearing, etc.) should be managed externally
}

void
StreamReader::reset_read_state()
{
  m_bytes_consumed = 0;
}

std::optional<size_t>
StreamReader::read_all(const std::atomic<bool>& shouldStop)
{
  // The chunk size determines how much data we try to read at once.
  // This should be efficient for the underlying socket and buffer.
  constexpr size_t CHUNK_SIZE = 1024; // read in 1KB chunks.

  while (!shouldStop) {
    // Get a writable span in the buffer. The buffer handles resizing.
    // The span's size indicates how much space is available to fill.
    nonstd::span<uint8_t> writable_span;
    try {
      writable_span = m_buffer.get().prepare(CHUNK_SIZE);
    } catch (const std::out_of_range& e) {
      std::cerr << "StreamReader error: " << e.what() << "\n";
      return std::nullopt;
    } catch (const std::logic_error& e) {
      std::cerr << "StreamReader buffer error: " << e.what() << "\n";
      return std::nullopt;
    }

    // If prepare_write_span returned an empty span, it means we couldn't get
    // space. This might happen if CHUNK_SIZE was 0 or MAX_MSG_SIZE was hit and
    // we couldn't grow.
    if (writable_span.empty()) {
      // If there's already data read but we can't get more space, it implies a
      // limit hit. If m_bytes_consumed is 0 and we can't even get a chunk, it's
      // an error/stuck state.
      if (m_bytes_consumed == 0) {
        return std::nullopt; // Cannot proceed
      }
      break; // Exit loop to return current progress.
    }

    // Read data from the stream into the prepared span.
    int bytes_read = m_stream.get().read(writable_span);

    if (bytes_read > 0) {
      m_buffer.get().commit(bytes_read);
      m_bytes_consumed += bytes_read;
    } else if (bytes_read == 0) {
      if (m_bytes_consumed == 0) {
        return std::nullopt;
      }
      // We have some data, but no more will come -> break and return
      break;
    } else { // bytes_read < 0
      // An error occurred during read but it should be handled already.
      return std::nullopt;
    }
  }

  // Return the total number of bytes read into the buffer by this reader.
  // The caller can then parse from the referenced read buffer.
  if (m_bytes_consumed > 0) {
    return m_bytes_consumed;
  } else {
    // If loop exited due to shouldStop or timeout with no data read.
    return std::nullopt;
  }
}

UnixSocket::UnixSocket(const std::string& host)
  : m_path(host),
    m_read_buffer(tlv::g_read_buffer),
    m_write_buffer(tlv::g_write_buffer)
{
}

UnixSocket::~UnixSocket()
{
#ifdef _WIN32
  if (WSACleanup() != 0) {
    std::cerr << "WSACleanup() failed";
  }
#endif
  end();
}

void
UnixSocket::set_nonblocking(bool nonblocking) const
{
  // https://beej.us/guide/bgnet/html/#blocking
  auto flags = fcntl(m_socket_id, F_GETFL, 0);
  fcntl(m_socket_id,
        F_SETFL,
        nonblocking ? (flags | O_NONBLOCK) : (flags & (~O_NONBLOCK)));
}

bool
UnixSocket::start(bool is_server)
{
  if (m_init_status) {
    return true;
  }

  // If this is a server and the socket path already exists, it might mean a
  // stale socket. We might want to clean it up. For client, existence is
  // crucial.
  if (is_server && exists()) {
    std::cout << "Warning: Socket path " << generate_path()
              << " already exists. Attempting to remove stale socket."
              << "\n";
    try {
      std::filesystem::remove(generate_path());
    } catch (const std::exception& e) {
      std::cerr << "Failed to remove stale socket: " << e.what() << "\n";
      // Decide if this should be a fatal error or just a warning.
    }
  } else if (!is_server && !exists()) {
    // std::cerr << "Error: Socket path " << generate_path()
    //           << " does not exist (client mode)." << "\n";
    return false;
  }

  m_socket_id = create_and_connect_socket();
  if (m_socket_id == invalid_socket_t) {
    return false; // Socket creation/connection failed.
  }

  // Set up the Stream with the socket and the buffer instances.
  // TODO Decide if clearing here is appropriate i.e. m_write_buffer.clear();
  m_socket_stream = std::make_unique<Stream>(m_socket_id);

  m_should_end_flag.store(false); // Reset flag
  m_init_status = true;

  // Start the listener thread.
  // Ensure any previous thread is joined before starting a new one.
  if (m_listen_thread.joinable()) {
    m_listen_thread.join();
  }
  m_listen_thread = std::thread(&UnixSocket::listener_loop, this);

  return true;
}

fs::path
UnixSocket::generate_path() const
{
  char sp[SOCKET_PATH_LENGTH];
  int s = snprintf(sp, sizeof(sp), SOCKET_PATH_TEMPLATE, m_path.c_str());

  if (s < 0) {
    std::cerr << "DEBUG Generate socket path failed with snprintf returning "
              << s << "\n";
  }
  return fs::path(sp);
}

void
UnixSocket::end()
{
  if (!m_init_status) {
    return;
  }

  m_should_end_flag.store(true); // Signal the listener thread to stop.
  // If a listener thread is active, wait for it to finish.
  if (m_listen_thread.joinable()) {
    // We need to unblock the listener thread if it's waiting on select or read.
    // A common way is to close the socket, which will cause recv/select to
    // return an error. Alternatively, a special "shutdown" message or event
    // could be used. For simplicity here, we just close the socket.
    close();
    m_listen_thread.join();
  }

  if (m_socket_id != invalid_socket_t) {
    close();
    m_socket_id = invalid_socket_t;
  }

  // Reset state.
  m_init_status = false;
  m_socket_stream.reset();
}

int
UnixSocket::close() const
{
  if (m_socket_id == invalid_socket_t) {
    return 0;
  }
#ifdef _WIN32
  return ::closesocket(m_socket_id)
#else
  return ::close(m_socket_id);
#endif
}

bool
UnixSocket::exists() const
{
  return fs::exists(generate_path());
}

// send method: Serializes message, writes to buffer, then sends.
OpCode
UnixSocket::send(nonstd::span<const uint8_t> msg)
{
  if (!m_init_status || !m_socket_stream) {
    return OpCode::error;
  }

  int bytes_sent = m_socket_stream->write(msg);

  if (bytes_sent < 0) {
    return OpCode::error;
  } else if (static_cast<size_t>(bytes_sent) < msg.size()) {
    // Partial send. This implies the socket might be blocked or the receiver is
    // slow. For simplicity, we treat partial sends as an error or a condition
    // needing retry. POSSIBLE implementation may loop, robustly sending
    // remaining data.
    std::cerr << "UnixSocket WARNING: Partial send, only " << bytes_sent
              << " of " << msg.size() << " bytes sent\n";
    return OpCode::error;
  }

  // Message successfully sent.
  return OpCode::ok;
}

OpCode
UnixSocket::receive(size_t& bytes_available)
{
  m_is_receiving.store(true);
  std::unique_lock<std::mutex> lock(m_buffer_data_mutex);
  if (!m_buffer_data_cond.wait_for(
        lock, std::chrono::seconds(MESSAGE_TIMEOUT), [&] {
          m_is_receiving.store(false);
          return m_bytes_available_in_buffer.load() > 0;
        })) {
    return OpCode::timeout;
  }
  bytes_available = m_bytes_available_in_buffer.load();
  m_bytes_available_in_buffer.store(0);
  m_is_receiving.store(false);
  return OpCode::ok;
}

void
UnixSocket::listener_loop()
{
  StreamReader reader(*m_socket_stream, m_read_buffer);
  // disattached thread keeps working through this cycle
  while (!m_should_end_flag) {
    const auto message = reader.read_all(m_should_end_flag);
    reader.reset_read_state();

    if (m_should_end_flag) {
      break;
    }

    if (message.has_value()) {
      {
        std::lock_guard<std::mutex> lock(m_buffer_data_mutex);
        m_bytes_available_in_buffer.store(message.value());
      }
      m_buffer_data_cond.notify_one();
    }
  }
}

bool
UnixSocket::wait_until_ready(time_t sec,
                             time_t usec,
                             bool read_ready,
                             bool write_ready) const
{
  if (m_socket_id == invalid_socket_t) {
    return false;
  }
  // https://beej.us/guide/bgnet/html/#select
  fd_set read_fds;
  fd_set write_fds;
  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);

  if (read_ready) {
    FD_SET(m_socket_id, &read_fds);
  }
  if (write_ready) {
    FD_SET(m_socket_id, &write_fds);
  }

  timeval tv;
  tv.tv_sec = static_cast<long>(sec);
  tv.tv_usec = static_cast<long>(usec);

  int ready_fds = ::select(static_cast<int>(m_socket_id + 1),
                           read_ready ? &read_fds : nullptr,
                           write_ready ? &write_fds : nullptr,
                           nullptr, // Exceptfds
                           &tv);

  if (ready_fds < 0) {
    // Error during select
    return false;
  } else if (ready_fds == 0) {
    // Timeout
    return false;
  }

  if (read_ready && FD_ISSET(m_socket_id, &read_fds)) {
    return true;
  }
  if (write_ready && FD_ISSET(m_socket_id, &write_fds)) {
    return true;
  }

  // This case should ideally not be reached if ready_fds > 0 and we checked the
  // sets. However, if we requested readiness for both, and only one happened,
  // this logic might need refinement. For now, if we requested readiness and
  // select returned >0, we assume success.
  return true; // Should have been handled by previous checks.
}

socket_t
UnixSocket::create_and_connect_socket()
{
  struct sockaddr_un maddr;
  std::filesystem::path socket_path = generate_path();

  if (socket_path.empty()) {
    std::cerr << "Failed to get valid socket path.\n";
    return invalid_socket_t;
  }
#ifdef _WIN32
  // Whilst they claim sockaddr_un to have identical syntax for windows
  // In the following example it looks different
  // https://devblogs.microsoft.com/commandline/windowswsl-interop-with-af_unix/
  // SOCKADDR_UN ServerSocket = { 0 };
  WORD wVersionRequested;
  WSADATA wsaData;

  // Initialising Winsock 2.2
  // https://learn.microsoft.com/en-us/windows/win32/winsock/initializing-winsock
  wVersionRequested = MAKEWORD(2, 2);
  if (WSAStartup(wVersionRequested, &wsaData) != 0) {
    std::cerr << "Unable to load WinSock DLL";
    return false;
  }
#endif

  memset(&maddr, 0, sizeof(maddr));
  maddr.sun_family = AF_UNIX;
  strncpy(maddr.sun_path, socket_path.c_str(), sizeof(maddr.sun_path) - 1);

  m_socket_id = ::socket(AF_UNIX, SOCK_STREAM, 0);
  if (m_socket_id == invalid_socket_t) {
    std::cerr << "Failed to create socket.\n";
#ifdef _WIN32
    WSACleanup();
#endif
    return invalid_socket_t;
  }

  // make the socket ready
  set_nonblocking(true);
  if (establish_connection(socket_path, false) < 0) {
    // Handle connection/bind errors. Wait for socket to become ready to confirm
    // connect success/failure.
    if (!wait_until_ready(1, 0, false, true)) { // Wait for write readiness
      std::cerr << "Connection/bind failed or timed out." << "\n";
      close();
#ifdef _WIN32
      WSACleanup();
#endif
      return invalid_socket_t;
    }
    // If wait_until_ready succeeded, check for errors with getsockopt
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(m_socket_id,
                   SOL_SOCKET,
                   SO_ERROR,
                   reinterpret_cast<char*>(&error),
                   &len)
          < 0
        || error) {
      std::cerr << "Socket SO_ERROR: " << error << "\n";
      close();
#ifdef _WIN32
      WSACleanup();
#endif
      return invalid_socket_t;
    }
  }

  // For clients, make socket blocking again after connect, or manage select
  // carefully. For servers, it might be better to keep it non-blocking for
  // accept. For simplicity, let's assume it's used for one-to-one
  // communication, so making it blocking again after successful connect might
  // simplify the read/write logic. However, `Stream` uses select, so keeping it
  // non-blocking is likely intended.
  set_nonblocking(false);
  return m_socket_id;
}

// establish_connection helper (for bind/connect)
int
UnixSocket::establish_connection(const std::filesystem::path& path,
                                 bool is_server) const
{
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  addr.sun_path[sizeof(addr.sun_path) - 1] = '\0'; // Ensure null termination

  if (is_server) {
    // Server: Bind the socket to the address.
    if (::bind(
          m_socket_id, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))
        < 0) {
      std::cerr << "Bind failed: " << strerror(errno) << "\n";
      return -1;
    }
    // Listen for incoming connections.
    if (::listen(m_socket_id, SOMAXCONN)
        < 0) { // SOMAXCONN is a reasonable backlog size
      std::cerr << "Listen failed: " << strerror(errno) << "\n";
      return -1;
    }
    // For a server, we'd typically then call accept() in a loop,
    // but this is a single-client connection per UnixSocket object.
    // So, after listen, we wait for the client to connect (which happens in
    // wait_until_ready on connect call). The `connect` call below will be
    // done by the client side. If this `UnixSocket` object is meant to be a
    // server that ACCEPTS connections, the logic needs to be different
    // (handling multiple clients). For now, assuming this UnixSocket object
    // represents one end of a client-server connection. If this IS the server
    // socket, us bind() to create the socket.
    return 0;
  } else {
    // Client: Connect to the server address.
    if (::connect(
          m_socket_id, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr))
        < 0) {
      // If it's not EINPROGRESS (for non-blocking sockets), it's a real error.
      if (errno != EINPROGRESS && errno != EALREADY) {
        std::cerr << "Connect failed: " << strerror(errno) << "\n";
        return -1;
      }
      // If EINPROGRESS, the connection is in progress, wait for readiness.
    }
    return 0; // Success (or connection in progress)
  }
}
