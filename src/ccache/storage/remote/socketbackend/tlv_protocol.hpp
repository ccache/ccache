#include "ccache/util/assertions.hpp"
#include "ccache/util/bytes.hpp"
#include "ccache/util/socketinterface.hpp"
#include "tlv_buffer.hpp"
#include "tlv_constants.hpp"

#include <nonstd/span.hpp>

#include <bits/floatn-common.h>
#include <emmintrin.h>
#include <sys/types.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tlv {

enum ResponseStatus : uint8_t { SUCCESS, NO_FILE, TIMEOUT, ERROR };

struct __attribute__((packed)) MessageHeader
{
  uint16_t version;
  uint16_t msg_type;
};

struct TLVFieldRef
{
  uint8_t tag;
  uint64_t length;
  nonstd::span<const uint8_t> data; // View into original buffer

  TLVFieldRef(uint8_t t, uint32_t len, const uint8_t* ptr)
    : tag(t),
      length(len),
      data(ptr, len)
  {
  }

  TLVFieldRef(uint8_t t, uint64_t len, std::string_view str)
    : tag(t),
      length(len),
      data(reinterpret_cast<const uint8_t*>(str.data()), str.size())
  {
    ASSERT(len == str.size());
  }

  TLVFieldRef(uint8_t t, uint64_t len, nonstd::span<const uint8_t> d)
    : tag(t),
      length(len),
      data(d)
  {
    ASSERT(len == d.size());
  }

  // Template constructor for integral or trivially copyable types
  template<typename T,
           typename = std::enable_if_t<std::is_trivially_copyable_v<T>>>
  TLVFieldRef(uint8_t t, uint64_t len, const T& value)
    : tag(t),
      length(len),
      data(reinterpret_cast<const uint8_t*>(&value), sizeof(T))
  {
    ASSERT(len == sizeof(T));
  }
};

class TLVParser
{
public:
  struct ParseResult
  {
    uint16_t version;
    uint16_t msg_type;
    std::vector<TLVFieldRef> fields; // Pre-allocated
    bool success;
  };

private:
  ParseResult m_result; // Reused to avoid allocations

  void
  parse_value(uint8_t tag, uint32_t length, const uint8_t* pos)
  {
    switch (tag) {
    case SETUP_TYPE_VERSION:
      m_result.fields.emplace_back(
        tag, length, meta::interpret_data<SETUP_TYPE_VERSION>(pos, length));
      return;
    case SETUP_TYPE_CONNECT_TIMEOUT:
      m_result.fields.emplace_back(
        tag,
        length,
        meta::interpret_data<SETUP_TYPE_CONNECT_TIMEOUT>(pos, length));
      return;
    case SETUP_TYPE_BUFFERSIZE:
      m_result.fields.emplace_back(
        tag, length, meta::interpret_data<SETUP_TYPE_BUFFERSIZE>(pos, length));
      return;
    case FIELD_TYPE_KEY:
      m_result.fields.emplace_back(
        tag, length, meta::interpret_data<FIELD_TYPE_KEY>(pos, length));
      return;
    case FIELD_TYPE_VALUE:
      m_result.fields.emplace_back(
        tag, length, meta::interpret_data<FIELD_TYPE_VALUE>(pos, length));
      return;
    case FIELD_TYPE_TIMESTAMP:
      m_result.fields.emplace_back(
        tag, length, meta::interpret_data<FIELD_TYPE_TIMESTAMP>(pos, length));
      return;
    case FIELD_TYPE_STATUS_CODE:
      m_result.fields.emplace_back(
        tag, length, meta::interpret_data<FIELD_TYPE_STATUS_CODE>(pos, length));
      return;
    case FIELD_TYPE_ERROR_MSG:
      m_result.fields.emplace_back(
        tag, length, meta::interpret_data<FIELD_TYPE_ERROR_MSG>(pos, length));
      return;
    case FIELD_TYPE_FLAGS:
      m_result.fields.emplace_back(
        tag, length, meta::interpret_data<FIELD_TYPE_FLAGS>(pos, length));
      return;
    default:
      throw std::runtime_error("Unavailable data type");
    }
  }

public:
  TLVParser()
  {
    m_result.fields.reserve(4); // Pre-allocate for common case
  }

  ParseResult&
  parse(util::Bytes data, size_t length)
  {
    m_result.fields.clear();
    m_result.success = false;

    if (length < TLV_HEADER_SIZE) { // ok..
      return m_result;
    }

    const uint8_t* pos = data.data();

    // Parse header
    MessageHeader msghdr;
    std::memcpy(&msghdr, pos, sizeof(MessageHeader));
    m_result.version = msghdr.version;
    // TODO checks on the version
    m_result.msg_type = msghdr.msg_type;
    pos += TLV_HEADER_SIZE;
    const uint8_t* end = data.data() + length;
    std::cerr << length << " here is length\n";

    while (pos + 2 <= end) {
      // parse tag
      std::cerr << int(data[0]) << " printed data[0]\n";
      uint8_t p_tag;
      std::memcpy(&p_tag, pos, 1);
      pos++;

      std::cerr << int(data[1]) << " printed data[1]\n";
      auto [field_length, length_bytes] = ndn::decode_length(pos, end - pos);
      if (length_bytes == 0) {
        break; // Invalid length encoding
      }
      pos += length_bytes;

      if (pos + field_length > end) {
        break;
      }
      std::cerr << int(p_tag) << " printed tag\n";
      // parse value and create field
      parse_value(p_tag, field_length, pos);
      pos += field_length;
    }

    m_result.success = (pos == end);
    std::cerr << "did we parse well? " << (m_result.success ? "yes" : "no")
              << "\n";
    return m_result;
  }
};

