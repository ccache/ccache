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
#include <variant>
#include <vector>

namespace tlv {
// Current protocol version
constexpr uint16_t TLV_VERSION = 0x01;

// SETUP-specific types (0x01 - 0x80)
constexpr uint8_t SETUP_TYPE_VERSION = 0x01;
constexpr uint8_t SETUP_TYPE_OPERATION_TIMEOUT = 0x02;
constexpr uint8_t SETUP_TYPE_BUFFERSIZE = 0x03;

// Application Types (0x81 - 0xFF)
constexpr uint8_t FIELD_TYPE_KEY = 0x81;
constexpr uint8_t FIELD_TYPE_VALUE = 0x82;
constexpr uint8_t FIELD_TYPE_TIMESTAMP = 0x83;
constexpr uint8_t FIELD_TYPE_STATUS_CODE = 0x84;
constexpr uint8_t FIELD_TYPE_ERROR_MSG = 0x85;
constexpr uint8_t FIELD_TYPE_FLAGS = 0x86;

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
constexpr uint8_t LENGTH_9_BYTE_FLAG = 255; // 0xFE

// Size constants
constexpr uint16_t TLV_HEADER_SIZE = 0x04;
constexpr uint16_t TLV_MAX_FIELD_SIZE = 0xFFFF;
constexpr uint32_t MAX_MSG_SIZE = 0xFFFFFFFF;
constexpr uint32_t DEFAULT_ALLOC = 1024;

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

template<> struct TagType<SETUP_TYPE_OPERATION_TIMEOUT>
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

} // namespace meta

template<typename T> struct UintFieldRef
{
  static_assert(std::is_arithmetic_v<T>,
                "TLVFieldRef only supports arithmetic types");

  uint8_t tag;
  T value;

  UintFieldRef(uint8_t t, T val)
    : tag(t),
      value(val)
  {
  }
};

using UintField = std::variant<UintFieldRef<uint8_t>,
                              UintFieldRef<uint16_t>,
                              UintFieldRef<uint32_t>,
                              UintFieldRef<uint64_t>>;

template<uint8_t Tag>
inline UintFieldRef<typename meta::TagType<Tag>::type>*
get_field(std::vector<UintField>& fields)
{
  using DataType = typename meta::TagType<Tag>::type;
  for (auto& field : fields) {
    if (auto* ref = std::get_if<UintFieldRef<DataType>>(&field)) {
      if (ref->tag == Tag) { // Use template parameter
        return ref;
      }
    }
  }
  return nullptr;
}

// Factory function
template<uint8_t Tag>
UintField
make_tlv_field(uint8_t tag, const uint8_t* ptr, uint32_t len)
{
  using DataType = typename meta::TagType<Tag>::type;
  static_assert(std::is_arithmetic_v<DataType>, "DataType must be arithmetic");

  if (len != sizeof(DataType)) {
    throw std::runtime_error("Length mismatch: expected "
                             + std::to_string(sizeof(DataType)) + " bytes, got "
                             + std::to_string(len));
  }

  DataType val;
  std::memcpy(&val, ptr, sizeof(DataType));
  return UintFieldRef<DataType>(tag, val);
}
} // namespace tlv
