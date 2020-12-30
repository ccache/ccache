// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include "core/system.hpp"

#include "CacheFile.hpp"
#include "core/Util.hpp"

#include "third_party/nonstd/optional.hpp"
#include "third_party/nonstd/string_view.hpp"

#include <algorithm>
#include <functional>
#include <ios>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class Context;

namespace Util {

// Clone, hard link or copy a file from `source` to `dest` depending on settings
// in `ctx`. If cloning or hard linking cannot and should not be done the file
// will be copied instead. Throws `Error` on error.
void clone_hard_link_or_copy_file(const Context& ctx,
                                  const std::string& source,
                                  const std::string& dest,
                                  bool via_tmp_file = false);

// Make a relative path from current working directory to `path` if `path` is
// under the base directory.
std::string make_relative_path(const Context& ctx, nonstd::string_view path);

// Send `text` to STDERR_FILENO, optionally stripping ANSI color sequences if
// `ctx.args_info.strip_diagnostics_colors` is true and rewriting paths to
// absolute if `ctx.config.absolute_paths_in_stderr` is true. Throws `Error` on
// error.
void send_to_stderr(const Context& ctx, const std::string& text);

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

} // namespace Util
