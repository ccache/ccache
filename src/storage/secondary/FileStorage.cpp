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
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/string.hpp>

#include <third_party/nonstd/string_view.hpp>

#include <sys/stat.h> // for mode_t

namespace storage {
namespace secondary {

namespace {

class FileStorageBackend : public SecondaryStorage::Backend
{
public:
  FileStorageBackend(const Params& params);

  nonstd::expected<nonstd::optional<std::string>, Failure>
  get(const Digest& key) override;

  nonstd::expected<bool, Failure> put(const Digest& key,
                                      const std::string& value,
                                      bool only_if_missing) override;

  nonstd::expected<bool, Failure> remove(const Digest& key) override;

private:
  const std::string m_dir;
  nonstd::optional<mode_t> m_umask;
  bool m_update_mtime = false;

  std::string get_entry_path(const Digest& key) const;
};

FileStorageBackend::FileStorageBackend(const Params& params)
  : m_dir(params.url.path())
{
  ASSERT(params.url.scheme() == "file");
  if (!params.url.host().empty()) {
    throw core::Fatal(FMT(
      "invalid file path \"{}\":  specifying a host (\"{}\") is not supported",
      params.url.str(),
      params.url.host()));
  }

  for (const auto& attr : params.attributes) {
    if (attr.key == "umask") {
      m_umask =
        util::value_or_throw<core::Fatal>(util::parse_umask(attr.value));
    } else if (attr.key == "update-mtime") {
      m_update_mtime = attr.value == "true";
    } else if (!is_framework_attribute(attr.key)) {
      LOG("Unknown attribute: {}", attr.key);
    }
  }
}

nonstd::expected<nonstd::optional<std::string>,
                 SecondaryStorage::Backend::Failure>
FileStorageBackend::get(const Digest& key)
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

    util::create_cachedir_tag(m_dir);

    const auto dir = Util::dir_name(path);
    if (!Util::create_dir(dir)) {
      LOG("Failed to create directory {}: {}", dir, strerror(errno));
      return nonstd::make_unexpected(Failure::error);
    }

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
  const auto key_string = key.to_string();
  const uint8_t digits = 2;
  return FMT("{}/{:.{}}/{}", m_dir, key_string, digits, &key_string[digits]);
}

} // namespace

std::unique_ptr<SecondaryStorage::Backend>
FileStorage::create_backend(const Backend::Params& params) const
{
  return std::make_unique<FileStorageBackend>(params);
}

} // namespace secondary
} // namespace storage
