// Copyright (C) 2021-2023 Joel Rosdahl and other contributors
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
#include <Logging.hpp>
#include <Stat.hpp>
#include <Util.hpp>
#include <assertions.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <util/Bytes.hpp>
#include <util/UmaskScope.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>
#include <util/string.hpp>

#include <sys/stat.h> // for mode_t

#include <string_view>

namespace fs = util::filesystem;

namespace storage::remote {

namespace {

class FileStorageBackend : public RemoteStorage::Backend
{
public:
  FileStorageBackend(const Params& params);

  tl::expected<std::optional<util::Bytes>, Failure>
  get(const Hash::Digest& key) override;

  tl::expected<bool, Failure> put(const Hash::Digest& key,
                                  nonstd::span<const uint8_t> value,
                                  bool only_if_missing) override;

  tl::expected<bool, Failure> remove(const Hash::Digest& key) override;

private:
  enum class Layout { flat, subdirs };

  std::string m_dir;
  std::optional<mode_t> m_umask;
  bool m_update_mtime = false;
  Layout m_layout = Layout::subdirs;

  std::string get_entry_path(const Hash::Digest& key) const;
};

FileStorageBackend::FileStorageBackend(const Params& params)
{
  ASSERT(params.url.scheme() == "file");

  const auto& host = params.url.host();
#ifdef _WIN32
  m_dir = util::replace_all(params.url.path(), "/", "\\");
  if (m_dir.length() >= 3 && m_dir[0] == '\\' && m_dir[2] == ':') {
    // \X:\foo\bar -> X:\foo\bar according to RFC 8089 appendix E.2.
    m_dir = m_dir.substr(1);
  }
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

tl::expected<std::optional<util::Bytes>, RemoteStorage::Backend::Failure>
FileStorageBackend::get(const Hash::Digest& key)
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

  auto value = util::read_file<util::Bytes>(path);
  if (!value) {
    LOG("Failed to read {}: {}", path, value.error());
    return tl::unexpected(Failure::error);
  }
  return std::move(*value);
}

tl::expected<bool, RemoteStorage::Backend::Failure>
FileStorageBackend::put(const Hash::Digest& key,
                        const nonstd::span<const uint8_t> value,
                        const bool only_if_missing)
{
  const auto path = get_entry_path(key);

  if (only_if_missing && Stat::stat(path)) {
    LOG("{} already in cache", path);
    return false;
  }

  {
    util::UmaskScope umask_scope(m_umask);

    const auto dir = Util::dir_name(path);
    if (auto result = fs::create_directories(dir); !result) {
      LOG("Failed to create directory {}: {}", dir, result.error().message());
      return tl::unexpected(Failure::error);
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
      return tl::unexpected(Failure::error);
    }
  }
}

tl::expected<bool, RemoteStorage::Backend::Failure>
FileStorageBackend::remove(const Hash::Digest& key)
{
  auto entry_path = get_entry_path(key);
  auto result = util::remove_nfs_safe(entry_path);
  if (!result) {
    LOG("Failed to remove {}: {}", entry_path, result.error().message());
    return tl::unexpected(RemoteStorage::Backend::Failure::error);
  }
  return *result;
}

std::string
FileStorageBackend::get_entry_path(const Hash::Digest& key) const
{
  switch (m_layout) {
  case Layout::flat:
    return FMT("{}/{}", m_dir, util::format_digest(key));

  case Layout::subdirs: {
    const auto key_str = util::format_digest(key);
    const uint8_t digits = 2;
    ASSERT(key_str.length() > digits);
    return FMT("{}/{:.{}}/{}", m_dir, key_str, digits, &key_str[digits]);
  }
  }

  ASSERT(false);
}

} // namespace

std::unique_ptr<RemoteStorage::Backend>
FileStorage::create_backend(const Backend::Params& params) const
{
  return std::make_unique<FileStorageBackend>(params);
}

} // namespace storage::remote
