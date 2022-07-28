// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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

class Context;
class Hash;

#include "Digest.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Depfile {

std::string escape_filename(std::string_view filename);

std::optional<std::string>
rewrite_source_paths(const Context& ctx, const std::string& file_content);

void make_paths_relative_in_output_dep(const Context& ctx);

std::vector<std::string> tokenize(std::string_view file_content);

} // namespace Depfile
