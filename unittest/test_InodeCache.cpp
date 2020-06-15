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
#include "TestUtil.hpp"

#include "third_party/catch.hpp"

#ifdef INODE_CACHE_SUPPORTED

using TestUtil::TestContext;

namespace {

Digest
digest_from_string(const char* s)
{
  Digest digest;
  struct hash* hash = hash_init();
  hash_string(hash, s);
  digest = hash_result(hash);
  hash_free(hash);
  return digest;
}

bool
digest_equals_string(const Digest& digest, const char* s)
{
  return digest == digest_from_string(s);
}

bool
put(const Context& ctx, const char* filename, const char* s, int return_value)
{
  return ctx.inode_cache.put(filename,
                             InodeCache::ContentType::code,
                             digest_from_string(s),
                             return_value);
}

} // namespace

TEST_CASE("Test disabled")
{
  TestContext test_context;

  Context ctx;
  ctx.config.set_debug(true);
  ctx.config.set_inode_cache(false);

  Digest digest;
  int return_value;

  CHECK(!ctx.inode_cache.get(
    "a", InodeCache::ContentType::code, digest, &return_value));
  CHECK(!ctx.inode_cache.put(
    "a", InodeCache::ContentType::code, digest, return_value));
  CHECK(ctx.inode_cache.get_hits() == -1);
  CHECK(ctx.inode_cache.get_misses() == -1);
  CHECK(ctx.inode_cache.get_errors() == -1);
}

TEST_CASE("Test lookup nonexistent")
{
  TestContext test_context;

  Context ctx;
  ctx.config.set_debug(true);
  ctx.config.set_inode_cache(true);
  ctx.inode_cache.drop();
  Util::write_file("a", "");

  Digest digest;
  int return_value;

  CHECK(!ctx.inode_cache.get(
    "a", InodeCache::ContentType::code, digest, &return_value));
  CHECK(ctx.inode_cache.get_hits() == 0);
  CHECK(ctx.inode_cache.get_misses() == 1);
  CHECK(ctx.inode_cache.get_errors() == 0);
}

TEST_CASE("Test put and lookup")
{
  TestContext test_context;

  Context ctx;
  ctx.config.set_debug(true);
  ctx.config.set_inode_cache(true);
  ctx.inode_cache.drop();
  Util::write_file("a", "a text");

  CHECK(put(ctx, "a", "a text", 1));

  Digest digest;
  int return_value;

  CHECK(ctx.inode_cache.get(
    "a", InodeCache::ContentType::code, digest, &return_value));
  CHECK(digest_equals_string(digest, "a text"));
  CHECK(return_value == 1);
  CHECK(ctx.inode_cache.get_hits() == 1);
  CHECK(ctx.inode_cache.get_misses() == 0);
  CHECK(ctx.inode_cache.get_errors() == 0);

  Util::write_file("a", "something else");

  CHECK(!ctx.inode_cache.get(
    "a", InodeCache::ContentType::code, digest, &return_value));
  CHECK(ctx.inode_cache.get_hits() == 1);
  CHECK(ctx.inode_cache.get_misses() == 1);
  CHECK(ctx.inode_cache.get_errors() == 0);

  CHECK(put(ctx, "a", "something else", 2));

  CHECK(ctx.inode_cache.get(
    "a", InodeCache::ContentType::code, digest, &return_value));
  CHECK(digest_equals_string(digest, "something else"));
  CHECK(return_value == 2);
  CHECK(ctx.inode_cache.get_hits() == 2);
  CHECK(ctx.inode_cache.get_misses() == 1);
  CHECK(ctx.inode_cache.get_errors() == 0);
}

TEST_CASE("Drop file")
{
  TestContext test_context;

  Context ctx;
  ctx.config.set_debug(true);
  ctx.config.set_inode_cache(true);

  Digest digest;

  ctx.inode_cache.get("a", InodeCache::ContentType::binary, digest);
  CHECK(Stat::stat(ctx.inode_cache.get_file()));
  CHECK(ctx.inode_cache.drop());
  CHECK(!Stat::stat(ctx.inode_cache.get_file()));
  CHECK(!ctx.inode_cache.drop());
}

TEST_CASE("Test content type")
{
  TestContext test_context;

  Context ctx;
  ctx.config.set_debug(true);
  ctx.inode_cache.drop();
  ctx.config.set_inode_cache(true);
  Util::write_file("a", "a text");
  Digest binary_digest = digest_from_string("binary");
  Digest code_digest = digest_from_string("code");
  Digest code_with_sloppy_time_macros_digest =
    digest_from_string("sloppy_time_macros");

  CHECK(ctx.inode_cache.put(
    "a", InodeCache::ContentType::binary, binary_digest, 1));
  CHECK(
    ctx.inode_cache.put("a", InodeCache::ContentType::code, code_digest, 2));
  CHECK(
    ctx.inode_cache.put("a",
                        InodeCache::ContentType::code_with_sloppy_time_macros,
                        code_with_sloppy_time_macros_digest,
                        3));

  Digest digest;
  int return_value;

  CHECK(ctx.inode_cache.get(
    "a", InodeCache::ContentType::binary, digest, &return_value));
  CHECK(digest == binary_digest);
  CHECK(return_value == 1);

  CHECK(ctx.inode_cache.get(
    "a", InodeCache::ContentType::code, digest, &return_value));
  CHECK(digest == code_digest);
  CHECK(return_value == 2);

  CHECK(
    ctx.inode_cache.get("a",
                        InodeCache::ContentType::code_with_sloppy_time_macros,
                        digest,
                        &return_value));
  CHECK(digest == code_with_sloppy_time_macros_digest);
  CHECK(return_value == 3);
}

#endif
