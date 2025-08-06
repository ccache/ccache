// Guide for internet sockets https://beej.us/guide/bgnet/html/
// But applied to a unix socket

#pragma once

#include "ccache/storage/remote/socketbackend/tlv_buffer.hpp"

#include <nonstd/span.hpp>

#include <fcntl.h>

#include <atomic>
#include <cassert>
#include <cctype>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <thread>

#ifdef _WIN32
#  include <windows.h>
#  include <winsock2.h>
#  include <ws2tcpip.h>
// AF_UNIX comes to Windows
// https://devblogs.microsoft.com/commandline/af_unix-comes-to-windows/
#  include <afunix.h>

using socket_t = SOCKET;
constexpr auto invalid_socket_t = INVALID_SOCKET;
#else
#  include <sys/socket.h>

using socket_t = int;
constexpr auto invalid_socket_t = -1;
#endif
#include <sys/un.h>
#include <unistd.h>

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

using namespace std::literals::string_view_literals;
namespace fs = std::filesystem;

constexpr auto SOCKET_PATH_LENGTH = 256;
constexpr auto SOCKET_PATH_TEMPLATE =
  "/home/rocky/repos/py_server_script/daemons/backend-%s.sock";

constexpr auto BUFFERSIZE = 8192;

constexpr std::size_t MESSAGE_TIMEOUT = 15;
constexpr std::size_t READ_TIMEOUT_SECOND = 0;
constexpr std::size_t READ_TIMEOUT_USECOND = 100;

// assertion for the current state of serialisation
static_assert(BUFFERSIZE % 2 == 0,
              "Buffer size should be set to a value dividable by 2!");

enum class OpCode : uint8_t {
  error,
  timeout,
  ok,
};

class Stream
{
public:
  Stream(socket_t sock);

  /// @brief receives messages from stream
  template<typename T> int read(nonstd::span<T>& ptr);
  /// @brief sends messages over stream
  template<typename T> int write(const nonstd::span<T>& ptr) const;

private:
  /// @brief the socket identifier
  socket_t m_sock;
  /// @brief calls select() and waits until timeout
  int select_read(time_t sec, time_t usec) const;
};

inline Stream::Stream(socket_t sock)
  : m_sock(sock)
{
}

inline int
Stream::select_read(time_t sec, time_t usec) const
{
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(m_sock, &fds);

  timeval tv;
  tv.tv_sec = static_cast<long>(sec);
  tv.tv_usec = static_cast<long>(usec);

  return ::select(static_cast<int>(m_sock + 1), &fds, nullptr, nullptr, &tv);
}

template<typename T>
inline int
Stream::read(nonstd::span<T>& ptr)
{
  if (select_read(std::chrono::seconds(READ_TIMEOUT_SECOND).count(),
                  std::chrono::milliseconds(READ_TIMEOUT_USECOND).count())
      > 0) {
    return ::recv(m_sock, ptr.data(), static_cast<int>(ptr.size_bytes()), 0);
  }
  return 0;
}

template<typename T>
inline int
Stream::write(const nonstd::span<T>& ptr) const
{
  return ::send(m_sock, ptr.data(), static_cast<int>(ptr.size()), MSG_NOSIGNAL);
}

// adapted from cpp-httplib
class StreamReader
{
public:
  StreamReader(Stream& strm);

  const char* ptr() const;

  size_t size() const;

  std::optional<size_t> getbytes(std::optional<char> delimiter,
                                 const std::atomic<bool>& shouldStop);

  void clear();

private:
  void append(uint8_t c);
  void appendChunk(const uint8_t* data, size_t len);

  std::function<void(char byte)> m_callback;
  std::reference_wrapper<Stream> m_strm;
  std::reference_wrapper<tlv::BigBuffer<uint8_t>>
    m_buffer;                // Reference to BigBuffer
  size_t m_current_size = 0; // Track buffer content
};

inline StreamReader::StreamReader(Stream& strm)
  : m_strm(strm),
    m_buffer(tlv::g_read_buffer)
{
  m_buffer.get().release(); // Reset buffer
}

inline const char*
StreamReader::ptr() const
{
  return reinterpret_cast<const char*>(m_buffer.get().data());
}

inline size_t
StreamReader::size() const
{
  return m_current_size;
}

inline void
StreamReader::clear()
{
  m_current_size = 0;
  // TODO should we release the buffer or just reset our tracking?
}

inline void
StreamReader::append(uint8_t c)
{
  m_buffer.get().write(&c, sizeof(uint8_t));
  m_current_size++;
}

