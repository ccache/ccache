// Copyright (C) 2020-2024 Joel Rosdahl and other contributors
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

#include <ccache/args.hpp>
#include <ccache/core/statistic.hpp>

#include <tl/expected.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class Context;

struct ProcessArgsResult
{
  // Arguments (except -E) to send to the preprocessor.
  Args preprocessor_args;

  // Arguments not sent to the preprocessor but that should be part of the hash.
  Args extra_args_to_hash;

  // Arguments to send to the real compiler.
  Args compiler_args;

  // Whether to include the actual CWD in the hash.
  bool hash_actual_cwd = false;
};

tl::expected<ProcessArgsResult, core::Statistic> process_args(Context& ctx);

// Return whether `path` represents a precompiled header (see "Precompiled
// Headers" in GCC docs).
bool is_precompiled_header(const std::filesystem::path& path);

bool option_should_be_ignored(const std::string& arg,
                              const std::vector<std::string>& patterns);
