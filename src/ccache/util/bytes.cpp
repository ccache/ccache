// Copyright (C) 2022-2025 Joel Rosdahl and other contributors
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

#include <stdexcept>

namespace util {

namespace {

void
assign_from_data(Bytes* bytes, const void* data, size_t size) noexcept
{
  bytes->resize(size);
  if (size > 0) {
    std::memcpy(bytes->data(), data, size);
  }
}

} // namespace

Bytes::Bytes(const Bytes& other) noexcept
{
  if (other.m_size > 0) {
    resize(other.m_size);
    std::memcpy(m_data.get(), other.m_data.get(), m_size);
  }
}

Bytes::Bytes(Bytes&& other) noexcept
  : m_data(std::move(other.m_data)),
    m_size(other.m_size),
    m_capacity(other.m_capacity)
{
  other.m_data.reset();
  other.m_size = 0;
  other.m_capacity = 0;
}

Bytes&
Bytes::operator=(const Bytes& other) noexcept
{
  if (&other == this) {
    return *this;
  }
  assign_from_data(this, other.m_data.get(), other.m_size);
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

Bytes&
Bytes::operator=(nonstd::span<const uint8_t> data) noexcept
{
  assign_from_data(this, data.data(), data.size());
  return *this;
}

Bytes&
Bytes::operator=(std::string_view data) noexcept
{
  assign_from_data(this, data.data(), data.size());
  return *this;
}

uint8_t
Bytes::at(size_t pos) const
{
  if (pos >= m_size) {
    throw std::out_of_range("Bytes::at: pos >= size()");
  }
  return m_data[pos];
}

uint8_t&
Bytes::at(size_t pos)
{
  if (pos >= m_size) {
    throw std::out_of_range("Bytes::at: pos >= size()");
  }
  return m_data[pos];
}

void
Bytes::reserve(size_t size) noexcept
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
}

void
Bytes::insert(const void* pos, const void* data, size_t size) noexcept
{
  if (size == 0) {
    return;
  }
  const size_t offset = reinterpret_cast<const uint8_t*>(pos) - m_data.get();
  if (m_size + size > m_capacity) {
    m_capacity = std::max(2 * m_capacity, m_size + size);
    // In C++20, use std::make_unique_for_overwrite instead.
    auto new_data = std::unique_ptr<uint8_t[]>(new uint8_t[m_capacity]);
    if (offset > 0) {
      std::memcpy(new_data.get(), m_data.get(), offset);
    }
    if (m_size > offset) {
      std::memcpy(
        new_data.get() + offset + size, m_data.get() + offset, m_size - offset);
    }
    m_data = std::move(new_data);
  } else if (m_size > offset) {
    std::memmove(
      m_data.get() + offset + size, m_data.get() + offset, m_size - offset);
  }
  std::memcpy(m_data.get() + offset, data, size);
  m_size += size;
}

void
Bytes::push_back(uint8_t value) noexcept
{
  if (m_size >= m_capacity) {
    reserve(m_capacity == 0 ? 1 : 2 * m_capacity);
  }
  m_data[m_size] = value;
  ++m_size;
}

void
Bytes::resize(size_t size) noexcept
{
  reserve(size);
  m_size = size;
}

void
Bytes::erase(const void* pos, const size_t size) noexcept
{
  if (size == 0) {
    return;
  }
  const size_t offset = reinterpret_cast<const uint8_t*>(pos) - m_data.get();
  if (offset + size < m_size) {
    std::memmove(m_data.get() + offset,
                 m_data.get() + offset + size,
                 m_size - offset - size);
  }
  m_size -= size;
}

} // namespace util
