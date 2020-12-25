// Copyright (C) 2009-2020 Joel Rosdahl and other contributors
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

#include "third_party/nonstd/string_view.hpp"

#include <string>

class Config;
class Context;
class Hash;

const int HASH_SOURCE_CODE_OK = 0;
const int HASH_SOURCE_CODE_ERROR = (1 << 0);
const int HASH_SOURCE_CODE_FOUND_DATE = (1 << 1);
const int HASH_SOURCE_CODE_FOUND_TIME = (1 << 2);
const int HASH_SOURCE_CODE_FOUND_TIMESTAMP = (1 << 3);

// Search for the strings "DATE", "TIME" and "TIMESTAMP" with two surrounding
// underscores in `str`.
//
// Returns a bitmask with HASH_SOURCE_CODE_FOUND_DATE,
// HASH_SOURCE_CODE_FOUND_TIME and HASH_SOURCE_CODE_FOUND_TIMESTAMP set
// appropriately.
int check_for_temporal_macros(nonstd::string_view str);

// Hash a string. Returns a bitmask of HASH_SOURCE_CODE_* results.
int hash_source_code_string(const Context& ctx,
                            Hash& hash,
                            nonstd::string_view str,
                            const std::string& path);

// Hash a file ignoring comments. Returns a bitmask of HASH_SOURCE_CODE_*
// results.
int hash_source_code_file(const Context& ctx,
                          Hash& hash,
                          const std::string& path,
                          size_t size_hint = 0);

// Hash a binary file using the inode cache if enabled.
//
// Returns true on success, otherwise false.
bool hash_binary_file(const Context& ctx, Hash& hash, const std::string& path);

// Hash the output of `command` (not executed via a shell). A "%compiler%"
// string in `command` will be replaced with `compiler`.
bool hash_command_output(Hash& hash,
                         const std::string& command,
                         const std::string& compiler);

// Like `hash_command_output` but for each semicolon-separated command in
// `command`.
bool hash_multicommand_output(Hash& hash,
                              const std::string& command,
                              const std::string& compiler);