inline std::optional<size_t>
StreamReader::getbytes(const std::optional<char> delimiter,
                       const std::atomic<bool>& shouldStop)
{
  constexpr size_t CHUNK_SIZE = 1024;
  std::array<unsigned char, CHUNK_SIZE> chunk_buffer;

  while (!shouldStop) {
    nonstd::span<unsigned char> chunk(chunk_buffer);
    int byte_count = m_strm.get().read(chunk);

    if (byte_count == 0) {
      if (size() == 0) {
        return std::nullopt;
      }
      return m_current_size;
    } else if (byte_count < 0) {
      continue;
    }

    for (int i = 0; i < byte_count; ++i) {
      uint8_t byte = chunk_buffer[i];

      if (delimiter && byte == *delimiter) {
        // Message complete
        return m_current_size;
      }

      append(byte);
    }
  }

  return std::nullopt;
}

inline void
StreamReader::appendChunk(const uint8_t* data, size_t len)
{
  m_buffer.get().write(data, len);
  m_current_size += len;
}

class UnixSocket
{
private:
  /// @brief describes the state of the socket (initialised/closed)
  bool m_init_status = false;

  /// @brief the socket descriptor
  socket_t m_socket_id = invalid_socket_t;

  /// @brief specifies where the socket is
  std::string m_path;

  /// @brief specifies whether connection should close
  std::atomic<bool> m_should_end{true};

  /// @brief negotiated delimiter
  char m_delimiter;

  /// @brief specifies the stream for reading / writing
  std::shared_ptr<Stream> m_socket_stream;

  /// @brief listens for messages over stream
  std::thread m_listen_thread;

  /// @brief stores the number of bytes available
  std::atomic<size_t> m_bytes_available{0};

  /// @brief signals whether message is ready
  std::mutex m_signal_mutex;
  std::condition_variable m_signal_cond;

  /// @brief waits for messages - called by the listener thread
  void wait_for_messages();

  /// @brief waits a specified amount of time for a socket to be ready
  bool wait_until_ready(time_t sec, time_t usec) const;

  /// @brief creates the socket
  socket_t create_socket();

  /// @brief creates the connection either as server or client
  std::function<int(sockaddr_un&)> establish_connection;

  /// @brief sets the non-blocking property of socket fd
  void set_nonblocking(bool nonblocking) const;

public:
  UnixSocket() = delete;
  UnixSocket(const std::string& host, char msg_delimiter);
  ~UnixSocket();

  /// @brief generate an encoded file system path to the socket
  fs::path generate_path();

  /// @brief starts the connection over socket
  bool start(bool is_server = false);

  /// @brief ends the connection and terminates listener thread
  void end();

  /// @brief checks whether the socket's path exists
  bool exists();

  /// @brief calls send and waits for an upcoming response
  OpCode send(const nonstd::span<uint8_t>& msg_args);

  /// @brief calls send and waits for an upcoming response
  OpCode receive(size_t& res_size);

private:
  /// @brief sends message over the stream
  bool send_message(const nonstd::span<uint8_t>& msg_args) const;

  /// @brief returns true on connection error
  bool is_connection_error() const;

  /// @brief closes the socket
  int close() const;
};

inline UnixSocket::~UnixSocket()
{
#ifdef _WIN32
  if (WSACleanup() != 0) {
    std::cerr << "WSACleanup() failed";
  }
#endif
  end();
}

inline void
UnixSocket::set_nonblocking(bool nonblocking) const
{
  // https://beej.us/guide/bgnet/html/#blocking
  auto flags = fcntl(m_socket_id, F_GETFL, 0);
  fcntl(m_socket_id,
        F_SETFL,
        nonblocking ? (flags | O_NONBLOCK) : (flags & (~O_NONBLOCK)));
}

inline bool
UnixSocket::is_connection_error() const
{
  return errno != EINPROGRESS;
}

inline bool
UnixSocket::start(bool is_server)
{
  if (m_init_status) {
    return true;
  }

  if (!exists()) {
    return false;
  }

  establish_connection = ([this, is_server](struct sockaddr_un& sun) {
    return is_server ? ::bind(m_socket_id,
                              reinterpret_cast<struct sockaddr*>(&sun),
                              sizeof(sun))
                     : ::connect(m_socket_id,
                                 reinterpret_cast<struct sockaddr*>(&sun),
                                 sizeof(sun));
  });

  m_socket_id = create_socket();
  if (m_socket_id == invalid_socket_t) {
    return false;
  }

  if (m_listen_thread.joinable()) {
    m_listen_thread.join(); // wait for it to finish execution
  }

  m_socket_stream = std::make_shared<Stream>(m_socket_id);
  m_should_end = false;
  m_init_status = true;

  m_listen_thread = std::thread(&UnixSocket::wait_for_messages, this);

  return true;
}

