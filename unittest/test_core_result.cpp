// Copyright (C) 2026 Joel Rosdahl and other contributors
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

#include <ccache/config.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/core/result.hpp>
#include <ccache/util/bytes.hpp>

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>

using core::result::Deserializer;
using core::result::FileType;
using core::result::Serializer;

namespace {

class CountingVisitor : public Deserializer::Visitor
{
public:
  void
  on_header(const Deserializer::Header& header) override
  {
    m_header = header;
  }

  void
  on_embedded_file(uint8_t,
                   FileType,
                   std::span<const uint8_t> /*data*/) override
  {
    ++m_embedded_files;
  }

  void
  on_raw_file(uint8_t, FileType, uint64_t) override
  {
    ++m_raw_files;
  }

  [[nodiscard]] const Deserializer::Header&
  header() const
  {
    return m_header;
  }

  [[nodiscard]] size_t
  embedded_files() const
  {
    return m_embedded_files;
  }

  [[nodiscard]] size_t
  raw_files() const
  {
    return m_raw_files;
  }

private:
  Deserializer::Header m_header;
  size_t m_embedded_files = 0;
  size_t m_raw_files = 0;
};

} // namespace

TEST_SUITE_BEGIN("core::result");

TEST_CASE("Serializer accepts the maximum uint8_t number of file entries")
{
  Config config;
  Serializer serializer(config);
  const std::array<uint8_t, 1> data = {0x2a};
  const auto max_file_entries = std::numeric_limits<uint8_t>::max();

  for (size_t i = 0; i < max_file_entries; ++i) {
    serializer.add_data(FileType::stderr_output, data);
  }

  util::Bytes bytes;
  serializer.serialize(bytes);

  CountingVisitor visitor;
  Deserializer(bytes).visit(visitor);

  CHECK(visitor.header().n_files == max_file_entries);
  CHECK(visitor.embedded_files() == max_file_entries);
  CHECK(visitor.raw_files() == 0);
}

TEST_CASE("Serializer rejects more than the uint8_t number of file entries")
{
  Config config;
  Serializer serializer(config);
  const std::array<uint8_t, 1> data = {0x2a};
  const auto max_file_entries = std::numeric_limits<uint8_t>::max();

  for (size_t i = 0; i <= max_file_entries; ++i) {
    serializer.add_data(FileType::stderr_output, data);
  }

  util::Bytes bytes;
  CHECK_THROWS_AS(serializer.serialize(bytes), core::Error);
}

TEST_SUITE_END();
