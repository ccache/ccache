// Copyright (C) 2021-2022 Joel Rosdahl and other contributors
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
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/string.hpp>

#include <sys/stat.h> // for mode_t

#include <string_view>

namespace storage::secondary {

namespace {

class FileStorageBackend : public SecondaryStorage::Backend
{
public:
  FileStorageBackend(const Params& params);

  nonstd::expected<std::optional<std::string>, Failure>
  get(const Digest& key) override;

  nonstd::expected<bool, Failure> put(const Digest& key,
                                      const std::string& value,
                                      bool only_if_missing) override;

  nonstd::expected<bool, Failure> remove(const Digest& key) override;

private:
  enum class Layout { flat, subdirs };

  std::string m_dir;
  std::optional<mode_t> m_umask;
  bool m_update_mtime = false;
  Layout m_layout = Layout::subdirs;

  std::string get_entry_path(const Digest& key) const;
};

FileStorageBackend::FileStorageBackend(const Params& params)
{
  ASSERT(params.url.scheme() == "file");

  const auto& host = params.url.host();
#ifdef _WIN32
  m_dir = util::replace_all(params.url.path(), "/", "\\");
  if (!host.empty()) {
    m_dir = FMT("\\\\{}{}", host, m_dir);
  }
#else
  if (!host.empty() && host != "localhost") {
    throw core::Fatal(
      FMT("invalid file URL \"{}\": specifying a host other than localhost is"
          " not supported",
          params.url.str()));
  }
  m_dir = params.url.path();
#endif

  for (const auto& attr : params.attributes) {
    if (attr.key == "layout") {
      if (attr.value == "flat") {
        m_layout = Layout::flat;
      } else if (attr.value == "subdirs") {
        m_layout = Layout::subdirs;
      } else {
        LOG("Unknown layout: {}", attr.value);
      }
    } else if (attr.key == "umask") {
      m_umask =
        util::value_or_throw<core::Fatal>(util::parse_umask(attr.value));
    } else if (attr.key == "update-mtime") {
      m_update_mtime = attr.value == "true";
    } else if (!is_framework_attribute(attr.key)) {
      LOG("Unknown attribute: {}", attr.key);
    }
  }
}

nonstd::expected<std::optional<std::string>, SecondaryStorage::Backend::Failure>
FileStorageBackend::get(const Digest& key)
{
  const auto path = get_entry_path(key);
  const bool exists = Stat::stat(path);

  if (!exists) {
    // Don't log failure if the entry doesn't exist.
    return std::nullopt;
  }

  if (m_update_mtime) {
    // Update modification timestamp for potential LRU cleanup by some external
    // mechanism.
    util::set_timestamps(path);
  }

  try {
    LOG("Reading {}", path);
    return Util::read_file(path);
  } catch (const core::Error& e) {
    LOG("Failed to read {}: {}", path, e.what());
    return nonstd::make_unexpected(Failure::error);
  }
}

nonstd::expected<bool, SecondaryStorage::Backend::Failure>
FileStorageBackend::put(const Digest& key,
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

    const auto dir = Util::dir_name(path);
    if (!Util::create_dir(dir)) {
      LOG("Failed to create directory {}: {}", dir, strerror(errno));
      return nonstd::make_unexpected(Failure::error);
    }

    util::create_cachedir_tag(m_dir);

    LOG("Writing {}", path);
    try {
      AtomicFile file(path, AtomicFile::Mode::binary);
      file.write(value);
      file.commit();
      return true;
    } catch (const core::Error& e) {
      LOG("Failed to write {}: {}", path, e.what());
      return nonstd::make_unexpected(Failure::error);
    }
  }
}

nonstd::expected<bool, SecondaryStorage::Backend::Failure>
FileStorageBackend::remove(const Digest& key)
{
  return Util::unlink_safe(get_entry_path(key));
}

std::string
FileStorageBackend::get_entry_path(const Digest& key) const
{
  switch (m_layout) {
  case Layout::flat:
    return FMT("{}/{}", m_dir, key.to_string());

  case Layout::subdirs: {
    const auto key_str = key.to_string();
    const uint8_t digits = 2;
    ASSERT(key_str.length() > digits);
    return FMT("{}/{:.{}}/{}", m_dir, key_str, digits, &key_str[digits]);
  }
  }

  ASSERT(false);
}

} // namespace

std::unique_ptr<SecondaryStorage::Backend>
FileStorage::create_backend(const Backend::Params& params) const
{
  return std::make_unique<FileStorageBackend>(params);
}

} // namespace storage::secondary