inline UnixSocket::UnixSocket(const std::string& host, const char msg_delimiter)
  : m_path(host),
    m_delimiter(msg_delimiter)
{
}

inline fs::path
UnixSocket::generate_path()
{
  char sp[SOCKET_PATH_LENGTH];
  int s = snprintf(sp, sizeof(sp), SOCKET_PATH_TEMPLATE, m_path.c_str());

  if (s < 0) {
    std::cerr << "DEBUG Generate socket path failed with snprintf returning "
              << s << "\n";
  }
  return fs::path(sp);
}

inline void
UnixSocket::end()
{
  m_should_end = true;
  assert(!close());

  m_listen_thread.detach();

  m_init_status = false;
}

inline int
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

inline bool
UnixSocket::send_message(const nonstd::span<uint8_t>& msg_args) const
{
  return m_socket_stream->write(msg_args) != -1;
}

inline bool
UnixSocket::exists()
{
  return fs::exists(generate_path());
}

inline OpCode
UnixSocket::send(const nonstd::span<uint8_t>& msg)
{
  const bool success = send_message(msg);
  return success ? OpCode::ok : OpCode::error;
}

inline OpCode
UnixSocket::receive(size_t& res_size)
{
  std::unique_lock<std::mutex> lock(m_signal_mutex);
  if (!m_signal_cond.wait_for(lock, std::chrono::seconds(MESSAGE_TIMEOUT), [&] {
        return m_bytes_available.load() > 0;
      })) {
    return OpCode::timeout;
  }
  res_size = m_bytes_available.load();
  m_bytes_available.store(0);
  return OpCode::ok;
}

inline void
UnixSocket::wait_for_messages()
{
  StreamReader reader(*m_socket_stream);
  // disattached thread keeps working through this cycle
  while (!m_should_end) {
    const auto message = reader.getbytes(m_delimiter, m_should_end);
    reader.clear();

    if (m_should_end) {
      break;
    }

    if (message.has_value()) {
      {
        std::lock_guard<std::mutex> lock(m_signal_mutex);
        m_bytes_available.store(message.value());
      }
      m_signal_cond.notify_one();
    }
  }
}

inline bool
UnixSocket::wait_until_ready(time_t sec, time_t usec) const
{
  // https://beej.us/guide/bgnet/html/#select
  fd_set fdsr;
  FD_ZERO(&fdsr);
  FD_SET(m_socket_id, &fdsr);

  fd_set fdsw = fdsr;
  fd_set fdse = fdsr;

  timeval tv;
  tv.tv_sec = static_cast<long>(sec);
  tv.tv_usec = static_cast<long>(usec);

  if (select(static_cast<int>(m_socket_id + 1), &fdsr, &fdsw, &fdse, &tv) < 0) {
    return false;
  } else if (FD_ISSET(m_socket_id, &fdsr) || FD_ISSET(m_socket_id, &fdsw)) {
    int error = 0;
    socklen_t len = sizeof(error);
    if (getsockopt(m_socket_id,
                   SOL_SOCKET,
                   SO_ERROR,
                   reinterpret_cast<char*>(&error),
                   &len)
          < 0
        || error) {
      return false;
    }
  } else {
    return false;
  }

  return true;
}

inline socket_t
UnixSocket::create_socket()
{
  struct sockaddr_un maddr;
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
  fs::path socket_path = generate_path();
  strncpy(maddr.sun_path, socket_path.c_str(), sizeof(maddr.sun_path) - 1);

  m_socket_id = ::socket(AF_UNIX, SOCK_STREAM, 0);

  // make the socket ready
  set_nonblocking(true);
  if (establish_connection(maddr) < 0) {
    if (is_connection_error()
        || !wait_until_ready(std::chrono::seconds(1).count(), 0)) {
#ifdef _WIN32
      std::cerr << "Establishing connection failed: " << WSAGetLastError()
                << "\n";
#else
      std::cerr << "Establishing connection fialed: Errno=" << errno << "\n";
#endif
      close();
      return invalid_socket_t;
    }
  }

  set_nonblocking(false);
  return m_socket_id;
}
