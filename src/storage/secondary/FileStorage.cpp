// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#include "FileStorage.hpp"

#include <AtomicFile.hpp>
#include <Digest.hpp>
#include <Logging.hpp>
#include <UmaskScope.hpp>
#include <Util.hpp>
#include <assertions.hpp>
#include <fmtmacros.hpp>
#include <util/file_utils.hpp>
#include <util/string_utils.hpp>

#include <third_party/nonstd/string_view.hpp>

namespace storage {
namespace secondary {

static std::string
parse_url(const Url& url)
{
  ASSERT(url.scheme() == "file");
  const auto& dir = url.path();
  if (!Util::starts_with(dir, "/")) {
    throw Error("invalid file path \"{}\" - directory must start with a slash",
                dir);
  }
  return dir;
}

static nonstd::optional<mode_t>
parse_umask(const AttributeMap& attributes)
{
  const auto it = attributes.find("umask");
  if (it == attributes.end()) {
    return nonstd::nullopt;
  }

  const auto umask = util::parse_umask(it->second);
  if (umask) {
    return *umask;
  } else {
    LOG("Error: {}", umask.error());
    return nonstd::nullopt;
  }
}

static bool
parse_update_mtime(const AttributeMap& attributes)
{
  const auto it = attributes.find("update-mtime");
  return it != attributes.end() && it->second == "true";
}

FileStorage::FileStorage(const Url& url, const AttributeMap& attributes)
  : m_dir(parse_url(url)),
    m_umask(parse_umask(attributes)),
    m_update_mtime(parse_update_mtime(attributes))
{
}

nonstd::expected<nonstd::optional<std::string>, SecondaryStorage::Error>
FileStorage::get(const Digest& key)
{
  const auto path = get_entry_path(key);
  const bool exists = Stat::stat(path);

  if (!exists) {
    // Don't log failure if the entry doesn't exist.
    return nonstd::nullopt;
  }

  if (m_update_mtime) {
    // Update modification timestamp for potential LRU cleanup by some external
    // mechanism.
    Util::update_mtime(path);
  }

  try {
    LOG("Reading {}", path);
    return Util::read_file(path);
  } catch (const ::Error& e) {
    LOG("Failed to read {}: {}", path, e.what());
    return nonstd::make_unexpected(Error::error);
  }
}

nonstd::expected<bool, SecondaryStorage::Error>
FileStorage::put(const Digest& key,
                 const std::string& value,
                 const bool only_if_missing)
{
  const auto path = get_entry_path(key);

  if (only_if_missing && Stat::stat(path)) {
    LOG("{} already in cache", path);
    return false;
  }

  {
    UmaskScope umask_scope(m_umask);

    util::create_cachedir_tag(m_dir);

    const auto dir = Util::dir_name(path);
    if (!Util::create_dir(dir)) {
      LOG("Failed to create directory {}: {}", dir, strerror(errno));
      return nonstd::make_unexpected(Error::error);
    }

    LOG("Writing {}", path);
    try {
      AtomicFile file(path, AtomicFile::Mode::binary);
      file.write(value);
      file.commit();
      return true;
    } catch (const ::Error& e) {
      LOG("Failed to write {}: {}", path, e.what());
      return nonstd::make_unexpected(Error::error);
    }
  }
}

nonstd::expected<bool, SecondaryStorage::Error>
FileStorage::remove(const Digest& key)
{
  return Util::unlink_safe(get_entry_path(key));
}

std::string
FileStorage::get_entry_path(const Digest& key) const
{
  const auto key_string = key.to_string();
  const uint8_t digits = 2;
  return FMT("{}/{:.{}}/{}", m_dir, key_string, digits, &key_string[digits]);
}

} // namespace secondary
} // namespace storage
