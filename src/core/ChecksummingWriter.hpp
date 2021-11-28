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

#include <core/Writer.hpp>
#include <util/XXH3_128.hpp>

namespace core {

class ChecksummingWriter : public Writer
{
public:
  ChecksummingWriter(core::Writer& writer);

  using core::Writer::write;
  void write(const void* data, size_t count) override;
  void finalize() override;

  void set_writer(core::Writer& writer);

  util::XXH3_128::Digest digest() const;

private:
  core::Writer* m_writer;
  util::XXH3_128 m_checksum;
};

inline ChecksummingWriter::ChecksummingWriter(core::Writer& writer)
  : m_writer(&writer)
{
}

inline void
ChecksummingWriter::write(const void* const data, const size_t count)
{
  m_writer->write(data, count);
  m_checksum.update(data, count);
}

inline void
ChecksummingWriter::finalize()
{
  m_writer->finalize();
}

inline void
ChecksummingWriter::set_writer(core::Writer& writer)
{
  m_writer = &writer;
}

inline util::XXH3_128::Digest
ChecksummingWriter::digest() const
{
  return m_checksum.digest();
}

} // namespace core
