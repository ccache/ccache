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

#include "Hash.hpp"

#include "Fd.hpp"
#include "Logging.hpp"
#include "fmtmacros.hpp"

#include <core/wincompat.hpp>
#include <util/file.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

const std::string_view HASH_DELIMITER("\000cCaChE\000", 8);

Hash::Hash()
{
  blake3_hasher_init(&m_hasher);
}

void
Hash::enable_debug(std::string_view section_name,
                   FILE* debug_binary,
                   FILE* debug_text)
{
  m_debug_binary = debug_binary;
  m_debug_text = debug_text;

  add_debug_text("=== ");
  add_debug_text(section_name);
  add_debug_text(" ===\n");
}

Digest
Hash::digest() const
{
  // Note that blake3_hasher_finalize doesn't modify the hasher itself, thus it
  // is possible to finalize again after more data has been added.
  Digest digest;
  blake3_hasher_finalize(&m_hasher, digest.bytes(), digest.size());
  return digest;
}

Hash&
Hash::hash_delimiter(std::string_view type)
{
  hash_buffer(HASH_DELIMITER);
  hash_buffer(type);
  hash_buffer(std::string_view("\x00", 1));
  add_debug_text("### ");
  add_debug_text(type);
  add_debug_text("\n");
  return *this;
}

Hash&
Hash::hash(const void* data, size_t size, HashType hash_type)
{
  std::string_view buffer(static_cast<const char*>(data), size);
  hash_buffer(buffer);

  switch (hash_type) {
  case HashType::binary:
    add_debug_text(
      Util::format_base16(static_cast<const uint8_t*>(data), size));
    break;

  case HashType::text:
    add_debug_text(buffer);
    break;
  }

  add_debug_text("\n");
  return *this;
}

Hash&
Hash::hash(std::string_view data)
{
  hash(data.data(), data.length());
  return *this;
}

Hash&
Hash::hash(int64_t x)
{
  hash_buffer(std::string_view(reinterpret_cast<const char*>(&x), sizeof(x)));
  add_debug_text(FMT("{}\n", x));
  return *this;
}

nonstd::expected<void, std::string>
Hash::hash_fd(int fd)
{
  return util::read_fd(
    fd, [this](const void* data, size_t size) { hash(data, size); });
}

nonstd::expected<void, std::string>
Hash::hash_file(const std::string& path)
{
  Fd fd(open(path.c_str(), O_RDONLY | O_BINARY));
  if (!fd) {
    LOG("Failed to open {}: {}", path, strerror(errno));
    return nonstd::make_unexpected(strerror(errno));
  }

  return hash_fd(*fd);
}

void
Hash::hash_buffer(std::string_view buffer)
{
  blake3_hasher_update(&m_hasher, buffer.data(), buffer.size());
  if (!buffer.empty() && m_debug_binary) {
    (void)fwrite(buffer.data(), 1, buffer.size(), m_debug_binary);
  }
}

void
Hash::add_debug_text(std::string_view text)
{
  if (!text.empty() && m_debug_text) {
    (void)fwrite(text.data(), 1, text.length(), m_debug_text);
  }
}
