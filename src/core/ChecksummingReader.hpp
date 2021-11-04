// Copyright (C) 2021 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
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

#include <core/Reader.hpp>
#include <util/XXH3_128.hpp>

namespace core {

class ChecksummingReader : public Reader
{
public:
  ChecksummingReader(core::Reader& reader);

  using core::Reader::read;
  size_t read(void* data, size_t count) override;

  void set_reader(core::Reader& reader);

  util::XXH3_128::Digest digest() const;

private:
  core::Reader* m_reader;
  util::XXH3_128 m_checksum;
};

inline ChecksummingReader::ChecksummingReader(core::Reader& reader)
  : m_reader(&reader)
{
}

inline size_t
ChecksummingReader::read(void* const data, const size_t count)
{
  const auto bytes_read = m_reader->read(data, count);
  m_checksum.update(data, bytes_read);
  return bytes_read;
}

inline void
ChecksummingReader::set_reader(core::Reader& reader)
{
  m_reader = &reader;
}

inline util::XXH3_128::Digest
ChecksummingReader::digest() const
{
  return m_checksum.digest();
}

} // namespace core
