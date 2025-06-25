#pragma once

#include "ccache/storage/remote/remotestorage.hpp"
#include "ccache/util/logging.hpp"
#include "socketinterface.hpp"

#include <ccache/util/bytes.hpp>

#include <nonstd/span.hpp>

#include <sys/types.h>
#include <unistd.h>

#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

constexpr auto MAX_CLIENT_SUPPORTED = 32;

constexpr auto MSG_GET = "1"sv;
constexpr auto MSG_PUT = "2"sv;
constexpr auto MSG_RM = "3"sv;
constexpr auto MSG_TEST = "4"sv;
constexpr auto MSG_SET = "5"sv;

namespace storage::remote::msgr::impl {
constexpr std::array<char, 16> digits = {'0',
                                         '1',
                                         '2',
                                         '3',
                                         '4',
                                         '5',
                                         '6',
                                         '7',
                                         '8',
                                         '9',
                                         'a',
                                         'b',
                                         'c',
                                         'd',
                                         'e',
                                         'f'};

template<typename T, typename = void> struct is_iterable : std::false_type
{
};

template<typename T>
struct is_iterable<T,
                   std::void_t<decltype(std::begin(std::declval<T>())),
                               decltype(std::end(std::declval<T>()))>>
  : std::true_type
{
};

template<typename T>
inline void
serialize(std::string& result, T data)
{
  static_assert(std::is_unsigned_v<T>, "Only unsigned types are supported");
  const size_t byteCount = sizeof(T);
  for (size_t i = 0; i < byteCount; ++i) {
    unsigned char byte =
      static_cast<unsigned char>((data >> (8 * (byteCount - 1 - i))) & 0xFF);
    result.push_back(digits[byte >> 4]);
    result.push_back(digits[byte & 0xF]);
  }
}

template<typename T, typename Iter>
inline T
deserialize(Iter& it, Iter end)
{
  static_assert(std::is_unsigned_v<T>, "T must be an unsigned integer type");
  constexpr long uint_size = sizeof(T);
  T result = 0;

  if (std::distance(it, end) < uint_size * 2) {
    throw std::out_of_range("Not enough characters in string");
  }

  std::string_view view(&(*it), 2 * uint_size);
  if (std::from_chars(view.begin(), view.end(), result, 16).ec != std::errc()) {
    throw std::runtime_error("Invalid hexadecimal number.");
  }

  std::advance(it, 2 * uint_size);
  return result;
}
} // namespace storage::remote::msgr::impl

namespace storage::remote::msgr {

enum ResponseStatus : uint8_t {
  SUCCESS,
  SIGWAIT,
  LOCAL_ERR,
  NO_FILE,
  TIMEOUT,
  REDIRECT,
  ERROR
};

uint8_t PCK_ID = 0;
uint8_t PREV_ACK = 0;

struct Packet
{
  uint8_t MsgType;
  uint8_t FileDescriptor;
  uint8_t MsgID;
  uint8_t Ack;
  uint32_t MsgLength;
  uint32_t Offset;
  std::vector<uint8_t> Body;
  static constexpr auto m_header_size = sizeof(MsgType) + sizeof(FileDescriptor) + sizeof(MsgID) + 
                     sizeof(Ack) + sizeof(MsgLength) + sizeof(Offset);

  /**
   * @brief Serializes the data fields and encodes the combined result into a
   * string.
   *
   * This method processes each field within the object, serializing their byte
   * representations. The serialized bytes from all fields are then combined and
   * encoded (e.g., base64, hexadecimal, or another encoding scheme) to produce
   * a single string.
   *
   * @param result A reference to a std::string that will be populated with the
   * encoded data.
   *
   * Usage:
   * @code
   *   std::string output;
   *   instance.encode(output);
   *   // 'output' now contains the serialized and encoded representation of the
   * object's data
   * @endcode
   *
   * Note: Ensure that the 'result' string is appropriately initialized or empty
   * before calling this method, depending on desired behavior.
   */
  void encode(std::string& result) const;

