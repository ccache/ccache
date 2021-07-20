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

#include <storage/primary/CacheFile.hpp>

#include <functional>
#include <string>
#include <vector>

namespace storage {
namespace primary {

using ProgressReceiver = std::function<void(double progress)>;
using SubdirVisitor = std::function<void(
  const std::string& dir_path, const ProgressReceiver& progress_receiver)>;

// Call a function for each subdir (0-9a-f) in the cache.
//
// Parameters:
// - cache_dir: Path to the cache directory.
// - visitor: Function to call with directory path and progress_receiver as
//   arguments.
// - progress_receiver: Function that will be called for progress updates.
void for_each_level_1_subdir(const std::string& cache_dir,
                             const SubdirVisitor& visitor,
                             const ProgressReceiver& progress_receiver);

// Get a list of files in a level 1 subdirectory of the cache.
//
// The function works under the assumption that directory entries with one
// character names (except ".") are subdirectories and that there are no other
// subdirectories.
//
// Files ignored:
// - CACHEDIR.TAG
// - stats
// - .nfs* (temporary NFS files that may be left for open but deleted files).
//
// Parameters:
// - dir: The directory to traverse recursively.
// - progress_receiver: Function that will be called for progress updates.
std::vector<CacheFile>
get_level_1_files(const std::string& dir,
                  const ProgressReceiver& progress_receiver);

} // namespace primary
} // namespace storage
