// Copyright (C) 2022 Joel Rosdahl and other contributors
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

#include "FileRecompressor.hpp"

#include <AtomicFile.hpp>
#include <Util.hpp>
#include <core/CacheEntry.hpp>
#include <core/exceptions.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>

namespace core {

int64_t
FileRecompressor::recompress(const std::string& cache_file,
                             const std::optional<int8_t> level)
{
  core::CacheEntry::Header header(cache_file);

  const int8_t wanted_level =
    level ? (*level == 0 ? core::CacheEntry::default_compression_level : *level)
          : 0;
  const auto old_stat = Stat::lstat(cache_file, Stat::OnError::log);
  Stat new_stat(old_stat);

  if (header.compression_level != wanted_level) {
    const auto cache_file_data = util::value_or_throw<core::Error>(
      util::read_file<util::Bytes>(cache_file),
      FMT("Failed to read {}: ", cache_file));
    core::CacheEntry cache_entry(cache_file_data);
    cache_entry.verify_checksum();

    header.entry_format_version = core::CacheEntry::k_format_version;
    header.compression_type =
      level ? core::CompressionType::zstd : core::CompressionType::none;
    header.compression_level = wanted_level;

    AtomicFile new_cache_file(cache_file, AtomicFile::Mode::binary);
    new_cache_file.write(
      core::CacheEntry::serialize(header, cache_entry.payload()));
    new_cache_file.commit();
    new_stat = Stat::lstat(cache_file, Stat::OnError::log);

    // Restore mtime/atime to keep cache LRU cleanup working as expected:
    util::set_timestamps(cache_file, old_stat.mtime(), old_stat.atime());
  }

  m_content_size += header.entry_size;
  m_old_size += old_stat.size_on_disk();
  m_new_size += new_stat.size_on_disk();

  return Util::size_change_kibibyte(old_stat, new_stat);
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
