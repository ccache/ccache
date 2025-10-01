#include "socketinterface.hpp"

#include <atomic>
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
Stream::read(const nonstd::span<uint8_t> writable_span) const
{
  try {
    // Check if the buffer is actually ready to write into.
    // If prepare_write_span returned an empty span or threw, this might fail.
    if (writable_span.empty()) {
      // This case implies writable span is invalid. Either n=0 or too large!
      // CHECK StreamBuffer::prepare
      return 0;
    }

    // Before calling the recv function make sure select_read was called over
    // the socket. It waits for the socket to be ready for reading. We pass the
    // size of the span we prepared as the amount we *want* to read. The select
    // timeout is handled by StreamReader. If event_result > 0, the socket is
    // ready for reading.

    // The actual number of bytes to attempt to read is the size of the span.
    ssize_t bytes_read = ::recv(m_sock,
                                writable_span.data(),
                                writable_span.size(),
                                0); // MSG_NOSIGNAL on send usually, not recv

    if (bytes_read > 0) {
      return bytes_read;
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
  return ::send(m_sock, ptr.data(), ptr.size(), MSG_NOSIGNAL);
}

BufferedStreamReader::SocketStreamBuf::SocketStreamBuf(
  Stream& stream, std::chrono::milliseconds timeout)
  : m_buffer(g_buffersize),
    m_stream(stream),
    m_timeout(timeout)
{
  // Initialize empty buffer
  setg(m_buffer.data(), m_buffer.data(), m_buffer.data());
}

std::streambuf::int_type
BufferedStreamReader::SocketStreamBuf::underflow()
{
  if (gptr() < egptr()) {
    return traits_type::to_int_type(*gptr());
  }

  int ready = m_stream.get().select_read(m_timeout.count(), 0, true, false);
  if (ready <= 0) {
    return traits_type::eof();
  }

  nonstd::span<uint8_t> write_span(reinterpret_cast<uint8_t*>(m_buffer.data()),
                                   g_buffersize);
  size_t bytes_read = m_stream.get().read(write_span);

  if (bytes_read == 0) {
    return traits_type::eof();
  }

  // Update buffer pointers
  setg(m_buffer.data(), m_buffer.data(), m_buffer.data() + bytes_read);
  return traits_type::to_int_type(*gptr());
}

// BufferedStreamReader implementation
BufferedStreamReader::BufferedStreamReader(Stream& stream,
                                           std::chrono::milliseconds timeout)
  : m_streambuf(stream, timeout),
    m_stream(&m_streambuf)
{
}

tl::expected<size_t, OpError>
BufferedStreamReader::read_exactly(size_t n, nonstd::span<uint8_t> result)
{
  if (result.size() < n) {
    return tl::unexpected<OpError>(OpError::error);
  }

  m_stream.read(reinterpret_cast<char*>(result.data()), n);

  if (m_stream.gcount() != static_cast<std::streamsize>(n)) {
    return tl::unexpected<OpError>(m_stream.eof() ? OpError::timeout
                                                  : OpError::error);
  }

  return n;
}

tl::expected<uint8_t, OpError>
BufferedStreamReader::read_byte()
{
  int c = m_stream.get();
  if (c == std::istream::traits_type::eof()) {
    return tl::unexpected<OpError>(OpError::error);
  }
  return static_cast<uint8_t>(c);
}

UnixSocket::UnixSocket(const std::string& host)
  : m_path(host)
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
  m_is_server = is_server;

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
    return false;
  }

  m_socket_id = create_and_connect_socket();
  if (m_socket_id == invalid_socket_t) {
    return false; // Socket creation/connection failed.
  }

  // For server mode, use client socket for Stream after accept
  socket_t stream_socket = m_is_server ? m_client_socket_id : m_socket_id;

  // Set up the Stream with the socket and the buffer instances.
  // TODO Decide if clearing here is appropriate i.e. m_write_buffer.clear();
  m_socket_stream = std::make_unique<Stream>(stream_socket);

  if (is_server) {
    m_should_end_flag.store(false); // Reset flag
  }
  m_init_status = true;

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

  m_should_end_flag.store(true); // Stop signal

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
tl::expected<size_t, OpError>
UnixSocket::send(nonstd::span<const uint8_t> msg)
{
  if (!m_init_status || !m_socket_stream) {
    return tl::unexpected<OpError>(OpError::error);
  }

  size_t bytes_sent = m_socket_stream->write(msg);

  if (bytes_sent < 0) {
    return tl::unexpected<OpError>(OpError::error);
  } else if (bytes_sent < msg.size()) {
    // TODO: currently not handled
    // Partial send. This implies the socket might be blocked or the receiver is
    // slow. For simplicity, we treat partial sends as an error or a condition
    // needing retry. POSSIBLE implementation may loop, robustly sending
    // remaining data.
    std::cerr << "UnixSocket WARNING: Partial send, only " << bytes_sent
              << " of " << msg.size() << " bytes sent\n";
    return tl::unexpected<OpError>(OpError::interrupted);
  }

  // Message successfully sent.
  return bytes_sent;
}

std::unique_ptr<BufferedStreamReader>
UnixSocket::create_reader(bool is_op)
{
  if (!m_init_status || !m_socket_stream) {
    return nullptr;
  }

  return std::make_unique<BufferedStreamReader>(
    *m_socket_stream, is_op ? g_operation_timeout : CONNECTION_TIMEOUT);
}

bool
UnixSocket::wait_until_ready(time_t sec,
                             time_t usec,
                             bool read_ready,
                             bool write_ready) const
{
  // Use appropriate socket for waiting (client socket for server mode)
  socket_t wait_socket = m_is_server ? m_client_socket_id : m_socket_id;

  if (wait_socket == invalid_socket_t) {
    return false;
  }
  // https://beej.us/guide/bgnet/html/#select
  fd_set read_fds;
  fd_set write_fds;
  FD_ZERO(&read_fds);
  FD_ZERO(&write_fds);

  if (read_ready) {
    FD_SET(wait_socket, &read_fds);
  }
  if (write_ready) {
    FD_SET(wait_socket, &write_fds);
  }

  timeval tv;
  tv.tv_sec = static_cast<long>(sec);
  tv.tv_usec = static_cast<long>(usec);

  int ready_fds = ::select(static_cast<int>(wait_socket + 1),
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

  if (read_ready && FD_ISSET(wait_socket, &read_fds)) {
    return true;
  }
  if (write_ready && FD_ISSET(wait_socket, &write_fds)) {
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
  if (establish_connection(socket_path, m_is_server) < 0) {
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
  if (!m_is_server) {
    set_nonblocking(false);
  }

  return m_socket_id;
}

// establish_connection helper (for bind/connect)
int
UnixSocket::establish_connection(const std::filesystem::path& path,
                                 bool is_server)
{
  struct sockaddr_un addr;
  memset(&addr, 0, sizeof(addr));
  addr.sun_family = AF_UNIX;
  strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
  addr.sun_path[sizeof(addr.sun_path) - 1] = '\0'; // Ensure null termination

  if (is_server) {
    // Server: Bind the socket to the address.
    // We allow the socket to act as a server although this is not required
    // within ccache as it helps creating a simple unittest for testing out the
    // socket send and receive methods.
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

    // Accept one client connection
    struct sockaddr_un client_addr;
    socklen_t client_len = sizeof(client_addr);

    set_nonblocking(false);
    m_client_socket_id =
      ::accept(m_socket_id,
               reinterpret_cast<struct sockaddr*>(&client_addr),
               &client_len);

    if (m_client_socket_id < 0) {
      std::cerr << "Accept failed: " << strerror(errno) << "\n";
      return -1;
    }

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
