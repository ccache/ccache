// Copyright (C) 2023-2024 Joel Rosdahl and other contributors
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

#include <tl/expected.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace util {

// Expand all instances of $VAR or ${VAR}, where VAR is an environment variable,
// in `str`.
tl::expected<std::string, std::string>
expand_environment_variables(const std::string& str);

// Get value of environment variable `name` as a path.
std::optional<std::filesystem::path> getenv_path(const char* name);

// Get value of environment variable `name` as a vector of paths where the value
// is delimited by ';' on Windows and ':' on other systems..
std::vector<std::filesystem::path> getenv_path_list(const char* name);

// Set environment variable `name` to `value`.
void setenv(const std::string& name, const std::string& value);

// Unset environment variable `name`.
void unsetenv(const std::string& name);

} // namespace util
