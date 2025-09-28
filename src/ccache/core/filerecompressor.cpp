// Copyright (C) 2022-2024 Joel Rosdahl and other contributors
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

#include "filerecompressor.hpp"

#include <ccache/core/atomicfile.hpp>
#include <ccache/core/cacheentry.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/format.hpp>

using util::DirEntry;

namespace core {

DirEntry
FileRecompressor::recompress(const DirEntry& dir_entry,
                             std::optional<int8_t> level,
                             KeepAtime keep_atime)
{
  core::CacheEntry::Header header(dir_entry.path());

  const int8_t wanted_level =
    level ? (*level == 0 ? core::CacheEntry::default_compression_level : *level)
          : 0;

  std::optional<DirEntry> new_dir_entry;

  if (header.compression_level != wanted_level) {
    const auto cache_file_data = util::value_or_throw<core::Error>(
      util::read_file<util::Bytes>(dir_entry.path()),
      FMT("Failed to read {}: ", dir_entry.path()));
    core::CacheEntry cache_entry(cache_file_data);
    cache_entry.verify_checksum();

    header.entry_format_version = core::CacheEntry::k_format_version;
    header.compression_type =
      level ? core::CompressionType::zstd : core::CompressionType::none;
    header.compression_level = wanted_level;

    AtomicFile new_cache_file(dir_entry.path(), AtomicFile::Mode::binary);
    new_cache_file.write(
      core::CacheEntry::serialize(header, cache_entry.payload()));
    new_cache_file.commit();
    new_dir_entry = DirEntry(dir_entry.path(), DirEntry::LogOnError::yes);
  }

  // Restore mtime/atime to keep cache LRU cleanup working as expected:
  if (keep_atime == KeepAtime::yes || new_dir_entry) {
    util::set_timestamps(
      dir_entry.path(), dir_entry.mtime(), dir_entry.atime());
  }

  m_content_size += util::likely_size_on_disk(header.entry_size);
  m_old_size += dir_entry.size_on_disk();
  m_new_size += new_dir_entry.value_or(dir_entry).size_on_disk();

  return new_dir_entry.value_or(dir_entry);
}

uint64_t
FileRecompressor::content_size() const
{
  return m_content_size;
}

uint64_t
FileRecompressor::old_size() const
{
  return m_old_size;
}

uint64_t
FileRecompressor::new_size() const
{
  return m_new_size;
}

} // namespace core