  /**
   * @brief Deserializes data from the provided string view into the object's
   * fields.
   *
   * This method interprets the serialized data contained in the input string
   * view, extracting and reconstructing the values for each field within the
   * object. It assumes that the input data is correctly formatted according to
   * the serialization scheme used during encoding.
   *
   * @param result An rvalue reference to a `std::string_view` containing the
   * serialized data. The method reads from this view to populate the object's
   * fields.
   *
   * Usage:
   * @code
   *   // Example usage with a string containing serialized data
   *   std::string serialized_data = "...";
   *   instance.decode(std::move(serialized_data));
   *   // 'instance' now contains the deserialized data
   * @endcode
   *
   * Note: Since the parameter is an rvalue reference to a string view, ensure
   * that temporary or movable strings are passed to avoid unintended behavior.
   */
  void decode(const std::string_view& result);

  /**
   * @brief Prints out the packet field's in a readable format.
   */
  void print() const;

  Packet() = default;
  ~Packet() = default;
  Packet(const Packet&) = default;
  Packet& operator=(const Packet&) = default;
  Packet(const std::string_view& data);
  Packet(Packet&& other) noexcept = default;
  Packet& operator=(Packet&& other) noexcept = default;
};

inline Packet::Packet(const std::string_view& data)
{
  decode(data);
}

inline void
Packet::print() const
{
  std::cerr << "<---------->";
  std::cerr << "\n\tHEAD:   " << int(MsgType) << " " << int(FileDescriptor)
            << " " << int(MsgID) << " " << int(Ack);
  std::cerr << "\n\tLength: " << MsgLength;
  std::cerr << "\n\tOffset: " << Offset << "\n";
  std::cerr << "</--------->" << "\n";
}

inline void
Packet::encode(std::string& result) const
{
  result.clear();
  result.reserve(m_header_size * 2 + Body.size());
  impl::serialize(result, MsgType);
  impl::serialize(result, FileDescriptor);
  impl::serialize(result, MsgID);
  impl::serialize(result, Ack);
  impl::serialize(result, MsgLength);
  impl::serialize(result, Offset);
  for (auto octet : Body) {
    impl::serialize(result, octet);
  }
}

inline void
Packet::decode(const std::string_view& result)
{
  auto it = result.cbegin();
  MsgType = impl::deserialize<decltype(MsgType)>(it, result.cend());
  FileDescriptor =
    impl::deserialize<decltype(FileDescriptor)>(it, result.cend());
  MsgID = impl::deserialize<decltype(MsgID)>(it, result.cend());
  Ack = impl::deserialize<decltype(Ack)>(it, result.cend());
  MsgLength = impl::deserialize<decltype(MsgLength)>(it, result.cend());
  Offset = impl::deserialize<decltype(Offset)>(it, result.cend());
  Body.resize(std::distance(it, result.cend()) / 2);
  auto bit = Body.begin();
  while (it != result.cend()) {
    uint8_t octet = impl::deserialize<uint8_t>(it, result.cend());
    *bit = octet;
    bit++;
  }
}

struct MessageHandler
{
  Packet packet;

  /**
   * @brief Creates data packets representing a message of provided type for
   * transmission over socket.
   *
   * @tparam T The type of the key. Must be iterable (e.g., container or array)
   * of bytes.
   * @tparam Args Variadic template for additional arguments; supports up to two
   * arguments:
   *              - A data span (must be iterable of bytes).
   *              - A boolean flag (`only_if_missing` property).
   *
   * @param msgType A string view representing the message type, which is
   * converted to a byte representing the type.
   * @param key The key data, which is copied into the message body. Must be
   * iterable of bytes of length 20.
   * @param args Optional additional arguments:
   *             - If two: first is a data span (iterable of bytes), second is a
   * boolean flag.
   *             - If none: only key data is added.
   *
   */
  template<typename T, typename... Args>
  void create(const std::string_view& msgType, const T& key, Args... args);

