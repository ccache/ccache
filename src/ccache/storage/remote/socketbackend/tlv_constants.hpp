#pragma once

#include "ccache/util/bytes.hpp"

#include <nonstd/span.hpp>

#include <cstdint>
#include <cstring>
#include <iterator>
#include <map>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace tlv {
constexpr uint16_t TLV_VERSION = 0x01; // Protocol version
// SETUP-specific types (0x01 - 0x80)
constexpr uint8_t SETUP_TYPE_VERSION = 0x01; // Protocol version
constexpr uint8_t SETUP_TYPE_CONNECT_TIMEOUT =
  0x02; // Timeout in milliseconds (uint32)
constexpr uint8_t SETUP_TYPE_OPERATION_TIMEOUT =
  0x03; // Timeout in milliseconds (uint32)
constexpr uint8_t SETUP_TYPE_BUFFERSIZE = 0x04; // Buffersize

// Application Types (0x81 - 0xFF)
constexpr uint8_t FIELD_TYPE_KEY = 0x81;         // KEY string
constexpr uint8_t FIELD_TYPE_VALUE = 0x82;       // VALUE string
constexpr uint8_t FIELD_TYPE_TIMESTAMP = 0x83;   // Unix timestamp (UINT64)
constexpr uint8_t FIELD_TYPE_STATUS_CODE = 0x84; // Status code
constexpr uint8_t FIELD_TYPE_ERROR_MSG = 0x85;   // Error message (STRING)
constexpr uint8_t FIELD_TYPE_FLAGS = 0x86;       // Flags field

// Message Types
constexpr uint16_t MSG_TYPE_SETUP_REQUEST = 0x01;
constexpr uint16_t MSG_TYPE_GET_REQUEST = 0x02;
constexpr uint16_t MSG_TYPE_PUT_REQUEST = 0x03;
constexpr uint16_t MSG_TYPE_DEL_REQUEST = 0x04;
constexpr uint16_t MSG_TYPE_SETUP_RESPONSE = 0x8001;
constexpr uint16_t MSG_TYPE_GET_RESPONSE = 0x8002;
constexpr uint16_t MSG_TYPE_PUT_RESPONSE = 0x8003;
constexpr uint16_t MSG_TYPE_DEL_RESPONSE = 0x8004;

// NDN Length encoding
// https://docs.named-data.net/NDN-packet-spec/current/tlv.html#variable-size-encoding-for-type-and-length
constexpr uint8_t LENGTH_1_BYTE_MAX = 252;  // 0xFC
constexpr uint8_t LENGTH_3_BYTE_FLAG = 253; // 0xFD
constexpr uint8_t LENGTH_5_BYTE_FLAG = 254; // 0xFE
constexpr uint32_t MAX_FIELD_SIZE = 0xFFFFFFFF;

// Size constants
constexpr uint16_t TLV_HEADER_SIZE = 0x04;
constexpr uint16_t TLV_MAX_FIELD_SIZE = 0xFFFF;

// Flags
constexpr uint8_t OVERWRITE_FLAG = 0x01;

// Status codes
enum ResponseStatus : uint8_t {
  LOCAL_ERROR,
  NO_FILE,
  TIMEOUT,
  SIGWAIT,
  SUCCESS,
  REDIRECT,
  ERROR
};

namespace meta {
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

// Helper for static_assert false in templates
template<class T> struct always_false : std::false_type
{
};

template<uint8_t Tag> struct TagType;

template<> struct TagType<SETUP_TYPE_VERSION>
{
  using type = uint8_t;
};

template<> struct TagType<SETUP_TYPE_CONNECT_TIMEOUT>
{
  using type = uint32_t;
};

template<> struct TagType<SETUP_TYPE_BUFFERSIZE>
{
  using type = uint32_t;
};

template<> struct TagType<FIELD_TYPE_KEY>
{
  using type = util::Bytes;
};

template<> struct TagType<FIELD_TYPE_VALUE>
{
  using type = util::Bytes;
};

template<> struct TagType<FIELD_TYPE_TIMESTAMP>
{
  using type = uint64_t;
};

template<> struct TagType<FIELD_TYPE_STATUS_CODE>
{
  using type = uint8_t;
};

template<> struct TagType<FIELD_TYPE_ERROR_MSG>
{
  using type = std::string;
};

template<> struct TagType<FIELD_TYPE_FLAGS>
{
  using type = uint8_t;
};

// fallback
template<uint8_t Tag> struct TagType
{
  using type = std::vector<uint8_t>; // just raw bytes
};

// TODO take care of error handling after making sure this works right
template<uint8_t Tag>
auto
interpret_data(const uint8_t* pos, uint32_t length)
{
  using DataType = typename TagType<Tag>::type;

  if constexpr (std::is_same_v<DataType, uint8_t>) {
    if (length != sizeof(uint8_t)) {
      throw std::runtime_error("Invalid length for uint8_t");
    }
    return nonstd::span(pos, length);
  } else if constexpr (std::is_same_v<DataType, uint16_t>) {
    if (length != sizeof(uint16_t)) {
      throw std::runtime_error("Invalid length for uint16_t");
    }
    return nonstd::span(pos, length);
  } else if constexpr (std::is_same_v<DataType, uint32_t>) {
    if (length != sizeof(uint32_t)) {
      throw std::runtime_error("Invalid length for uint32_t");
    }
    return nonstd::span(pos, length);
  } else if constexpr (std::is_same_v<DataType, uint64_t>) {
    if (length != sizeof(uint64_t)) {
      throw std::runtime_error("Invalid length for uint64_t");
    }
    return nonstd::span(pos, length);
  } else if constexpr (std::is_same_v<DataType, std::string>) {
    return nonstd::span(pos, length);
  } else if constexpr (std::is_same_v<DataType, util::Bytes>) {
    return nonstd::span(pos, length);
  } else {
    static_assert(always_false<DataType>::value, "Unknown DataType");
  }
}

} // namespace meta

namespace ndn {
inline size_t
encode_length(uint8_t* buffer, uint32_t length)
{
  if (length <= LENGTH_1_BYTE_MAX) {
    buffer[0] = static_cast<uint8_t>(length);
    return 1;
  } else if (length <= 0xFFFF) {
    buffer[0] = LENGTH_3_BYTE_FLAG;
    uint16_t len = static_cast<uint16_t>(length);
    std::memcpy(buffer + 1, &len, 2);
    return 3;
  } else {
    buffer[0] = LENGTH_5_BYTE_FLAG;
    std::memcpy(buffer + 1, &length, 4);
    return 5;
  }
}

// Decode length, returns {length, byte count}
inline std::pair<uint32_t, size_t>
decode_length(const uint8_t* buffer, size_t available)
{
  if (available < 1) {
    return {0, 0};
  }
  uint8_t first_byte = buffer[0];

  if (first_byte <= LENGTH_1_BYTE_MAX) {
    return {first_byte, 1};
  } else if (first_byte == LENGTH_3_BYTE_FLAG) {
    if (available < 3) {
      return {0, 0};
    }
    uint16_t length;
    std::memcpy(&length, buffer + 1, 2);
    return {length, 3};
  } else if (first_byte == LENGTH_5_BYTE_FLAG) {
    if (available < 5) {
      return {0, 0};
    }
    uint32_t length;
    std::memcpy(&length, buffer + 1, 4);
    return {length, 5};
  }

  return {0, 0}; // Invalid encoding
}
} // namespace ndn
} // namespace tlv
