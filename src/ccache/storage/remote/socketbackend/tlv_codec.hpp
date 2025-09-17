#include "ccache/util/bytes.hpp"
#include "ccache/util/logging.hpp"
#include "ccache/util/socketinterface.hpp"
#include "tlv_constants.hpp"

#include <nonstd/span.hpp>
#include <tl/expected.hpp>

#include <emmintrin.h>
#include <sys/ioctl.h>
#include <sys/types.h>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <utility>

namespace tlv {

struct __attribute__((packed)) MessageHeader
{
  uint8_t version;
  uint8_t num_fields;
  uint16_t msg_type;
};

class TLVParser
{
public:
  struct ParseResult
  {
    MessageHeader header;
    bool success;
    tlv::ResponseStatus status;
    std::vector<UintField> fields; // Pre-allocated
    util::Bytes value;
  };

private:
  ParseResult m_result; // Reused to avoid allocations

  size_t
  decode_length(const uint8_t first_byte)
  {
    switch (first_byte) {
    case LENGTH_3_BYTE_FLAG:
      return 2;
    case LENGTH_5_BYTE_FLAG:
      return 4;
    case LENGTH_9_BYTE_FLAG:
      return 8;
    default:
      // One byte representable
      return 0;
    }
  }

  bool
  read_uint_field(uint8_t tag,
                  uint32_t length,
                  std::unique_ptr<BufferedStreamReader>& reader) noexcept
  {
    std::array<uint8_t, 8> payload;
    auto res = reader->read_exactly(length, payload);
    if (!res.has_value()) {
      return false;
    }

    switch (tag) {
    case SETUP_TYPE_VERSION:
      m_result.fields.emplace_back(
        make_tlv_field<SETUP_TYPE_VERSION>(tag, payload.data(), length));
      break;
    case SETUP_TYPE_OPERATION_TIMEOUT:
      m_result.fields.emplace_back(make_tlv_field<SETUP_TYPE_OPERATION_TIMEOUT>(
        tag, payload.data(), length));
      break;
    case SETUP_TYPE_BUFFERSIZE:
      m_result.fields.emplace_back(
        make_tlv_field<SETUP_TYPE_BUFFERSIZE>(tag, payload.data(), length));
      break;
    case FIELD_TYPE_TIMESTAMP:
      m_result.fields.emplace_back(
        make_tlv_field<FIELD_TYPE_TIMESTAMP>(tag, payload.data(), length));
      break;
    case FIELD_TYPE_FLAGS:
      m_result.fields.emplace_back(
        make_tlv_field<FIELD_TYPE_FLAGS>(tag, payload.data(), length));
      break;
    case FIELD_TYPE_STATUS_CODE:
      m_result.fields.emplace_back(
        make_tlv_field<FIELD_TYPE_STATUS_CODE>(tag, payload.data(), length));
      break;
    }
    return true;
  }

  bool
  read_value_field(uint32_t length,
                   std::unique_ptr<BufferedStreamReader>& reader)
  {
    m_result.value.resize(length);
    auto res = reader->read_exactly(length, m_result.value);
    return res.has_value();
  }

  bool
  read_error_message(uint32_t length,
                     std::unique_ptr<BufferedStreamReader>& reader)
  {
    // Error message uses same result field as value
    m_result.value.reserve(length);
    auto res = reader->read_exactly(length, m_result.value);
    return res.has_value();
  }

  bool
  handle_unknown_tag(uint8_t tag, uint32_t length) noexcept
  {
    LOG("Unavailable data type for tag={} length={}", tag, length);
    return false;
  }

  bool
  parse_value(uint8_t tag,
              uint32_t length,
              std::unique_ptr<BufferedStreamReader>& reader)
  {
    if (length == 0) {
      return false;
    }
    // compiler can optimize this into jump table
    switch (tag) {
    case SETUP_TYPE_VERSION:
    case SETUP_TYPE_OPERATION_TIMEOUT:
    case SETUP_TYPE_BUFFERSIZE:
    case FIELD_TYPE_TIMESTAMP:
    case FIELD_TYPE_FLAGS:
    case FIELD_TYPE_STATUS_CODE:
      return read_uint_field(tag, length, reader);
    case FIELD_TYPE_KEY:
      // TODO jump over the key length
      return true; // Ignore key when parsing from reader
    case FIELD_TYPE_VALUE:
      return read_value_field(length, reader);
    case FIELD_TYPE_ERROR_MSG:
      return read_error_message(length, reader);
    default:
      return handle_unknown_tag(tag, length);
    }
  }

