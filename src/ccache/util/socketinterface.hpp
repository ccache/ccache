// Guide for internet sockets https://beej.us/guide/bgnet/html/
// But applied to a unix socket

#pragma once

#include "ccache/util/streambuffer.hpp"

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
#include <optional>
#include <string>

namespace fs = std::filesystem;
using StreamBuffer = tlv::StreamBuffer<uint8_t>;

constexpr auto SOCKET_PATH_LENGTH = 256;
constexpr auto SOCKET_PATH_TEMPLATE =
  "/home/rocky/repos/py_server_script/daemons/backend-%s.sock";

size_t BUFFERSIZE = 1024;
std::chrono::seconds OPERATION_TIMEOUT{15};

constexpr std::chrono::seconds CONNECTION_TIMEOUT{5};
constexpr std::chrono::seconds READ_TIMEOUT_SECOND{0};
constexpr std::chrono::microseconds READ_TIMEOUT_USECOND{100};

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
  size_t read(nonstd::span<uint8_t> writable_span);
  /// @brief sends messages over stream
  size_t write(nonstd::span<const uint8_t> ptr) const;

private:
  /// @brief the socket identifier
  socket_t m_sock;
  /// @brief calls select() and waits until timeout
  int select_read(time_t sec,
                  time_t usec,
                  bool read_possible,
                  bool write_possible) const;
};

// adapted from cpp-httplib
class StreamReader
{
public:
  StreamReader(Stream& strm, StreamBuffer& buffer);

  /// @brief Reads data from the stream into the buffer until a condition is met
  /// or an error occurs
  std::optional<size_t> read_all(const std::atomic<bool>& should_stop);

  /// @brief Clears the internal tracking of read data, but does NOT clear the
  /// buffer itself
  void reset_read_state();

private:
  // The stream to read from.
  std::reference_wrapper<Stream> m_stream;
  // The buffer to read data into.
  std::reference_wrapper<StreamBuffer> m_buffer;
  // Tracks how many bytes this reader has read into the buffer.
  size_t m_bytes_consumed{0};
};

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
  std::atomic<bool> m_should_end_flag{false};

  /// @brief the stream interface for reading / writing
  std::unique_ptr<Stream> m_socket_stream;

  /// @brief listens for messages over stream; reads into m_read_buffer
  std::thread m_listen_thread;

  /// @brief stores the number of bytes available
  std::atomic<size_t> m_bytes_available_in_buffer{0};

  /// @brief signals whether message is ready for processing from buffer
  std::mutex m_buffer_data_mutex;
  std::condition_variable m_buffer_data_cond;

  // Internal listener thread function
  void listener_loop();

  // Helper to create and connect the socket
  socket_t create_and_connect_socket();

  // Helper to set non-blocking mode
  void set_nonblocking(bool nonblocking) const;

  // Helper to wait for socket readiness (for connect, send, recv)
  bool wait_until_ready(time_t sec,
                        time_t usec,
                        bool read_ready,
                        bool write_ready) const;

public:
  UnixSocket() = delete;
  UnixSocket(const std::string& host);
  ~UnixSocket();

  /// @brief the buffer used for reading and writing
  StreamBuffer connection_stream;

  /// @brief generate an encoded file system path to the socket
  std::filesystem::path generate_path() const;

  /// @brief starts the connection over socket
  bool start(bool is_server = false);

  /// @brief ends the connection and terminates listener thread
  void end();

  /// @brief checks whether the socket's path exists
  bool exists() const;

  /// @brief sends data (e.g., a serialized message)
  OpCode send(nonstd::span<const uint8_t> msg);

  /// @brief receives a notification that data is available in the read buffer
  OpCode receive(size_t& bytes_available, bool is_op = true);

private:
  // Helper to establish the socket connection (bind/connect)
  int establish_connection(const std::filesystem::path& path,
                           bool is_server) const;

  /// @brief closes the socket
  int close() const;
};
