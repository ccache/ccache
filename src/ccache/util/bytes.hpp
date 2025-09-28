// Copyright (C) 2022-2024 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#pragma once

#include <nonstd/span.hpp>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <memory>

namespace util {

// This class represents a contiguous array of bytes.
//
// The primary motivation for this class instead of just using
// std::vector<uint8_t> is to make zero copying without zero-filling possible
// when retrieving data from syscalls like read(2), i.e, when
// std::vector::insert cannot be used.
class Bytes
{
public:
  Bytes() noexcept = default;
  explicit Bytes(size_t size) noexcept;

  Bytes(const void* data, size_t size) noexcept;
  Bytes(nonstd::span<const uint8_t> data) noexcept;

  Bytes(const Bytes& other) noexcept;
  Bytes(Bytes&& other) noexcept;

  Bytes(std::initializer_list<uint8_t> init) noexcept;

  ~Bytes() noexcept;

  Bytes& operator=(const Bytes& other) noexcept;
  Bytes& operator=(Bytes&& other) noexcept;

  uint8_t operator[](size_t pos) const noexcept;
  uint8_t& operator[](size_t pos) noexcept;

  bool operator==(const Bytes& other) const noexcept;
  bool operator!=(const Bytes& other) const noexcept;

  const uint8_t* data() const noexcept;
  uint8_t* data() noexcept;

  uint8_t* begin() noexcept;
  const uint8_t* begin() const noexcept;
  const uint8_t* cbegin() const noexcept;

  uint8_t* end() noexcept;
  const uint8_t* end() const noexcept;
  const uint8_t* cend() const noexcept;

  bool empty() const noexcept;
  size_t size() const noexcept;
  size_t capacity() const noexcept;
  void reserve(size_t size) noexcept;

  void clear() noexcept;
  void resize(size_t size) noexcept; // Note: New bytes will be uninitialized.

  void insert(const uint8_t* pos,
              const uint8_t* first,
              const uint8_t* last) noexcept;
  void
  insert(const uint8_t* pos, const uint8_t* data, const size_t size) noexcept;
  void insert(const uint8_t* pos, const char* first, const char* last) noexcept;
  void insert(const uint8_t* pos, const char* data, size_t size) noexcept;
  void insert(const uint8_t* pos, nonstd::span<const uint8_t> data) noexcept;

private:
  std::unique_ptr<uint8_t[]> m_data;
  size_t m_size = 0;
  size_t m_capacity = 0;
};

inline Bytes::Bytes(size_t size) noexcept
  : m_data(std::make_unique<uint8_t[]>(size)),
    m_size(size),
    m_capacity(size)
{
}

inline Bytes::Bytes(const void* data, size_t size) noexcept
  : m_data(std::make_unique<uint8_t[]>(size)),
    m_size(size),
    m_capacity(size)
{
  std::memcpy(m_data.get(), data, size);
}

inline Bytes::Bytes(nonstd::span<const uint8_t> data) noexcept
  : Bytes(data.data(), data.size())
{
}

inline Bytes::Bytes(std::initializer_list<uint8_t> init) noexcept
  : Bytes({init.begin(), init.end()})
{
}

inline Bytes::~Bytes() noexcept = default;

inline uint8_t
Bytes::operator[](size_t pos) const noexcept
{
  return m_data[pos];
}

inline uint8_t&
Bytes::operator[](size_t pos) noexcept
{
  return m_data[pos];
}

inline bool
Bytes::operator==(const Bytes& other) const noexcept
{
  return this == &other
         || (m_size == other.m_size
             && std::memcmp(m_data.get(), other.m_data.get(), m_size) == 0);
}

inline bool
Bytes::operator!=(const Bytes& other) const noexcept
{
  return !(*this == other);
}

inline const uint8_t*
Bytes::data() const noexcept
{
  return m_data.get();
}

inline uint8_t*
Bytes::data() noexcept
{
  return m_data.get();
}

inline uint8_t*
Bytes::begin() noexcept
{
  return m_data.get();
}

inline const uint8_t*
Bytes::begin() const noexcept
{
  return m_data.get();
}

inline const uint8_t*
Bytes::cbegin() const noexcept
{
  return m_data.get();
}

inline uint8_t*
Bytes::end() noexcept
{
  return m_data.get() + m_size;
}

inline const uint8_t*
Bytes::end() const noexcept
{
  return m_data.get() + m_size;
}

inline const uint8_t*
Bytes::cend() const noexcept
{
  return m_data.get() + m_size;
}

inline bool
Bytes::empty() const noexcept
{
  return m_size == 0;
}

inline size_t
Bytes::size() const noexcept
{
  return m_size;
}

inline size_t
Bytes::capacity() const noexcept
{
  return m_capacity;
}

inline void
Bytes::clear() noexcept
{
  m_size = 0;
}

inline void
Bytes::insert(const uint8_t* pos,
              const uint8_t* data,
              const size_t size) noexcept
{
  return insert(pos, data, data + size);
}

inline void
Bytes::insert(const uint8_t* pos, const char* first, const char* last) noexcept
{
  return insert(pos,
                reinterpret_cast<const uint8_t*>(first),
                reinterpret_cast<const uint8_t*>(last));
}

inline void
Bytes::insert(const uint8_t* pos, const char* data, size_t size) noexcept
{
  return insert(pos, data, data + size);
}

inline void
Bytes::insert(const uint8_t* pos, nonstd::span<const uint8_t> data) noexcept
{
  return insert(pos, &*data.begin(), &*data.end());
}

} // namespace util
