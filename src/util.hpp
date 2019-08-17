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

#include <string>

namespace util {

// Read file data as a string.
std::string read_file(const std::string& path);

// Return true if prefix is a prefix of string.
bool starts_with(const std::string& string, const std::string& prefix);

// Strip whitespace from left and right side of a string.
[[gnu::warn_unused_result]] std::string
strip_whitespace(const std::string& string);

// Convert a string to lowercase.
[[gnu::warn_unused_result]] std::string to_lowercase(const std::string& string);

// Write file data from a string.
void write_file(const std::string& path,
                const std::string& data,
                bool binary = false);

} // namespace util
