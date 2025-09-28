// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include <ccache/util/direntry.hpp>

#include <filesystem>
#include <functional>
#include <vector>

namespace storage::local {

using ProgressReceiver = std::function<void(double progress)>;
using SubdirVisitor = std::function<void(uint8_t subdir_index)>;
using SubdirProgressVisitor = std::function<void(
  uint8_t subdir_index, const ProgressReceiver& progress_receiver)>;

// Call `visitor` for each subdirectory (0-9a-f) in `cache_dir`.
void for_each_cache_subdir(const SubdirVisitor& visitor);
void for_each_cache_subdir(const ProgressReceiver& progress_receiver,
                           const SubdirProgressVisitor& visitor);

void for_each_level_1_and_2_stats_file(
  const std::filesystem::path& cache_dir,
  const std::function<void(const std::filesystem::path& path)> function);

// Get a list of files in a subdirectory of the cache.
//
// The function works under the assumption that directory entries with one
// character names (except ".") are subdirectories and that there are no other
// subdirectories.
//
// Files ignored:
// - CACHEDIR.TAG
// - stats
// - .nfs* (temporary NFS files that may be left for open but deleted files).
std::vector<util::DirEntry>
get_cache_dir_files(const std::filesystem::path& dir);

} // namespace storage::local
