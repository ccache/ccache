// Copyright (C) 2009-2024 Joel Rosdahl and other contributors
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

#include <ccache/hash.hpp>
#include <ccache/util/bitset.hpp>

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

class Config;
class Context;

enum class HashSourceCode {
  ok = 0,
  error = 1U << 0,
  found_date = 1U << 1,
  found_time = 1U << 2,
  found_timestamp = 1U << 3,
};

using HashSourceCodeResult = util::BitSet<HashSourceCode>;

// Search for tokens (described in HashSourceCode) in `str`.
HashSourceCodeResult check_for_temporal_macros(std::string_view str);

// Hash a source code file using the inode cache if enabled.
HashSourceCodeResult hash_source_code_file(const Context& ctx,
                                           Hash::Digest& digest,
                                           const std::filesystem::path& path,
                                           size_t size_hint = 0);

// Hash a binary file (using the inode cache if enabled) and put its digest in
// `digest`
//
// Returns true on success, otherwise false.
bool hash_binary_file(const Context& ctx,
                      Hash::Digest& digest,
                      const std::filesystem::path& path,
                      size_t size_hint = 0);

// Hash a binary file (using the inode cache if enabled) and hash the digest to
// `hash`.
//
// Returns true on success, otherwise false.
bool hash_binary_file(const Context& ctx,
                      Hash& hash,
                      const std::filesystem::path& path);

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
