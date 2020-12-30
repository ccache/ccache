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

#include "../src/Context.hpp"
#include "../src/Hash.hpp"
#include "../src/InodeCache.hpp"
#include "TestUtil.hpp"
#include "core/Config.hpp"
#include "core/Util.hpp"

#include "third_party/doctest.h"

using TestUtil::TestContext;

namespace {

void
init(Context& ctx)
{
  ctx.config.set_debug(true);
  ctx.config.set_inode_cache(true);
  ctx.config.set_cache_dir(Util::get_home_directory());
}

bool
put(const Context& ctx,
    const std::string& filename,
    const std::string& str,
    int return_value)
{
  return ctx.inode_cache.put(filename,
                             InodeCache::ContentType::code,
                             Hash().hash(str).digest(),
                             return_value);
}

} // namespace

TEST_SUITE_BEGIN("InodeCache");

TEST_CASE("Test disabled")
{
  TestContext test_context;

  Context ctx;
  init(ctx);
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
  init(ctx);
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
  init(ctx);
  ctx.inode_cache.drop();
  Util::write_file("a", "a text");

  CHECK(put(ctx, "a", "a text", 1));

  Digest digest;
  int return_value;

  CHECK(ctx.inode_cache.get(
    "a", InodeCache::ContentType::code, digest, &return_value));
  CHECK(digest == Hash().hash("a text").digest());
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
  CHECK(digest == Hash().hash("something else").digest());
  CHECK(return_value == 2);
  CHECK(ctx.inode_cache.get_hits() == 2);
  CHECK(ctx.inode_cache.get_misses() == 1);
  CHECK(ctx.inode_cache.get_errors() == 0);
}

TEST_CASE("Drop file")
{
  TestContext test_context;

  Context ctx;
  init(ctx);

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
  init(ctx);
  ctx.inode_cache.drop();
  Util::write_file("a", "a text");
  Digest binary_digest = Hash().hash("binary").digest();
  Digest code_digest = Hash().hash("code").digest();
  Digest code_with_sloppy_time_macros_digest =
    Hash().hash("sloppy_time_macros").digest();

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

TEST_SUITE_END();
