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

#include <ccache/context.hpp>
#include <ccache/core/manifest.hpp>
#include <ccache/hash.hpp>
#include <ccache/hashutil.hpp>
#include <ccache/util/bytes.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>

#include <doctest/doctest.h>

#include <string>

namespace fs = util::filesystem;

using TestUtil::TestContext;

static core::Manifest::FileStats
stat_file(const std::string& path)
{
  util::DirEntry entry(path);
  return {entry.size(), util::TimePoint(), util::TimePoint()};
}

TEST_SUITE_BEGIN("core::Manifest");

TEST_CASE("core::Manifest shadow paths")
{
  TestContext test_context;
  Context ctx;

  REQUIRE(fs::create_directories("inc1"));
  REQUIRE(fs::create_directories("inc2"));
  REQUIRE(util::write_file("inc2/hello.h", "int x;\n"));
  const auto digest = hash_source_code_file(ctx, "inc2/hello.h");
  REQUIRE(digest);

  Hash::Digest key;
  key.fill(1);
  Hash::Digest other_key;
  other_key.fill(2);

  core::Manifest manifest;
  REQUIRE(manifest.add_result(key,
                              {
                                {"inc2/hello.h", *digest}
  },
                              {"inc1/hello.h"},
                              stat_file));
  CHECK(manifest.look_up_result_digest(ctx) == key);

  SUBCASE("appearing shadow path invalidates result")
  {
    REQUIRE(util::write_file("inc1/hello.h", ""));
    CHECK(!manifest.look_up_result_digest(ctx));
  }

  SUBCASE("serialization")
  {
    util::Bytes data;
    manifest.serialize(data);
    CHECK(data.size() == manifest.serialized_size());

    core::Manifest read_manifest;
    read_manifest.read(data);
    CHECK(read_manifest.look_up_result_digest(ctx) == key);

    REQUIRE(util::write_file("inc1/hello.h", ""));
    CHECK(!read_manifest.look_up_result_digest(ctx));
  }

  SUBCASE("merge")
  {
    util::Bytes data;
    manifest.serialize(data);

    core::Manifest merged;
    REQUIRE(merged.add_result(other_key,
                              {
                                {"inc2/hello.h", *digest}
    },
                              {},
                              stat_file));
    merged.read(data);
    CHECK(merged.look_up_result_digest(ctx) == key);

    REQUIRE(util::write_file("inc1/hello.h", ""));
    CHECK(merged.look_up_result_digest(ctx) == other_key);
  }

  SUBCASE("results with different shadow paths are distinct")
  {
    CHECK(!manifest.add_result(key,
                               {
                                 {"inc2/hello.h", *digest}
    },
                               {"inc1/hello.h"},
                               stat_file));
    CHECK(manifest.add_result(key,
                              {
                                {"inc2/hello.h", *digest}
    },
                              {},
                              stat_file));
  }
}

TEST_SUITE_END();