  /**
   * @brief Dispatches a sequence of packets over a Unix socket and processes
   * the server's response.
   *
   * This method sends the current packets over the provided `UnixSocket`, then
   * waits for and interprets the server's response. It handles different error
   * conditions such as timeouts and errors, and reconstructs the response data
   * from incoming packets.
   *
   * @param result A reference to a vector where the received data payload will
   * be stored. On success, this will contain the concatenated data payloads
   * from responses.
   * @param sock A reference to the `UnixSocket` used for sending and receiving
   * packets.
   *
   * @return An optional indicating failure reason:
   *         - `std::nullopt` if the operation succeeded without error.
   *         - `RemoteStorage::Backend::Failure` in case of error or timeout.
   *
   * The method performs the following steps:
   * - Sends the current packets over the socket.
   * - Checks the send operation's outcome:
   *   - On error or timeout, clears packets and returns corresponding failure.
   * - Enters a loop to receive packets:
   *   - On timeout, clears packets and result, then returns timeout failure.
   *   - Decodes each received packet and appends its payload to `result`.
   *   - Continues until `packet.Rest` indicates all packets have been received.
   * - Checks the `Ack` status of the final packet:
   *   - If `NO_FILE`, logs and clears data, returning an empty optional.
   *   - If not `SUCCESS`, logs an error and returns appropriate failure.
   * - On success, clears packets and returns no failure (`std::nullopt`).
   *
   */
  std::optional<RemoteStorage::Backend::Failure>
  dispatch(std::vector<uint8_t>& result, UnixSocket& bsock);
};

template<typename T, typename... Args>
inline void
MessageHandler::create(const std::string_view& msgType,
                       const T& key,
                       Args... args)
{
  packet = Packet();
  uint8_t type_enc = 0;
  std::from_chars(msgType.begin(), msgType.end(), type_enc);

  static_assert(impl::is_iterable<decltype(key)>::value,
                "type of key should be iterable");
  packet.Body.insert(packet.Body.end(), std::begin(key), std::end(key));

  if constexpr (sizeof...(args) == 2) { // we have a put call
    auto [dataSpan, flag] = std::forward_as_tuple(args...);
    static_assert(impl::is_iterable<decltype(dataSpan)>::value,
                  "type of data span should be iterable");
    static_assert(std::is_same_v<std::decay_t<decltype(flag)>, bool>,
                  "type of flag should be bool");

    packet.Body.insert(packet.Body.end(), std::begin(dataSpan), std::end(dataSpan));
    packet.Body.push_back(static_cast<uint8_t>(flag));
  }

  packet.MsgType = type_enc;
  packet.FileDescriptor = 0;
  packet.MsgID = PCK_ID;
  packet.Ack = PREV_ACK;
  packet.MsgLength = packet.Body.size();
  PCK_ID = (PCK_ID + 1) % 100;
}

inline std::optional<RemoteStorage::Backend::Failure>
MessageHandler::dispatch(std::vector<uint8_t>& result, UnixSocket& sock)
{
  auto opcode = sock.send(packet);
  if (opcode == OpCode::error) {
    return RemoteStorage::Backend::Failure::error;
  } else if (opcode == OpCode::timeout) {
    return RemoteStorage::Backend::Failure::timeout;
  }

  std::string recv;
  opcode = sock.receive(recv);
  if (opcode == OpCode::timeout) {
    LOG("Client Timeout!", "");
    return RemoteStorage::Backend::Failure::timeout;
  }
  packet = Packet(std::move(recv));

  if (packet.Ack == ResponseStatus::NO_FILE) {
    LOG("Client: File not found on server", "");
    return std::nullopt;
  }

  if (packet.Ack != ResponseStatus::SUCCESS) {
    LOG("Response Status {}: Error Occured With Storage!", int(packet.Ack));
    result.clear();
    return packet.Ack == TIMEOUT ? RemoteStorage::Backend::Failure::timeout
                                 : RemoteStorage::Backend::Failure::error;
  }

  result.insert(result.end(), 
                 std::make_move_iterator(packet.Body.begin()),
                 std::make_move_iterator(packet.Body.end()));
  return std::nullopt;
}

} // namespace storage::remote::msgr
