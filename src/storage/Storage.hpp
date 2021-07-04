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

#pragma once

#include "types.hpp"

#include <core/types.hpp>
#include <storage/SecondaryStorage.hpp>
#include <storage/primary/PrimaryStorage.hpp>

#include <third_party/nonstd/optional.hpp>

#include <functional>
#include <string>
#include <vector>

class Digest;

namespace storage {

class Storage
{
public:
  Storage(const Config& config);
  ~Storage();

  void initialize();
  void finalize();

  primary::PrimaryStorage& primary();

  // Returns a path to a file containing the value.
  nonstd::optional<std::string> get(const Digest& key,
                                    core::CacheEntryType type);

  bool put(const Digest& key,
           core::CacheEntryType type,
           const storage::CacheEntryWriter& entry_writer);

  void remove(const Digest& key, core::CacheEntryType type);

private:
  struct SecondaryStorageEntry
  {
    std::unique_ptr<storage::SecondaryStorage> backend;
    std::string url;
    bool read_only = false;
  };

  const Config& m_config;
  primary::PrimaryStorage m_primary_storage;
  std::vector<SecondaryStorageEntry> m_secondary_storages;
  std::vector<std::string> m_tmp_files;

  void add_secondary_storages();
};

} // namespace storage
