// Copyright (C) 2020 Joel Rosdahl and other contributors
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
#include "core/Statistics.hpp"

#include "third_party/nonstd/optional.hpp"

class Context;

struct ProcessArgsResult
{
  ProcessArgsResult(Statistic error);
  ProcessArgsResult(const Args& preprocessor_args,
                    const Args& extra_args_to_hash,
                    const Args& compiler_args);

  // nullopt on success, otherwise the statistics counter that should be
  // incremented.
  nonstd::optional<Statistic> error;

  // Arguments (except -E) to send to the preprocessor.
  Args preprocessor_args;

  // Arguments not sent to the preprocessor but that should be part of the hash.
  Args extra_args_to_hash;

  // Arguments to send to the real compiler.
  Args compiler_args;
};

inline ProcessArgsResult::ProcessArgsResult(Statistic error_) : error(error_)
{
}

inline ProcessArgsResult::ProcessArgsResult(const Args& preprocessor_args_,
                                            const Args& extra_args_to_hash_,
                                            const Args& compiler_args_)
  : preprocessor_args(preprocessor_args_),
    extra_args_to_hash(extra_args_to_hash_),
    compiler_args(compiler_args_)
{
}

ProcessArgsResult process_args(Context& ctx);
