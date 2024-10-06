// Copyright (C) 2010-2024 Joel Rosdahl and other contributors
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

#include <optional>
#include <string_view>

bool compopt_short(bool (*fn)(std::string_view option),
                   std::string_view option);
bool compopt_affects_cpp_output(std::string_view option);
bool compopt_affects_compiler_output(std::string_view option);
bool compopt_too_hard(std::string_view option);
bool compopt_too_hard_for_direct_mode(std::string_view option);
bool compopt_takes_path(std::string_view option);
bool compopt_takes_arg(std::string_view option);
bool compopt_takes_concat_arg(std::string_view option);
bool compopt_prefix_affects_cpp_output(std::string_view option);
bool compopt_prefix_affects_compiler_output(std::string_view option);
std::optional<std::string_view>
compopt_prefix_takes_path(std::string_view option);
