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

#pragma once

#include <ccache/core/types.hpp>
#include <ccache/hash.hpp>
#include <ccache/storage/local/localstorage.hpp>
#include <ccache/storage/remote/remotestorage.hpp>
#include <ccache/storage/types.hpp>
#include <ccache/util/bytes.hpp>

#include <nonstd/span.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace storage {

constexpr auto k_redacted_password = "********";

std::vector<std::string> get_features();

struct RemoteStorageBackendEntry;
struct RemoteStorageEntry;

std::string get_redacted_url_str_for_logging(const Url& url);

class Storage
{
public:
  Storage(const Config& config);
  ~Storage();

  void initialize();
  void finalize();

  local::LocalStorage local;

  using EntryReceiver = std::function<bool(util::Bytes&&)>;

  void get(const Hash::Digest& key,
           core::CacheEntryType type,
           const EntryReceiver& entry_receiver);

  void put(const Hash::Digest& key,
           core::CacheEntryType type,
           nonstd::span<const uint8_t> value);

  void remove(const Hash::Digest& key, core::CacheEntryType type);

  bool has_remote_storage() const;
  std::string get_remote_storage_config_for_logging() const;

private:
  const Config& m_config;
  std::vector<std::unique_ptr<RemoteStorageEntry>> m_remote_storages;

  void add_remote_storages();

  void mark_backend_as_failed(RemoteStorageBackendEntry& backend_entry,
                              remote::RemoteStorage::Backend::Failure failure);

  RemoteStorageBackendEntry* get_backend(RemoteStorageEntry& entry,
                                         const Hash::Digest& key,
                                         std::string_view operation_description,
                                         const bool for_writing);

  void get_from_remote_storage(const Hash::Digest& key,
                               core::CacheEntryType type,
                               const EntryReceiver& entry_receiver);

  void put_in_remote_storage(const Hash::Digest& key,
                             nonstd::span<const uint8_t> value,
                             Overwrite overwrite);

  void remove_from_remote_storage(const Hash::Digest& key);
};

} // namespace storage
