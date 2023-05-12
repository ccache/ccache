// Copyright (C) 2023 Joel Rosdahl and other contributors
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

#include <string>

namespace util {

// Expand all instances of $VAR or ${VAR}, where VAR is an environment variable,
// in `str`. Throws `core::Error` if one of the referenced variables is not set
// or a closing '}' is missing after '${'.
[[nodiscard]] std::string expand_environment_variables(const std::string& str);

// Set environment variable `name` to `value`.
void setenv(const std::string& name, const std::string& value);

// Unset environment variable `name`.
void unsetenv(const std::string& name);

} // namespace util
