// Guide for internet sockets https://beej.us/guide/bgnet/html/
// But applied to a unix socket

#pragma once

#include <nonstd/span.hpp>

#include <fcntl.h>
#include <netdb.h>
#include <readerwriterqueue/readerwritercircularbuffer.hpp>

#include <cassert>
#include <cctype>
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
#include <string_view>

using namespace std::literals::string_view_literals;
namespace fs = std::filesystem;

constexpr auto SOCKET_PATH_LENGTH = 256;
constexpr auto SOCKET_PATH_TEMPLATE =
  "/home/rocky/repos/py_server_script/daemons/backend-%s.sock";

constexpr auto BUFFERSIZE = 8192;
constexpr auto LOCKFREEQUEUE_CAP = 8;

constexpr std::size_t MESSAGE_TIMEOUT = 15;
constexpr std::size_t READ_TIMEOUT_SECOND = 5;
constexpr std::size_t READ_TIMEOUT_USECOND = 0;

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
  /// @brief calls write(const nonstd::span<T>& ptr)
  int write(const std::string_view& s) const;

private:
  /// @brief the socket identifier
  socket_t m_sock;
  /// @brief calls select() and waits until timeout
  int select_read(time_t sec, time_t usec) const;
};

inline Stream::Stream(socket_t sock) : m_sock(sock)
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
  return -1;
}

template<typename T>
inline int
Stream::write(const nonstd::span<T>& ptr) const
{
  return ::send(m_sock, ptr.data(), static_cast<int>(ptr.size()), MSG_NOSIGNAL);
}

inline int
Stream::write(const std::string_view& s) const
{
  return write(nonstd::span<const char>(s.data(), s.size()));
}

// adapted from cpp-httplib
class StreamReader
{
public:
  StreamReader(Stream& strm, char* fixed_buffer, size_t fixed_buffer_size);

  const char* ptr() const;

  size_t size() const;

  std::optional<std::string> getbytes(std::optional<char> delimiter,
                                      const std::atomic<bool>& shouldStop);

private:
  void append(char c);

  size_t m_fixed_buffer_size;
  char* m_fixed_buffer;
  std::reference_wrapper<Stream> m_strm;
  size_t m_fixed_buffer_used_size;
  std::string m_buffer;
};

inline StreamReader::StreamReader(Stream& strm,
                                  char* fixed_buffer,
                                  const size_t fixed_buffer_size)
  : m_fixed_buffer_size(fixed_buffer_size),
    m_fixed_buffer(fixed_buffer),
    m_strm(strm)
{
}

inline const char*
StreamReader::ptr() const
{
  if (m_buffer.empty()) {
    return m_fixed_buffer;
  } else {
    return m_buffer.data();
  }
}

inline size_t
StreamReader::size() const
{
  if (m_buffer.empty()) {
    return m_fixed_buffer_used_size;
  } else {
    return m_buffer.size();
  }
}

inline std::optional<std::string>
StreamReader::getbytes(const std::optional<char> delimiter,
                       const std::atomic<bool>& shouldStop)
{
  m_fixed_buffer_used_size = 0;
  m_buffer.clear();

  while (!shouldStop) {
    std::array<char, 1> byte_holder;
    nonstd::span<char> byte(byte_holder);

    int next_byte = m_strm.get().read(byte);
    if (next_byte == 0) {
      return std::nullopt;
    } else if (next_byte < 0) {
      continue;
    }

    if (byte[0] == delimiter) {
      // message complete
      return std::string(ptr(), size());
    }
    append(byte[0]);
  }

  return std::nullopt;
}

inline void
StreamReader::append(char c)
{
  if (m_fixed_buffer_used_size + 1 < m_fixed_buffer_size) {
    m_fixed_buffer[m_fixed_buffer_used_size++] = c;
    m_fixed_buffer[m_fixed_buffer_used_size] = '\0';
  } else {
    if (m_buffer.empty()) {
      assert(m_fixed_buffer[m_fixed_buffer_used_size] == '\0');
      m_buffer.assign(m_fixed_buffer, m_fixed_buffer_used_size);
    }
    m_buffer += c;
  }
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
  std::atomic<bool> m_should_end = true;

  /// @brief negotiated delimiter
  char m_delimiter;

  /// @brief specifies the stream for reading / writing
  std::shared_ptr<Stream> m_socket_stream;

  /// @brief is behaviour after message receival set?
  std::atomic<bool> m_message_callback = false;
  /// @brief called upon receiving a message
  std::function<void(const std::string& message)> message_callback;

  /// @brief listens for messages over stream
  std::thread m_listen_thread;

  /// @brief buffer pool for message receival
  moodycamel::BlockingReaderWriterCircularBuffer<std::string> m_msg_queue;

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
  template<typename T> OpCode send(const T& msg_args);

  /// @brief calls send and waits for an upcoming response
  OpCode receive(std::string& result, bool timeout = true);

private:
  /// @brief sends message over the stream
  bool send_message(const std::string& msg_args) const;

  /// @brief returns true on connection error
  bool is_connection_error() const;

  /// @brief sets the callback function on messag receival
  void set_response_behaviour(
    std::function<void(const std::string& message)> callback);

  /// @brief closes the socket
  int close() const;
};

inline void
UnixSocket::set_response_behaviour(
  const std::function<void(const std::string& message)> callback)
{
  message_callback = callback;
}

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
    m_delimiter(msg_delimiter),
    m_msg_queue(LOCKFREEQUEUE_CAP)
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
UnixSocket::send_message(const std::string& msg_args) const
{
  return m_socket_stream->write(msg_args) != -1;
}

inline bool
UnixSocket::exists()
{
  return fs::exists(generate_path());
}

template<typename T>
inline OpCode
UnixSocket::send(const T& msg)
{
  std::string msg_string;
  msg.encode(msg_string);

  const bool success = send_message(msg_string + m_delimiter);
  return success ? OpCode::ok : OpCode::error;
}

inline OpCode
UnixSocket::receive(std::string& result, const bool timeout)
{
  bool got_message = false;

  if (!timeout) {
    m_msg_queue.try_dequeue(result);
    got_message = true;
  } else {
    got_message = m_msg_queue.wait_dequeue_timed(
      result, std::chrono::seconds(MESSAGE_TIMEOUT));

    if (!got_message) {
      return OpCode::timeout;
    }
  }
  return OpCode::ok;
}

inline void
UnixSocket::wait_for_messages()
{
  const auto bufsiz = BUFFERSIZE;
  char buf[bufsiz];

  // disattached thread keeps working through this cycle
  while (!m_should_end) {
    StreamReader reader(*m_socket_stream, buf, bufsiz);

    const auto message = reader.getbytes(m_delimiter, m_should_end);

    if (m_should_end) {
      break;
    }

    if (message.has_value()) {
      m_msg_queue.try_enqueue(message.value());

      if (m_message_callback) {
        message_callback(*message);
      }
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
