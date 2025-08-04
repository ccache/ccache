#pragma once

#include "ccache/storage/remote/socketbackend/tlv_constants.hpp"
#include <vector>
#include <mutex>

namespace tlv {

template<typename T>
class BigBuffer {
public:
    /// Returns the singleton instance
    static BigBuffer& getInstance() {
        static BigBuffer instance;
        return instance;
    }

    BigBuffer(const BigBuffer&) = delete;
    BigBuffer& operator=(const BigBuffer&) = delete;
    BigBuffer(BigBuffer&&) = delete;
    BigBuffer& operator=(BigBuffer&&) = delete;

    /// Access buffer data
    T* data() {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_buffer.data();
    }

    /// Get buffer capacity before needing to allocate more memory.
    size_t capacity() const {
        return m_buffer.capacity();
    }

    /// Resize buffer to preallocate a buffer of n elements.
    void resize(size_t n) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (n > m_buffer.capacity()) {
            m_buffer.reserve(n);
        }
        m_buffer.resize(n);
    }

    /// Releases (clears) buffer and default allocates.
    void release() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffer.clear();
        resize(TLV_MAX_FIELD_SIZE);
    }

    /// Returns the number of elements in the buffer
    size_t size() const {
        return m_buffer.size();
    }

private:
    BigBuffer() {
        /// initial capacity is TLV_MAX_FIELD_SIZE
        m_buffer.reserve(TLV_MAX_FIELD_SIZE);
    }

    std::vector<T> m_buffer;
    mutable std::mutex m_mutex;
};

BigBuffer<uint8_t>& tlv_buffer = BigBuffer<uint8_t>::getInstance();

}