  bool
  parse_header(std::unique_ptr<BufferedStreamReader>& reader)
  {
    std::array<uint8_t, sizeof(MessageHeader)> hdr_stream;
    auto res = reader->read_exactly(sizeof(MessageHeader), hdr_stream);
    if (!res.has_value()) {
      LOG("Parser: Reading Header {}",
          (res.error() == OpError::error) ? "ERROR" : "TIMEOUT");
      return false;
    }

    std::memcpy(&m_result.header, hdr_stream.data(), sizeof(MessageHeader));
    return true;
  }

public:
  TLVParser()
  {
    m_result.fields.reserve(4); // Pre-allocate for common case
  }

  ParseResult&
  parse(std::unique_ptr<BufferedStreamReader>& reader)
  {
    // parse_header may modify the m_result to add the version, type.
    // Thus, even if they fail, the m_result object would contain
    // information about possible issues.
    m_result.success = true;
    m_result.status = SUCCESS;

    if (!parse_header(reader)) {
      m_result.status = ERROR;
      return m_result;
    }

    // Create a reusable temporary buffer for the data. Max possible length
    // encoding is 8 bytes
    //
    // The loop first reads two bytes (tag and length). It decodes length first.
    // In case the length is 1 byte represantable it proceeds to decode the
    // value. Otherwise, first read the actual length of value (multiple bytes)
    // and then parse the value!
    std::array<uint8_t, 8> tmp_buffer;
    for (uint8_t i = 0; i < m_result.header.num_fields; i++) {
      auto res = reader->read_exactly(2, tmp_buffer);
      uint8_t tag = tmp_buffer[0];
      size_t val_len = tmp_buffer[1];
      if (!res.has_value()) {
        m_result.status = ERROR;
        break;
      }

      // read actual value length
      auto length_encoding = decode_length(val_len);
      if (length_encoding) {
        if (!reader->read_exactly(length_encoding, tmp_buffer).has_value()) {
          m_result.status = ERROR;
          break;
        }
        std::memcpy(&val_len, tmp_buffer.data(), length_encoding);
      }

      if (!parse_value(tag, val_len, reader)) {
        m_result.status = ERROR;
        break;
      }
    }

    return m_result;
  }
};

class TLVSerializer
{
private:
  std::size_t m_position{0};
  std::reference_wrapper<StreamBuffer<uint8_t>> m_buffer;

  size_t
  encode_length(uint32_t length)
  {
    if (length <= LENGTH_1_BYTE_MAX) {
      m_buffer.get().write(&length, sizeof(uint8_t));
      return 1;
    } else if (length <= 0xFFFF) {
      m_buffer.get().write(&LENGTH_3_BYTE_FLAG, sizeof(uint8_t));
      m_buffer.get().write(&length, sizeof(uint16_t));
      return 3;
    } else {
      m_buffer.get().write(&LENGTH_5_BYTE_FLAG, sizeof(uint8_t));
      m_buffer.get().write(&length, sizeof(uint32_t));
      return 5;
    }
  }

  bool
  begin_message(const MessageHeader& msghdr)
  {
    m_position = TLV_HEADER_SIZE; // position past header
    m_buffer.get().write(&msghdr, sizeof(MessageHeader));
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
    m_buffer.get().write(&tag, sizeof(uint8_t));
    m_position++;

    // Write variable length
    m_position += encode_length(length);

    // Write value
    m_buffer.get().write(data, length);
    m_position += length;
    return true;
  }

  std::pair<uint8_t*, size_t>
  finalize() const
  {
    if (m_position == 0) {
      return {nullptr, 0};
    }
    return {m_buffer.get().data(), m_position};
  }

public:
  TLVSerializer(StreamBuffer<uint8_t>& stream) 
  : m_buffer(stream)
  {
  } 
  TLVSerializer() = delete;
  TLVSerializer(const TLVSerializer&) = delete;
  TLVSerializer& operator=(const TLVSerializer&) = delete;
  TLVSerializer(TLVSerializer&&) = delete;
  TLVSerializer& operator=(TLVSerializer&&) = delete;
  ~TLVSerializer() = default;

  void
  release()
  {
    m_buffer.get().release();
    m_position = 0;
  }

  size_t
  size() const noexcept
  {
    return m_position;
  }

  template<typename... Args>
  std::pair<uint8_t*, size_t>
  serialize(const int& msg_tag, Args&&... args)
  {
    m_buffer.get().release();
    begin_message({TLV_VERSION, static_cast<uint16_t>(msg_tag)});

    auto serialise_fields =
      [this](auto&& self, auto&& first, auto&& second, auto&&... rest) -> void {
      this->addfield(std::forward<decltype(first)>(first),
               std::forward<decltype(second)>(second));
      if constexpr (sizeof...(rest) > 0) {
        self(self, std::forward<decltype(rest)>(rest)...);
      }
    };

    if constexpr (sizeof...(args) > 0) {
      serialise_fields(serialise_fields, std::forward<Args>(args)...);
    }

    return finalize();
  }
};

} // namespace tlv
