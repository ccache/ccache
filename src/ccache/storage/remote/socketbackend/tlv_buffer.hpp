#pragma once

#include "ccache/storage/remote/socketbackend/tlv_constants.hpp"

#include <cstddef>
#include <cstring>
#include <vector>

namespace tlv {

template<typename T> class BigBuffer
{
public:
  /// Returns a singleton instance for reading
  static BigBuffer&
  readInstance()
  {
    static BigBuffer readBuf;
    return readBuf;
  }

  /// Returns a singleton instance for writing
  static BigBuffer&
  writeInstance()
  {
    static BigBuffer writeBuf;
    return writeBuf;
  }

  BigBuffer(const BigBuffer&) = delete;
  BigBuffer& operator=(const BigBuffer&) = delete;
  BigBuffer(BigBuffer&&) = delete;
  BigBuffer& operator=(BigBuffer&&) = delete;

  /// Access buffer data
  T*
  data()
  {
    return m_buffer.data();
  }

  /// Get buffer capacity before needing to allocate more memory.
  size_t
  capacity() const
  {
    return m_buffer.capacity();
  }

  /// Releases (clears) buffer and default allocates.
  void
  release()
  {
    m_buffer.clear();
    m_size = 0;
    m_buffer.reserve(DEFAULT_ALLOC);
  }

  /// Returns the number of elements in the buffer
  size_t
  size() const
  {
    return m_size;
  }

  bool
  write(const void *src, size_t n)
  {
    if (n + m_size > MAX_MSG_SIZE) {
      return false;
    } else if (n + m_size > m_buffer.size()) {
      resize(1.5 * (m_size + n));
    }

    std::memcpy(m_buffer.data() + m_size, src, n);
    m_size += n;
    return true;
  }

private:
  BigBuffer()
  {
    m_buffer.reserve(DEFAULT_ALLOC);
  }

  /// Resize buffer to preallocate a buffer of n elements.
  void
  resize(size_t n)
  {
    if (n > m_buffer.capacity()) {
      m_buffer.reserve(n);
    }
    m_buffer.resize(n);
  }

  std::vector<T> m_buffer;
  size_t m_size{0};
};

inline BigBuffer<uint8_t>& g_write_buffer = BigBuffer<uint8_t>::writeInstance();
inline BigBuffer<uint8_t>& g_read_buffer = BigBuffer<uint8_t>::readInstance();

} // namespace tlv