class TLVSerializer
{
private:
  uint8_t* m_buffer = nullptr;
  std::size_t m_capacity{};
  std::size_t m_position{};

public:
  TLVSerializer() = default;

  bool
  begin_message(const MessageHeader& msghdr)
  {
    m_buffer = tlv_buffer.data();
    if (!m_buffer) {
      return false;
    }

    m_capacity = tlv_buffer.capacity();
    m_position = TLV_HEADER_SIZE; // position past header

    std::memcpy(m_buffer, &msghdr, sizeof(MessageHeader));
    return true;
  }

  template<typename T>
  bool
  addfield(uint16_t tag, const T& value)
  {
    return addfield_raw(tag, &value, sizeof(T));
  }

  bool
  addfield(uint16_t tag, const std::string& value)
  {
    return addfield_raw(tag, value.data(), value.length());
  }

  bool
  addfield(uint16_t tag, const util::Bytes& value)
  {
    return addfield_raw(tag, value.data(), value.size());
  }

  bool
  addfield_raw(uint8_t tag, const void* data, uint32_t length)
  {
    // Calculate space needed: 1 byte tag + variable length + data
    size_t length_encoding_size = (length <= LENGTH_1_BYTE_MAX) ? 1
                                  : (length <= 0xFFFF)          ? 3
                                                                : 5;
    size_t needed = 1 + length_encoding_size + length;

    if (auto newsize = (m_position + needed); newsize > m_capacity) {
      // TODO allocate more first if it fails return false
      if (newsize > sizeof(size_t)) {
        return false;
      }
      tlv_buffer.resize(newsize * 2);
    }

    // Write tag
    std::memcpy(m_buffer + m_position, &tag, 1);
    m_position++;

    // Write variable length
    m_position += ndn::encode_length(m_buffer + m_position, length);

    // Write value
    std::memcpy(m_buffer + m_position, data, length);
    m_position += length;
    return true;
  }

  size_t
  size() const noexcept
  {
    return m_position;
  }

  std::pair<uint8_t*, size_t>
  finalize() const
  {
    if (!m_buffer) {
      return {nullptr, 0};
    }
    return {m_buffer, m_position};
  }

  void
  release()
  {
    if (m_buffer) {
      tlv_buffer.release();
      m_buffer = nullptr;
      m_capacity = 0;
      m_position = 0;
    }
  }
};

TLVFieldRef*
getfield(std::vector<TLVFieldRef>& fields, uint16_t target_tag)
{
  if (fields.size() < 8) {
    for (auto& field : fields) {
      if (field.tag == target_tag) {
        return &field;
      }
    }
    return nullptr;
  }

  // SIMD search for larger field counts
  __m128i target = _mm_set1_epi16(target_tag);

  for (size_t i = 0; i + 8 <= fields.size(); i += 8) {
    __m128i tags =
      _mm_loadu_si128(reinterpret_cast<const __m128i*>(&fields[i].tag));

    __m128i cmp = _mm_cmpeq_epi16(tags, target);
    int mask = _mm_movemask_epi8(cmp);

    if (mask) {
      int pos = __builtin_ctz(mask) / 2; // Convert byte mask to element
      return &fields[i + pos];
    }
  }

  for (size_t i = fields.size() & ~7; i < fields.size(); ++i) {
    if (fields[i].tag == target_tag) {
      return &fields[i];
    }
  }

  return nullptr;
}

template<typename T, typename... Args>
inline ResponseStatus
dispatch(std::vector<uint8_t>& result,
         UnixSocket& sock,
         const int& msg_tag,
         const T& key,
         Args... args)
{
  TLVSerializer serializer;
  // pass message type to the header
  serializer.begin_message({0x01, static_cast<uint16_t>(msg_tag)});

  // process data into tlv binary to dispatch
  static_assert(meta::is_iterable<decltype(key)>::value,
                "type of key should be iterable");
  serializer.addfield(FIELD_TYPE_KEY, key);

  if constexpr (sizeof...(args) == 2) { // we have a put call
    auto [data_span, flag] = std::forward_as_tuple(args...);
    static_assert(meta::is_iterable<decltype(data_span)>::value,
                  "type of data span should be iterable");
    static_assert(std::is_same_v<std::decay_t<decltype(flag)>, bool>,
                  "type of flag should be bool");

    serializer.addfield(FIELD_TYPE_VALUE, data_span);
    serializer.addfield(FIELD_TYPE_FLAGS, flag);
  }

  auto [data, length] = serializer.finalize();

  auto opcode = sock.send({data, data + length});
  if (opcode == OpCode::error) {
    return ERROR;
  } else if (opcode == OpCode::timeout) {
    return TIMEOUT;
  }

  std::string received_bytes;
  opcode = sock.receive(received_bytes);
  if (opcode == OpCode::timeout) {
    return TIMEOUT;
  }

  TLVParser parser;
  auto& res = parser.parse({received_bytes.data(), received_bytes.size()},
                           received_bytes.size());
  if (!res.success) {
    return ERROR;
  }

  ResponseStatus status_code;
  auto errcode_field = getfield(res.fields, FIELD_TYPE_STATUS_CODE);
  std::memcpy(&status_code, errcode_field->data.data(), errcode_field->length);
  std::cerr << "status=" << int(status_code) << "\n";
  if (status_code != SUCCESS) {
    // TODO LOG the error message?
    return status_code;
  }

  const TLVFieldRef* val_field = getfield(res.fields, FIELD_TYPE_VALUE);
  result.insert(result.end(), val_field->data.begin(), val_field->data.end());
  return SUCCESS;
}

} // namespace tlv
