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

#include "bytes.hpp"

#include <ccache/util/assertions.hpp>

namespace util {

Bytes::Bytes(const Bytes& other) noexcept
  : m_data(std::make_unique<uint8_t[]>(other.m_size)),
    m_size(other.m_size),
    m_capacity(other.m_size)
{
  if (m_size > 0) {
    std::memcpy(m_data.get(), other.m_data.get(), m_size);
  }
}

Bytes::Bytes(Bytes&& other) noexcept
  : m_data(std::move(other.m_data)),
    m_size(other.m_size),
    m_capacity(other.m_capacity)
{
  other.m_data = nullptr;
  other.m_size = 0;
  other.m_capacity = 0;
}

Bytes&
Bytes::operator=(const Bytes& other) noexcept
{
  if (&other == this) {
    return *this;
  }
  m_data = std::make_unique<uint8_t[]>(other.m_size);
  m_size = other.m_size;
  m_capacity = other.m_size;
  if (m_size > 0) {
    std::memcpy(m_data.get(), other.m_data.get(), m_size);
  }
  return *this;
}

Bytes&
Bytes::operator=(Bytes&& other) noexcept
{
  if (&other == this) {
    return *this;
  }
  m_data = std::move(other.m_data);
  m_size = other.m_size;
  m_capacity = other.m_capacity;
  other.m_data.reset();
  other.m_size = 0;
  other.m_capacity = 0;
  return *this;
}

void
Bytes::reserve(size_t size) noexcept
{
  if (size > m_capacity) {
    auto bytes = std::make_unique<uint8_t[]>(size);
    if (m_size > 0) {
      std::memcpy(bytes.get(), m_data.get(), m_size);
    }
    m_data = std::move(bytes);
    m_capacity = size;
  }
}

void
Bytes::insert(const uint8_t* pos,
              const uint8_t* first,
              const uint8_t* last) noexcept
{
  const size_t inserted_size = last - first;
  if (inserted_size == 0) {
    return;
  }
  const size_t offset = pos - m_data.get();
  if (m_size + inserted_size > m_capacity) {
    m_capacity = std::max(2 * m_capacity, m_size + inserted_size);
    auto new_data = std::make_unique<uint8_t[]>(m_capacity);
    if (offset > 0) {
      std::memcpy(new_data.get(), m_data.get(), offset);
    }
    if (m_size > offset) {
      std::memcpy(new_data.get() + offset + inserted_size,
                  m_data.get() + offset,
                  m_size - offset);
    }
    m_data = std::move(new_data);
  } else if (m_size > offset) {
    std::memmove(m_data.get() + offset + inserted_size,
                 m_data.get() + offset,
                 m_size - offset);
  }
  std::memcpy(m_data.get() + offset, first, inserted_size);
  m_size += inserted_size;
}

void
Bytes::resize(size_t size) noexcept
{
  if (size > m_capacity) {
    // In C++20, use std::make_unique_for_overwrite instead.
    auto new_data = std::unique_ptr<uint8_t[]>(new uint8_t[size]);
    if (m_size > 0) {
      std::memcpy(new_data.get(), m_data.get(), m_size);
    }
    m_data = std::move(new_data);
    m_capacity = size;
  }
  m_size = size;
}

} // namespace util
