// Copyright (C) 2020 Joel Rosdahl and other contributors
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
put(const Context& ctx, const char* filename, const char* s, int return_value)
{
  return ctx.inode_cache.put(filename, digest_from_string(s), return_value);
}

} // namespace

TEST_CASE("Test disabled")
{
  Context ctx;
  ctx.config.set_inode_cache(false);

  struct digest digest;
  int return_value;

  CHECK(!ctx.inode_cache.get("a", &digest, &return_value));
  CHECK(!ctx.inode_cache.put("a", digest, return_value));
  CHECK(!ctx.inode_cache.zero_stats());
  CHECK(ctx.inode_cache.get_hits() == -1);
  CHECK(ctx.inode_cache.get_misses() == -1);
  CHECK(ctx.inode_cache.get_errors() == -1);
}

TEST_CASE("Test lookup nonexistent")
{
  Context ctx;
  ctx.config.set_inode_cache(true);
  Util::write_file("a", "");

  struct digest digest;
  int return_value;

  CHECK(ctx.inode_cache.zero_stats());

  CHECK(!ctx.inode_cache.get("a", &digest, &return_value));
  CHECK(ctx.inode_cache.get_hits() == 0);
  CHECK(ctx.inode_cache.get_misses() == 1);
  CHECK(ctx.inode_cache.get_errors() == 0);
}

TEST_CASE("Test put and lookup")
{
  Context ctx;
  ctx.config.set_inode_cache(true);
  Util::write_file("a", "a text");

  CHECK(put(ctx, "a", "a text", 1));

  struct digest digest;
  int return_value;

  CHECK(ctx.inode_cache.zero_stats());

  CHECK(ctx.inode_cache.get("a", &digest, &return_value));
  CHECK(digest_equals_string(digest, "a text"));
  CHECK(return_value == 1);
  CHECK(ctx.inode_cache.get_hits() == 1);
  CHECK(ctx.inode_cache.get_misses() == 0);
  CHECK(ctx.inode_cache.get_errors() == 0);

  Util::write_file("a", "something else");

  CHECK(!ctx.inode_cache.get("a", &digest, &return_value));
  CHECK(ctx.inode_cache.get_hits() == 1);
  CHECK(ctx.inode_cache.get_misses() == 1);
  CHECK(ctx.inode_cache.get_errors() == 0);

  CHECK(put(ctx, "a", "something else", 2));

  CHECK(ctx.inode_cache.get("a", &digest, &return_value));
  CHECK(digest_equals_string(digest, "something else"));
  CHECK(return_value == 2);
  CHECK(ctx.inode_cache.get_hits() == 2);
  CHECK(ctx.inode_cache.get_misses() == 1);
  CHECK(ctx.inode_cache.get_errors() == 0);
}

TEST_CASE("Drop file")
{
  Context ctx;
  ctx.config.set_inode_cache(true);

  ctx.inode_cache.zero_stats();

  CHECK(!ctx.inode_cache.get_file().empty());
  CHECK(ctx.inode_cache.drop());
  CHECK(ctx.inode_cache.get_file().empty());
  CHECK(!ctx.inode_cache.drop());
}
#endif
