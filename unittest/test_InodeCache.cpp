// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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
#include "../src/InodeCache.hpp"
#include "../src/Util.hpp"
#include "../src/hash.hpp"

#include "third_party/catch.hpp"

#ifdef INODE_CACHE_SUPPORTED

namespace {

struct digest
digest_from_string(const char* s)
{
  struct digest digest;
  struct hash* hash = hash_init();
  hash_string(hash, s);
  hash_result_as_bytes(hash, &digest);
  hash_free(hash);
  return digest;
}

bool
digest_equals_string(const struct digest& digest, const char* s)
{
  struct digest rhs = digest_from_string(s);
  return digests_equal(&digest, &rhs);
}

bool
put(const Config& config, const char* filename, const char* s, int return_value)
{
  return InodeCache::put(config, filename, digest_from_string(s), return_value);
}

} // namespace

TEST_CASE("Test disabled")
{
  Config config;
  config.set_inode_cache(false);

  struct digest digest;
  int return_value;

  CHECK(!InodeCache::get(config, "a", &digest, &return_value));
  CHECK(!InodeCache::put(config, "a", digest, return_value));
  CHECK(!InodeCache::zero_stats(config));
  CHECK(-1 == InodeCache::get_hits(config));
  CHECK(-1 == InodeCache::get_misses(config));
  CHECK(-1 == InodeCache::get_errors(config));
}

TEST_CASE("Test lookup nonexistent")
{
  Config config;
  config.set_inode_cache(true);
  Util::write_file("a", "");

  struct digest digest;
  int return_value;

  CHECK(InodeCache::zero_stats(config));

  CHECK(!InodeCache::get(config, "a", &digest, &return_value));
  CHECK(0 == InodeCache::get_hits(config));
  CHECK(1 == InodeCache::get_misses(config));
  CHECK(0 == InodeCache::get_errors(config));
}

TEST_CASE("Test put and lookup")
{
  Config config;
  config.set_inode_cache(true);
  Util::write_file("a", "a text");

  CHECK(put(config, "a", "a text", 1));

  struct digest digest;
  int return_value;

  CHECK(InodeCache::zero_stats(config));

  CHECK(InodeCache::get(config, "a", &digest, &return_value));
  CHECK(digest_equals_string(digest, "a text"));
  CHECK(1 == return_value);
  CHECK(1 == InodeCache::get_hits(config));
  CHECK(0 == InodeCache::get_misses(config));
  CHECK(0 == InodeCache::get_errors(config));

  Util::write_file("a", "something else");

  CHECK(!InodeCache::get(config, "a", &digest, &return_value));
  CHECK(1 == InodeCache::get_hits(config));
  CHECK(1 == InodeCache::get_misses(config));
  CHECK(0 == InodeCache::get_errors(config));

  CHECK(put(config, "a", "something else", 2));

  CHECK(InodeCache::get(config, "a", &digest, &return_value));
  CHECK(digest_equals_string(digest, "something else"));
  CHECK(2 == return_value);
  CHECK(2 == InodeCache::get_hits(config));
  CHECK(1 == InodeCache::get_misses(config));
  CHECK(0 == InodeCache::get_errors(config));
}

TEST_CASE("Drop file")
{
  Config config;
  config.set_inode_cache(true);

  InodeCache::zero_stats(config);

  CHECK(!InodeCache::get_file(config).empty());
  CHECK(InodeCache::drop(config));
  CHECK(InodeCache::get_file(config).empty());
  CHECK(!InodeCache::drop(config));
}
#endif
