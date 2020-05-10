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

#include "stats.hpp"

#include "third_party/nonstd/optional.hpp"

class Context;
class Args;

// Process the compiler options into options suitable for passing to the
// preprocessor (`preprocessor_args`) and the real compiler (`compiler_args`).
// `preprocessor_args` doesn't include -E; this is added later.
// `extra_args_to_hash` are the arguments that are not included in
// `preprocessor_args` but that should be included in the hash.
//
// Returns nullopt on success, otherwise the statistics counter that should be
// incremented.
nonstd::optional<enum stats> process_args(Context& ctx,
                                          Args& preprocessor_args,
                                          Args& extra_args_to_hash,
                                          Args& compiler_args);
