// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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
    int return_value)
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
  InodeCache inode_cache(config);

  Digest digest;
  int return_value;

  CHECK(!inode_cache.get("a",
                         InodeCache::ContentType::checked_for_temporal_macros,
                         digest,
                         &return_value));
  CHECK(!inode_cache.put("a",
                         InodeCache::ContentType::checked_for_temporal_macros,
                         digest,
                         return_value));
  CHECK(inode_cache.get_hits() == -1);
  CHECK(inode_cache.get_misses() == -1);
  CHECK(inode_cache.get_errors() == -1);
}

TEST_CASE("Test lookup nonexistent")
{
  TestContext test_context;

  Config config;
  init(config);

  InodeCache inode_cache(config);
  util::write_file("a", "");

  Digest digest;
  int return_value;

  CHECK(!inode_cache.get("a",
                         InodeCache::ContentType::checked_for_temporal_macros,
                         digest,
                         &return_value));
  CHECK(inode_cache.get_hits() == 0);
  CHECK(inode_cache.get_misses() == 1);
  CHECK(inode_cache.get_errors() == 0);
}

TEST_CASE("Test put and lookup")
{
  TestContext test_context;

  Config config;
  init(config);

  InodeCache inode_cache(config);
  util::write_file("a", "a text");

  CHECK(put(inode_cache, "a", "a text", 1));

  Digest digest;
  int return_value;

  CHECK(inode_cache.get("a",
                        InodeCache::ContentType::checked_for_temporal_macros,
                        digest,
                        &return_value));
  CHECK(digest == Hash().hash("a text").digest());
  CHECK(return_value == 1);
  CHECK(inode_cache.get_hits() == 1);
  CHECK(inode_cache.get_misses() == 0);
  CHECK(inode_cache.get_errors() == 0);

  util::write_file("a", "something else");

  CHECK(!inode_cache.get("a",
                         InodeCache::ContentType::checked_for_temporal_macros,
                         digest,
                         &return_value));
  CHECK(inode_cache.get_hits() == 1);
  CHECK(inode_cache.get_misses() == 1);
  CHECK(inode_cache.get_errors() == 0);

  CHECK(put(inode_cache, "a", "something else", 2));

  CHECK(inode_cache.get("a",
                        InodeCache::ContentType::checked_for_temporal_macros,
                        digest,
                        &return_value));
  CHECK(digest == Hash().hash("something else").digest());
  CHECK(return_value == 2);
  CHECK(inode_cache.get_hits() == 2);
  CHECK(inode_cache.get_misses() == 1);
  CHECK(inode_cache.get_errors() == 0);
}

TEST_CASE("Drop file")
{
  TestContext test_context;

  Config config;
  init(config);

  InodeCache inode_cache(config);

  Digest digest;

  inode_cache.get("a", InodeCache::ContentType::raw, digest);
  CHECK(Stat::stat(inode_cache.get_file()));
  CHECK(inode_cache.drop());
  CHECK(!Stat::stat(inode_cache.get_file()));
  CHECK(!inode_cache.drop());
}

TEST_CASE("Test content type")
{
  TestContext test_context;

  Config config;
  init(config);

  InodeCache inode_cache(config);
  util::write_file("a", "a text");
  Digest binary_digest = Hash().hash("binary").digest();
  Digest code_digest = Hash().hash("code").digest();

  CHECK(inode_cache.put("a", InodeCache::ContentType::raw, binary_digest, 1));
  CHECK(inode_cache.put(
    "a", InodeCache::ContentType::checked_for_temporal_macros, code_digest, 2));

  Digest digest;
  int return_value;

  CHECK(
    inode_cache.get("a", InodeCache::ContentType::raw, digest, &return_value));
  CHECK(digest == binary_digest);
  CHECK(return_value == 1);

  CHECK(inode_cache.get("a",
                        InodeCache::ContentType::checked_for_temporal_macros,
                        digest,
                        &return_value));
  CHECK(digest == code_digest);
  CHECK(return_value == 2);
}

TEST_SUITE_END();
