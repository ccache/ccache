// Copyright (C) 2022-2025 Joel Rosdahl and other contributors
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

#include <ccache/util/bytes.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Context;

namespace compiler {

std::vector<std::string_view>
get_includes_from_msvc_show_includes(std::string_view file_content,
                                     std::string_view prefix);

util::Bytes strip_includes_from_msvc_show_includes(const Context& ctx,
                                                   util::Bytes&& stdout_data);

// A /pathmap:<old>=<new> value.
struct MsvcPathMap
{
  std::string old_path; // As given, ending with a path separator.
  std::string new_path; // As given.
};

// Parses a /pathmap value. Returns std::nullopt without "=", or if the old path
// doesn't end with a path separator or isn't ASCII.
std::optional<MsvcPathMap> parse_msvc_path_map(std::string_view value);

// Returns `path` mapped through `map`, or std::nullopt unless `path` is under
// the old path: `path` with forward slashes replaced by backslashes and a
// backslash appended starts with the old path, compared ASCII
// case-insensitively.
std::optional<std::string> apply_msvc_path_map(const MsvcPathMap& map,
                                               std::string_view path);

// Returns the root map R=N if `base_dirs` is a single directory and the
// /pathmap values `maps` are exactly "R=N" and the same with every backslash
// replaced by a slash, in either order, where R is the base directory with
// backslashes and one trailing backslash. Returns std::nullopt otherwise.
std::optional<MsvcPathMap>
select_msvc_root_map(const std::vector<std::string>& maps,
                     const std::vector<std::filesystem::path>& base_dirs);

// Returns the lexically normal directory of the object file `output_obj`
// relative to `cwd`.
std::filesystem::path msvc_object_dir(const std::filesystem::path& cwd,
                                      const std::filesystem::path& output_obj);

// Returns whether `output` contains `root` (ending with a backslash) without
// its trailing backslash, compared ASCII case-insensitively with slashes taken
// as backslashes. A longer directory name starting with the root also counts.
bool msvc_output_names_root(std::string_view root, std::string_view output);

} // namespace compiler
