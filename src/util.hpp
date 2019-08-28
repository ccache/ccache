// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#include "system.hpp"

#include "CacheFile.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace util {

typedef std::function<void(double)> ProgressReceiver;
typedef std::function<void(std::shared_ptr<CacheFile>)> CacheFileVisitor;
typedef std::function<void(const std::string& /*dir_path*/,
                           const ProgressReceiver& /*progress_receiver*/)>
  SubdirVisitor;

// Get base name of path.
std::string base_name(const std::string& path);

// Create a directory if needed, including its parents if needed.
//
// Returns true if the directory exists or could be created, otherwise false.
bool create_dir(const std::string& dir);

// Get directory name of path.
std::string dir_name(const std::string& path);

// Return true if suffix is a suffix of string.
bool ends_with(const std::string& string, const std::string& suffix);

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
// - files: Found files.
void get_level_1_files(const std::string& dir,
                       const ProgressReceiver& progress_receiver,
                       std::vector<std::shared_ptr<CacheFile>>& files);

// Read file data as a string.
//
// Throws Error on error.
std::string read_file(const std::string& path);

// Return true if prefix is a prefix of string.
bool starts_with(const std::string& string, const std::string& prefix);

// Strip whitespace from left and right side of a string.
[[gnu::warn_unused_result]] std::string
strip_whitespace(const std::string& string);

// Convert a string to lowercase.
[[gnu::warn_unused_result]] std::string to_lowercase(const std::string& string);

// Write file data from a string.
//
// Throws Error on error.
void write_file(const std::string& path,
                const std::string& data,
                bool binary = false);

} // namespace util
