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

#include <filesystem>
#include <string>
#include <string_view>

class Context;

namespace core {

// Like std::filesystem::create_directories but throws core::Fatal on error.
void ensure_dir_exists(const std::filesystem::path& dir);

// Make a `path` relative to CWD if it's under base_dir.
std::filesystem::path make_relative_path(const Context& ctx,
                                         const std::filesystem::path& path);

// Rewrite path to absolute path in `text` in the following cases, where X
// may be optional ANSI CSI sequences:
//
//     X<path>[:1:2]X: ...
//     In file included from X<path>[:1:2]X:
//     X<path>(line[,column])[ ]: ...
//
// See get_diagnostics_path_length().
std::string rewrite_stderr_to_absolute_paths(std::string_view text);

// Send `text` to file descriptor `fd` (typically stdout or stderr, which
// potentially is connected to a console), optionally stripping ANSI color
// sequences if `ctx.args_info.strip_diagnostics_colors` is true and rewriting
// paths to absolute if `ctx.config.absolute_paths_in_stderr()` is true. Throws
// `core::Error` on error.
void send_to_console(const Context& ctx, std::string_view text, int fd);

// Returns a copy of string with the specified ANSI CSI sequences removed.
[[nodiscard]] std::string strip_ansi_csi_seqs(std::string_view string);

// Get the length of paths in compiler diagnostics messages in following forms:
// 1. <path>:
// 2. <path>(line[,column]):    // MSVC
// 3. <path>(line[,column]) :   // MSVC
std::size_t get_diagnostics_path_length(std::string_view line);

} // namespace core
