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

#include <tl/expected.hpp>

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

// Rewrite the paths below base_dir in the JSON file that /sourceDependencies
// produces so that they are relative to the working directory. Returns nothing
// when no path was rewritten.
std::optional<std::string>
rewrite_paths_in_source_dependencies(const Context& ctx,
                                     std::string_view file_content);

tl::expected<void, std::string>
make_paths_relative_in_source_dependencies(const Context& ctx);

} // namespace compiler
