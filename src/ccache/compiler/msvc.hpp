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
#include <utility>
#include <vector>

class Context;

namespace compiler {

std::vector<std::string_view>
get_includes_from_msvc_show_includes(std::string_view file_content,
                                     std::string_view prefix);

util::Bytes strip_includes_from_msvc_show_includes(const Context& ctx,
                                                   util::Bytes&& stdout_data);

using MsvcPathMap = std::pair<std::filesystem::path, std::string>;

// Parse the value of /pathmap:<old>=<new>. Returns std::nullopt for maps that
// cl doesn't apply to the paths it embeds: cl only matches an old path written
// with backslashes, and a map only covers whole path components if the old path
// ends with a backslash.
std::optional<MsvcPathMap> parse_msvc_path_map(std::string_view value);

// Return `path` with the first map in `maps` applied that matches it, the way
// cl applies /pathmap, or std::nullopt if no map matches.
std::optional<std::string>
apply_msvc_path_maps(const std::filesystem::path& path,
                     const std::vector<MsvcPathMap>& maps);

} // namespace compiler
