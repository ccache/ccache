// Copyright (C) 2020-2023 Joel Rosdahl and other contributors
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

#include "../src/Config.hpp"
#include "../src/Context.hpp"
#include "../src/Hash.hpp"
#include "../src/InodeCache.hpp"
#include "../src/Util.hpp"
#include "TestUtil.hpp"

#include <Fd.hpp>
#include <util/file.hpp>

#include "third_party/doctest.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

using TestUtil::TestContext;

namespace {

bool
inode_cache_available()
{
  Fd fd(open(Util::get_actual_cwd().c_str(), O_RDONLY));
  return fd && InodeCache::available(*fd);
}

void
init(Config& config)
{
  config.set_debug(true);
  config.set_inode_cache(true);
  config.set_temporary_dir(Util::get_actual_cwd());
}

bool
put(InodeCache& inode_cache,
    const std::string& filename,
    const std::string& str,
    HashSourceCodeResult return_value)
{
  return inode_cache.put(filename,
                         InodeCache::ContentType::checked_for_temporal_macros,
                         Hash().hash(str).digest(),
                         return_value);
}

} // namespace

TEST_SUITE_BEGIN("InodeCache" * doctest::skip(!inode_cache_available()));

TEST_CASE("Test disabled")
{
  TestContext test_context;

  Config config;
  init(config);
  config.set_inode_cache(false);
  InodeCache inode_cache(config, util::Duration(0));

  CHECK(!inode_cache.get("a",
                         InodeCache::ContentType::checked_for_temporal_macros));
  CHECK(!inode_cache.put("a",
                         InodeCache::ContentType::checked_for_temporal_macros,
                         Hash::Digest(),
                         HashSourceCodeResult()));
  CHECK(inode_cache.get_hits() == -1);
  CHECK(inode_cache.get_misses() == -1);
  CHECK(inode_cache.get_errors() == -1);
}

TEST_CASE("Test lookup nonexistent")
{
  TestContext test_context;

  Config config;
  init(config);

  InodeCache inode_cache(config, util::Duration(0));
  util::write_file("a", "");

  CHECK(!inode_cache.get("a",
                         InodeCache::ContentType::checked_for_temporal_macros));
  CHECK(inode_cache.get_hits() == 0);
  CHECK(inode_cache.get_misses() == 1);
  CHECK(inode_cache.get_errors() == 0);
}

TEST_CASE("Test put and lookup")
{
  TestContext test_context;

  Config config;
  init(config);

  InodeCache inode_cache(config, util::Duration(0));
  util::write_file("a", "a text");

  HashSourceCodeResult result;
  result.insert(HashSourceCode::found_date);
  CHECK(put(inode_cache, "a", "a text", result));

  auto return_value =
    inode_cache.get("a", InodeCache::ContentType::checked_for_temporal_macros);
  REQUIRE(return_value);
  CHECK(return_value->first.to_bitmask()
        == static_cast<int>(HashSourceCode::found_date));
  CHECK(return_value->second == Hash().hash("a text").digest());
  CHECK(inode_cache.get_hits() == 1);
  CHECK(inode_cache.get_misses() == 0);
  CHECK(inode_cache.get_errors() == 0);

  util::write_file("a", "something else");

  CHECK(!inode_cache.get("a",
                         InodeCache::ContentType::checked_for_temporal_macros));
  CHECK(inode_cache.get_hits() == 1);
  CHECK(inode_cache.get_misses() == 1);
  CHECK(inode_cache.get_errors() == 0);

  CHECK(put(inode_cache,
            "a",
            "something else",
            HashSourceCodeResult(HashSourceCode::found_time)));

  return_value =
    inode_cache.get("a", InodeCache::ContentType::checked_for_temporal_macros);
  REQUIRE(return_value);
  CHECK(return_value->first.to_bitmask()
        == static_cast<int>(HashSourceCode::found_time));
  CHECK(return_value->second == Hash().hash("something else").digest());
  CHECK(inode_cache.get_hits() == 2);
  CHECK(inode_cache.get_misses() == 1);
  CHECK(inode_cache.get_errors() == 0);
}

TEST_CASE("Drop file")
{
  TestContext test_context;

  Config config;
  init(config);

  InodeCache inode_cache(config, util::Duration(0));

  inode_cache.get("a", InodeCache::ContentType::raw);
  CHECK(Stat::stat(inode_cache.get_file()));
  CHECK(inode_cache.drop());
  CHECK(!Stat::stat(inode_cache.get_file()));
  CHECK(inode_cache.drop());
}

TEST_CASE("Test content type")
{
  TestContext test_context;

  Config config;
  init(config);

  InodeCache inode_cache(config, util::Duration(0));
  util::write_file("a", "a text");
  auto binary_digest = Hash().hash("binary").digest();
  auto code_digest = Hash().hash("code").digest();

  CHECK(inode_cache.put("a",
                        InodeCache::ContentType::raw,
                        binary_digest,
                        HashSourceCodeResult(HashSourceCode::found_date)));
  CHECK(inode_cache.put("a",
                        InodeCache::ContentType::checked_for_temporal_macros,
                        code_digest,
                        HashSourceCodeResult(HashSourceCode::found_time)));

  auto return_value = inode_cache.get("a", InodeCache::ContentType::raw);
  REQUIRE(return_value);
  CHECK(return_value->first.to_bitmask()
        == static_cast<int>(HashSourceCode::found_date));
  CHECK(return_value->second == binary_digest);

  return_value =
    inode_cache.get("a", InodeCache::ContentType::checked_for_temporal_macros);
  REQUIRE(return_value);
  CHECK(return_value->first.to_bitmask()
        == static_cast<int>(HashSourceCode::found_time));
  CHECK(return_value->second == code_digest);
}

TEST_SUITE_END();
