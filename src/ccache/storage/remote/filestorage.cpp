// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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

#include "filestorage.hpp"

#include <ccache/core/atomicfile.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/util/assertions.hpp>
#include <ccache/util/bytes.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/umaskscope.hpp>

#include <sys/stat.h> // for mode_t

#include <string_view>

namespace fs = util::filesystem;

using util::DirEntry;

namespace storage::remote {

namespace {

class FileStorageBackend : public RemoteStorage::Backend
{
public:
  FileStorageBackend(const Url& url,
                     const std::vector<Backend::Attribute>& attributes);

  tl::expected<std::optional<util::Bytes>, Failure>
  get(const Hash::Digest& key) override;

  tl::expected<bool, Failure> put(const Hash::Digest& key,
                                  nonstd::span<const uint8_t> value,
                                  Overwrite overwrite) override;

  tl::expected<bool, Failure> remove(const Hash::Digest& key) override;

private:
  enum class Layout : uint8_t { flat, subdirs };

  std::string m_dir;
  std::optional<mode_t> m_umask;
  bool m_update_mtime = false;
  Layout m_layout = Layout::subdirs;

  std::string get_entry_path(const Hash::Digest& key) const;
};

FileStorageBackend::FileStorageBackend(
  const Url& url, const std::vector<Backend::Attribute>& attributes)
{
  ASSERT(url.scheme() == "file");

  const auto& host = url.host();
#ifdef _WIN32
  m_dir = util::replace_all(url.path(), "/", "\\");
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
          url.str()));
  }
  m_dir = url.path();
#endif

  for (const auto& attr : attributes) {
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

  if (!DirEntry(path).exists()) {
    // Don't log failure if the entry doesn't exist.
    return std::nullopt;
  }

  if (m_update_mtime) {
    // Update modification timestamp for potential LRU cleanup by some external
    // mechanism.
    util::set_timestamps(path);
  }

  return util::read_file<util::Bytes>(path).transform_error(
    [&](const auto& error) {
      LOG("Failed to read {}: {}", path, error);
      return Failure::error;
    });
}

tl::expected<bool, RemoteStorage::Backend::Failure>
FileStorageBackend::put(const Hash::Digest& key,
                        const nonstd::span<const uint8_t> value,
                        const Overwrite overwrite)
{
  const auto path = get_entry_path(key);

  if (overwrite == Overwrite::no && DirEntry(path).exists()) {
    LOG("{} already in cache", path);
    return false;
  }

  {
    util::UmaskScope umask_scope(m_umask);

    const fs::path dir = fs::path(path).parent_path();
    if (auto result = fs::create_directories(dir); !result) {
      LOG("Failed to create directory {}: {}", dir, result.error().message());
      return tl::unexpected(Failure::error);
    }

    util::create_cachedir_tag(m_dir);

    LOG("Writing {}", path);
    try {
      core::AtomicFile file(path, core::AtomicFile::Mode::binary);
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
  auto result = util::remove_nfs_safe(get_entry_path(key));
  if (result) {
    return *result;
  } else {
    return tl::unexpected(RemoteStorage::Backend::Failure::error);
  }
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
FileStorage::create_backend(
  const Url& url, const std::vector<Backend::Attribute>& attributes) const
{
  return std::make_unique<FileStorageBackend>(url, attributes);
}

} // namespace storage::remote
