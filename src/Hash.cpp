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

#include "Hash.hpp"

#include "Fd.hpp"
#include "Logging.hpp"

using Logging::log;
using nonstd::string_view;

const string_view HASH_DELIMITER("\000cCaChE\000", 8);

Hash::Hash()
{
  blake3_hasher_init(&m_hasher);
}

void
Hash::enable_debug(string_view section_name,
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
Hash::hash_delimiter(string_view type)
{
  hash_buffer(HASH_DELIMITER);
  hash_buffer(type);
  hash_buffer(string_view("", 1)); // NUL
  add_debug_text("### ");
  add_debug_text(type);
  add_debug_text("\n");
  return *this;
}

Hash&
Hash::hash(const void* data, size_t size, HashType hash_type)
{
  const string_view buffer(static_cast<const char*>(data), size);
  hash_buffer(buffer);

  switch (hash_type) {
  case HashType::binary:
    add_debug_text(Util::format_hex(static_cast<const uint8_t*>(data), size));
    break;

  case HashType::text:
    add_debug_text(buffer);
    break;
  }

  add_debug_text("\n");
  return *this;
}

Hash&
Hash::hash(nonstd::string_view data)
{
  hash(data.data(), data.length());
  return *this;
}

Hash&
Hash::hash(int64_t x)
{
  hash_buffer(string_view(reinterpret_cast<const char*>(&x), sizeof(x)));
  add_debug_text(fmt::format("{}\n", x));
  return *this;
}

bool
Hash::hash_fd(int fd)
{
  return Util::read_fd(
    fd, [=](const void* data, size_t size) { hash(data, size); });
}

bool
Hash::hash_file(const std::string& path)
{
  const Fd fd(open(path.c_str(), O_RDONLY | O_BINARY));
  if (!fd) {
    log("Failed to open {}: {}", path, strerror(errno));
    return false;
  }

  const bool ret = hash_fd(*fd);
  return ret;
}

void
Hash::hash_buffer(string_view buffer)
{
  blake3_hasher_update(&m_hasher, buffer.data(), buffer.size());
  if (!buffer.empty() && m_debug_binary) {
    (void)fwrite(buffer.data(), 1, buffer.size(), m_debug_binary);
  }
}

void
Hash::add_debug_text(string_view text)
{
  if (!text.empty() && m_debug_text) {
    (void)fwrite(text.data(), 1, text.length(), m_debug_text);
  }
}
