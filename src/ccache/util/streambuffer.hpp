#pragma once

#include "ccache/storage/remote/socketbackend/tlv_constants.hpp"

#include <nonstd/span.hpp>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <vector>

namespace tlv {

template<typename T> class StreamBuffer
{
public:
  StreamBuffer()
    : m_buffer(DEFAULT_ALLOC)
  {
  }
  StreamBuffer(const StreamBuffer&) = default;
  StreamBuffer& operator=(const StreamBuffer&) = default;
  StreamBuffer(StreamBuffer&&) = default;
  StreamBuffer& operator=(StreamBuffer&&) = default;
  ~StreamBuffer() = default;

  /// Access buffer data
  T*
  data()
  {
    return m_buffer.data();
  }

  // Const access to buffer data
  const T*
  data() const
  {
    return m_buffer.data();
  }

  /// Get buffer capacity before needing to allocate more memory.
  size_t
  capacity() const
  {
    return m_buffer.capacity();
  }

  bool
  empty() const
  {
    return m_size == 0;
  }

  void
  clear()
  {
    m_size = 0;
  }

  /// Releases (clears) buffer and default allocates.
  void
  release()
  {
    m_buffer.clear();
    m_size = 0;
    m_buffer.reserve(DEFAULT_ALLOC);
    m_buffer.resize(DEFAULT_ALLOC);
  }

  /// Returns the number of elements in the buffer
  size_t
  size() const
  {
    return m_size;
  }

  // Writes `n` bytes from `src` into the buffer.
  bool
  write(const void* src, size_t n)
  {
    if (src == nullptr || n + m_size > MAX_MSG_SIZE || n == 0) {
      return false;
    }

    if (!ensure_capacity(m_size + n)) {
      std::cerr << "StreamBuffer::write: Error - failed to ensure capacity\n";
      return false;
    }

    std::memcpy(m_buffer.data() + m_size, src, n);
    m_size += n;
    return true;
  }

  // After writing into the span, this commits the number of elements written
  bool
  commit(size_t n)
  {
    if (n > MAX_MSG_SIZE) {
      std::cerr << "StreamBuffer::commit: Committing more than MAX_MSG_SIZE\n";
      return false;
    }
    if (m_size + n > capacity()) {
      std::cerr << "StreamBuffer::commit: Committing beyond buffer capacity\n";
      return false;
    }
    m_size += n;
    return true;
  }

  // Prepares a writable span of `n` items in the buffer
  //
  // Note: The vector's `size()` might still be less than `m_msize + n` if
  // only `reserve` was used. This is why `commit_write` or a later `resize`
  // is important.
  nonstd::span<T>
  prepare(size_t n)
  {
    if (m_size + n > MAX_MSG_SIZE || n == 0) {
      std::cerr << "StreamBuffer::prepare: Warning. n=" << n
                << ".\n";
      return {};
    }

    if (!ensure_capacity(m_size + n)) {
      std::cerr << "StreamBuffer::prepare: Error - failed to ensure capacity\n";
      return {};
    }

    // Return a span to the newly available space.
    return {m_buffer.data() + m_size, n};
  }

  nonstd::span<const T>
  view() const
  {
    return {m_buffer.data(), m_size};
  }

  nonstd::span<T>
  view()
  {
    return {m_buffer.data(), m_size};
  }

private:
  /// Resize buffer to preallocate a buffer of n elements.
  void
  resize(size_t n)
  {
    m_buffer.resize(n);
  }

  // Ensures the buffer has at least `required_capacity` bytes of
  bool
  ensure_capacity(size_t required_capacity)
  {
    if (required_capacity > m_buffer.capacity()) {
      // Calculate new capacity: 1.5x growth factor, or required_capacity if
      // it's larger. Guard against potential overflow for very large
      // capacities.
      size_t new_capacity = required_capacity;
      if (m_buffer.capacity() > 0) {
        new_capacity = std::max(required_capacity,
                                static_cast<size_t>(m_buffer.capacity() * 1.5));
      }

      try {
        m_buffer.reserve(new_capacity);
        m_buffer.resize(new_capacity);
      } catch (const std::bad_alloc& e) {
        std::cerr << "StreamBuffer error: Failed to reserve capacity: "
                  << e.what() << "\n";
        return false;
      }
    }
    return true;
  }

  std::vector<T> m_buffer;
  size_t m_size{0};
};

} // namespace tlv
