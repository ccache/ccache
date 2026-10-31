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

#include "testutil.hpp"

#include <ccache/config.hpp>
#include <ccache/core/cacheentry.hpp>
#include <ccache/core/result.hpp>
#include <ccache/core/types.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/string.hpp>

#include <doctest/doctest.h>

TEST_CASE("Serialize result files through a sink")
{
  TestUtil::TestContext test_context;
  Config config;
  config.set_file_clone(false);
  config.set_hard_link(false);
  bool compressed = false;

  SUBCASE("Uncompressed")
  {
    compressed = false;
  }

  SUBCASE("Compressed")
  {
    compressed = true;
  }

  util::Bytes data(2 * CCACHE_READ_BUFFER_SIZE + 17);
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<uint8_t>(i);
  }
  REQUIRE(util::write_file("object", data));
  REQUIRE(util::write_file("empty", ""));
  core::result::Serializer serializer(config);
  serializer.add_data(core::result::FileType::stderr_output,
                      util::to_span("diagnostics"));
  REQUIRE(serializer.add_file(core::result::FileType::object, "object"));
  REQUIRE(serializer.add_file(core::result::FileType::dependency, "empty"));

  util::Bytes expected;
  size_t largest_chunk = 0;
  serializer.serialize([&](auto chunk) {
    largest_chunk = std::max(largest_chunk, chunk.size());
    expected.insert(expected.end(), chunk);
  });
  CHECK(largest_chunk <= CCACHE_READ_BUFFER_SIZE);
  CHECK(expected.size() == serializer.serialized_size());

  core::CacheEntry::Header header(config, core::CacheEntryType::result);
  header.compression_type =
    compressed ? core::CompressionType::zstd : core::CompressionType::none;
  const auto serialized = core::CacheEntry::serialize(header, serializer);
  const core::CacheEntry entry(serialized);
  CHECK_NOTHROW(entry.verify_checksum());
  CHECK(util::Bytes(entry.payload()) == expected);

  // Compare streaming and non-streaming versions:
  const auto non_streaming = core::CacheEntry::serialize(header, expected);
  const core::CacheEntry non_streaming_entry(non_streaming);
  CHECK_NOTHROW(non_streaming_entry.verify_checksum());
  CHECK(util::Bytes(non_streaming_entry.payload()) == expected);
}
