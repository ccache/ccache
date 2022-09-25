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

#pragma once

#include <core/types.hpp>
#include <storage/local/LocalStorage.hpp>
#include <storage/remote/RemoteStorage.hpp>
#include <storage/types.hpp>
#include <util/Bytes.hpp>

#include <third_party/nonstd/span.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class Digest;

namespace storage {

std::string get_features();

struct RemoteStorageBackendEntry;
struct RemoteStorageEntry;

class Storage
{
public:
  Storage(const Config& config);
  ~Storage();

  void initialize();
  void finalize();

  local::LocalStorage local;

  using EntryReceiver = std::function<bool(util::Bytes&&)>;

  void get(const Digest& key,
           core::CacheEntryType type,
           const EntryReceiver& entry_receiver);

  void put(const Digest& key,
           core::CacheEntryType type,
           nonstd::span<const uint8_t> value);

  void remove(const Digest& key, core::CacheEntryType type);

  bool has_remote_storage() const;
  std::string get_remote_storage_config_for_logging() const;

private:
  const Config& m_config;
  std::vector<std::unique_ptr<RemoteStorageEntry>> m_remote_storages;

  void add_remote_storages();

  void mark_backend_as_failed(RemoteStorageBackendEntry& backend_entry,
                              remote::RemoteStorage::Backend::Failure failure);

  RemoteStorageBackendEntry* get_backend(RemoteStorageEntry& entry,
                                         const Digest& key,
                                         std::string_view operation_description,
                                         const bool for_writing);

  void get_from_remote_storage(const Digest& key,
                               const EntryReceiver& entry_receiver);

  void put_in_remote_storage(const Digest& key,
                             nonstd::span<const uint8_t> value,
                             bool only_if_missing);

  void remove_from_remote_storage(const Digest& key);
};

} // namespace storage
