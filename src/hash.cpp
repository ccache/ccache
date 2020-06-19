// Copyright (C) 2002 Andrew Tridgell
// Copyright (C) 2010-2020 Joel Rosdahl and other contributors
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

#include "hash.hpp"

#include "Fd.hpp"
#include "Util.hpp"
#include "logging.hpp"

#include "third_party/blake3/blake3.h"

#define HASH_DELIMITER "\000cCaChE"

struct hash
{
  blake3_hasher hasher;
  FILE* debug_binary;
  FILE* debug_text;
};

static void
do_hash_buffer(struct hash* hash, const void* s, size_t len)
{
  assert(s);

  blake3_hasher_update(&hash->hasher, s, len);
  if (len > 0 && hash->debug_binary) {
    (void)fwrite(s, 1, len, hash->debug_binary);
  }
}

static void
do_debug_text(struct hash* hash, const void* s, size_t len)
{
  if (len > 0 && hash->debug_text) {
    (void)fwrite(s, 1, len, hash->debug_text);
  }
}

struct hash*
hash_init()
{
  auto hash = static_cast<struct hash*>(malloc(sizeof(struct hash)));
  blake3_hasher_init(&hash->hasher);
  hash->debug_binary = nullptr;
  hash->debug_text = nullptr;
  return hash;
}

struct hash*
hash_copy(struct hash* hash)
{
  auto result = static_cast<struct hash*>(malloc(sizeof(struct hash)));
  result->hasher = hash->hasher;
  result->debug_binary = nullptr;
  result->debug_text = nullptr;
  return result;
}

void
hash_free(struct hash* hash)
{
  free(hash);
}

void
hash_enable_debug(struct hash* hash,
                  const char* section_name,
                  FILE* debug_binary,
                  FILE* debug_text)
{
  hash->debug_binary = debug_binary;
  hash->debug_text = debug_text;

  do_debug_text(hash, "=== ", 4);
  do_debug_text(hash, section_name, strlen(section_name));
  do_debug_text(hash, " ===\n", 5);
}

void
hash_buffer(struct hash* hash, const void* s, size_t len)
{
  do_hash_buffer(hash, s, len);
  do_debug_text(hash, s, len);
}

Digest
hash_buffer_once(const void* s, size_t len)
{
  blake3_hasher hasher;
  blake3_hasher_init(&hasher);
  blake3_hasher_update(&hasher, s, len);

  Digest digest;
  blake3_hasher_finalize(&hasher, digest.bytes(), digest.size());
  return digest;
}

Digest
hash_result(struct hash* hash)
{
  // Note that blake3_hasher_finalize doesn't modify the hasher itself, thus it
  // is possible to finalize again after more data has been added.
  Digest digest;
  blake3_hasher_finalize(&hash->hasher, digest.bytes(), digest.size());
  return digest;
}

void
hash_delimiter(struct hash* hash, const char* type)
{
  do_hash_buffer(hash, HASH_DELIMITER, sizeof(HASH_DELIMITER));
  do_hash_buffer(hash, type, strlen(type) + 1); // Include NUL.
  do_debug_text(hash, "### ", 4);
  do_debug_text(hash, type, strlen(type));
  do_debug_text(hash, "\n", 1);
}

void
hash_string(struct hash* hash, const char* s)
{
  hash_string_buffer(hash, s, strlen(s));
}

void
hash_string(struct hash* hash, const std::string& s)
{
  hash_string_buffer(hash, s.data(), s.length());
}

void
hash_string_view(struct hash* hash, nonstd::string_view sv)
{
  hash_string_buffer(hash, sv.data(), sv.length());
}

void
hash_string_buffer(struct hash* hash, const char* s, size_t length)
{
  hash_buffer(hash, s, length);
  do_debug_text(hash, "\n", 1);
}

void
hash_int(struct hash* hash, int x)
{
  do_hash_buffer(hash, reinterpret_cast<const char*>(&x), sizeof(x));

  char buf[16];
  snprintf(buf, sizeof(buf), "%d", x);
  do_debug_text(hash, buf, strlen(buf));
  do_debug_text(hash, "\n", 1);
}

bool
hash_fd(struct hash* hash, int fd, bool fd_is_file)
{
  char buf[READ_BUFFER_SIZE];
  ssize_t n;

  while ((n = read(fd, buf, sizeof(buf))) != 0) {
    if (n == -1 && errno != EINTR) {
      break;
    }
    if (n > 0) {
      do_hash_buffer(hash, buf, n);
      do_debug_text(hash, buf, n);
      if (fd_is_file && static_cast<size_t>(n) < sizeof(buf)) {
        break;
      }
    }
  }
  return n >= 0;
}

bool
hash_file(struct hash* hash, const char* fname)
{
  Fd fd(open(fname, O_RDONLY | O_BINARY));
  if (!fd) {
    cc_log("Failed to open %s: %s", fname, strerror(errno));
    return false;
  }

  bool ret = hash_fd(hash, *fd, true);
  return ret;
}
