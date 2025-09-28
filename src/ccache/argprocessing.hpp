// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#include <ccache/core/statistic.hpp>
#include <ccache/util/args.hpp>

#include <tl/expected.hpp>

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

class Context;

struct ProcessArgsResult
{
  // Arguments (except "-E -o output.i") to send to the preprocessor. These are
  // part of the input hash (except those marked as AFFECTS_CPP in compopt.cpp).
  util::Args preprocessor_args;

  // Arguments to send to the real compiler. Not part of the input hash.
  util::Args compiler_args;

  // Arguments not sent to the preprocessor but added to the input hash anyway.
  util::Args extra_args_to_hash;

  // -m*=native arguments to let the preprocessor expand.
  util::Args native_args;

  // Whether to include the actual CWD in the input hash.
  bool hash_actual_cwd = false;
};

tl::expected<ProcessArgsResult, core::Statistic> process_args(Context& ctx);

// Return whether `path` represents a precompiled header (see "Precompiled
// Headers" in GCC docs).
bool is_precompiled_header(const std::filesystem::path& path);

bool option_should_be_ignored(const std::string& arg,
                              const std::vector<std::string>& patterns);
