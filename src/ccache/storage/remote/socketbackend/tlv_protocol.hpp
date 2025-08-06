#include "ccache/util/assertions.hpp"
#include "ccache/util/socketinterface.hpp"
#include "tlv_buffer.hpp"
#include "tlv_constants.hpp"

#include <nonstd/span.hpp>
#include <tl/expected.hpp>

#include <emmintrin.h>
#include <sys/types.h>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace tlv {

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

  TLVFieldRef(uint8_t t, uint64_t len, nonstd::span<const uint8_t> data)
    : tag(t),
      length(len),
      data(data)
  {
    ASSERT(len == data.size());
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

  std::pair<uint32_t, size_t>
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
      std::string err_str = "Unavailable data type for " + std::to_string(tag)
                            + " " + std::to_string(length);
      throw std::runtime_error(err_str);
    }
  }

public:
  TLVParser()
  {
    m_result.fields.reserve(4); // Pre-allocate for common case
  }

  ParseResult&
  parse(nonstd::span<uint8_t> data)
  {
    m_result.fields.clear();
    m_result.success = false;

    if (data.size() < TLV_HEADER_SIZE) { // ok..
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
    const uint8_t* end = data.data() + data.size();

    while (pos != end) {
      // parse tag
      uint8_t p_tag;
      std::memcpy(&p_tag, pos, 1);
      pos++;

      auto [field_length, length_bytes] = decode_length(pos, end - pos);
      if (length_bytes == 0) {
        break; // Invalid length encoding
      }
      pos += length_bytes;

      if (pos + field_length > end) {
        break;
      }
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
  std::size_t m_position{0};

  size_t
  encode_length(uint32_t length)
  {
    if (length <= LENGTH_1_BYTE_MAX) {
      g_write_buffer.write(&length, sizeof(uint8_t));
      return 1;
    } else if (length <= 0xFFFF) {
      g_write_buffer.write(&LENGTH_3_BYTE_FLAG, sizeof(uint8_t));
      g_write_buffer.write(&length, sizeof(uint16_t));
      return 3;
    } else {
      g_write_buffer.write(&LENGTH_5_BYTE_FLAG, sizeof(uint8_t));
      g_write_buffer.write(&length, sizeof(uint32_t));
      return 5;
    }
  }

public:
  TLVSerializer() = default;

  bool
  begin_message(const MessageHeader& msghdr)
  {
    m_position = TLV_HEADER_SIZE; // position past header
    g_write_buffer.write(&msghdr, sizeof(MessageHeader));
    return true;
  }

  template<typename T>
  std::enable_if_t<std::is_integral_v<T>, bool>
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
  addfield(uint16_t tag, const nonstd::span<const uint8_t>& value)
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

    if ((m_position + needed) > MAX_MSG_SIZE) {
      return false;
    }

    // Write tag
    g_write_buffer.write(&tag, sizeof(uint8_t));
    m_position++;

    // Write variable length
    m_position += encode_length(length);

    // Write value
    g_write_buffer.write(data, length);
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
    if (m_position == 0) {
      return {nullptr, 0};
    }
    return {g_write_buffer.data(), m_position};
  }

  void
  release()
  {
    g_write_buffer.release();
    m_position = 0;
  }
};

template<typename... Args>
inline tl::expected<TLVParser::ParseResult, ResponseStatus>
dispatch(TLVParser& parser,
         UnixSocket& sock,
         const int& msg_tag,
         Args&&... args)
{
  static_assert(sizeof...(args) % 2 == 0,
                "Arguments must come in pairs: tag, value, tag, value, ...");
  TLVSerializer serializer;

  g_read_buffer.release();
  serializer.begin_message({TLV_VERSION, static_cast<uint16_t>(msg_tag)});

  auto serialise_fields = [&serializer](auto&& self,
                                        auto&& first,
                                        auto&& second,
                                        auto&&... rest) -> void {
    serializer.addfield(std::forward<decltype(first)>(first),
                        std::forward<decltype(second)>(second));
    if constexpr (sizeof...(rest) > 0) {
      self(self, std::forward<decltype(rest)>(rest)...);
    }
  };

  if constexpr (sizeof...(args) > 0) {
    serialise_fields(serialise_fields, std::forward<Args>(args)...);
  }

  auto [data, length] = serializer.finalize();

  auto opcode = sock.send({data, data + length});
  if (opcode == OpCode::error) {
    return tl::unexpected<ResponseStatus>(ERROR);
  } else if (opcode == OpCode::timeout) {
    return tl::unexpected<ResponseStatus>(TIMEOUT);
  }

  serializer.release();
  size_t received_size;
  opcode = sock.receive(received_size);
  if (opcode == OpCode::error) {
    return tl::unexpected<ResponseStatus>(ERROR);
  } else if (opcode == OpCode::timeout) {
    return tl::unexpected<ResponseStatus>(TIMEOUT);
  }

  auto& res = parser.parse({g_read_buffer.data(), received_size});
  if (!res.success) {
    return tl::unexpected<ResponseStatus>(ERROR);
  }

  ResponseStatus status_code;
  auto errcode_field = getfield(res.fields, FIELD_TYPE_STATUS_CODE);
  std::memcpy(&status_code, errcode_field->data.data(), errcode_field->length);

  if (status_code != SUCCESS) {
    // TODO LOG the error message?
    return tl::unexpected<ResponseStatus>(status_code);
  }

  return res;
}

} // namespace tlv
