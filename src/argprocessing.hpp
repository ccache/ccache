// Copyright (C) 2020-2023 Joel Rosdahl and other contributors
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

#include "Args.hpp"

#include <core/Statistic.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class Context;

struct ProcessArgsResult
{
  ProcessArgsResult(core::Statistic error_);
  ProcessArgsResult(const Args& preprocessor_args_,
                    const Args& extra_args_to_hash_,
                    const Args& compiler_args_,
                    bool hash_actual_cwd_);

  // nullopt on success, otherwise the statistics counter that should be
  // incremented.
  std::optional<core::Statistic> error;

  // Arguments (except -E) to send to the preprocessor.
  Args preprocessor_args;

  // Arguments not sent to the preprocessor but that should be part of the hash.
  Args extra_args_to_hash;

  // Arguments to send to the real compiler.
  Args compiler_args;

  // Whether to include the actual CWD in the hash.
  bool hash_actual_cwd;
};

inline ProcessArgsResult::ProcessArgsResult(core::Statistic error_)
  : error(error_)
{
}

inline ProcessArgsResult::ProcessArgsResult(const Args& preprocessor_args_,
                                            const Args& extra_args_to_hash_,
                                            const Args& compiler_args_,
                                            bool hash_actual_cwd_)
  : preprocessor_args(preprocessor_args_),
    extra_args_to_hash(extra_args_to_hash_),
    compiler_args(compiler_args_),
    hash_actual_cwd(hash_actual_cwd_)
{
}

ProcessArgsResult process_args(Context& ctx);

// Return whether `path` represents a precompiled header (see "Precompiled
// Headers" in GCC docs).
bool is_precompiled_header(std::string_view path);

bool option_should_be_ignored(const std::string& arg,
                              const std::vector<std::string>& patterns);
