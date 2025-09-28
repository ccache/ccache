// Copyright (C) 2020-2024 Joel Rosdahl and other contributors
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

#include "hash.hpp"

#include <ccache/util/fd.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/wincompat.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

namespace fs = util::filesystem;

const uint8_t HASH_DELIMITER[] = {0, 'c', 'C', 'a', 'C', 'h', 'E', 0};

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

Hash::Digest
Hash::digest() const
{
  // Note that blake3_hasher_finalize doesn't modify the hasher itself, thus it
  // is possible to finalize again after more data has been added.
  Digest digest;
  blake3_hasher_finalize(&m_hasher, digest.data(), digest.size());
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
Hash::hash(nonstd::span<const uint8_t> data)
{
  hash_buffer(data);
  add_debug_text(data);
  add_debug_text("\n");
  return *this;
}

Hash&
Hash::hash(int64_t x)
{
  hash_buffer(std::string_view(reinterpret_cast<const char*>(&x), sizeof(x)));
  add_debug_text(FMT("{}\n", x));
  return *this;
}

tl::expected<void, std::string>
Hash::hash_fd(int fd)
{
  return util::read_fd(fd, [this](auto data) { hash(data); });
}

tl::expected<void, std::string>
Hash::hash_file(const fs::path& path)
{
  util::Fd fd(open(util::pstr(path).c_str(), O_RDONLY | O_BINARY));
  if (!fd) {
    LOG("Failed to open {}: {}", path, strerror(errno));
    return tl::unexpected(strerror(errno));
  }

  return hash_fd(*fd);
}

void
Hash::hash_buffer(nonstd::span<const uint8_t> buffer)
{
  blake3_hasher_update(&m_hasher, buffer.data(), buffer.size());
  if (!buffer.empty() && m_debug_binary) {
    (void)fwrite(buffer.data(), 1, buffer.size(), m_debug_binary);
  }
}

void
Hash::hash_buffer(std::string_view buffer)
{
  hash_buffer(util::to_span(buffer));
}

void
Hash::add_debug_text(nonstd::span<const uint8_t> text)
{
  if (!text.empty() && m_debug_text) {
    (void)fwrite(text.data(), 1, text.size(), m_debug_text);
  }
}

void
Hash::add_debug_text(std::string_view text)
{
  add_debug_text(util::to_span(text));
}
