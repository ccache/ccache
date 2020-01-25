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

#include "system.hpp"

#include "ArgsInfo.hpp"
#include "Config.hpp"
#include "NonCopyable.hpp"

struct args;
struct digest;

struct Context : NonCopyable
{
  Context();
  ~Context();

  ArgsInfo args_info;
  Config config;

  // Current working directory as returned by getcwd(3).
  std::string actual_cwd;

  // Current working directory according to $PWD (falling back to getcwd(3)).
  std::string apparent_cwd;

  // Full path to the statistics file in the subdirectory where the cached
  // result belongs (<cache_dir>/<x>/stats).
  std::string stats_file;

  // The original argument list.
  struct args* orig_args = nullptr;

  // Name (represented as a struct digest) of the file containing the cached
  // result.
  struct digest* result_name = nullptr;

  // Full path to the file containing the result
  // (cachedir/a/b/cdef[...]-size.result).
  std::string result_path;

  // Full path to the file containing the manifest
  // (cachedir/a/b/cdef[...]-size.manifest).
  std::string manifest_path;

  // Time of compilation. Used to see if include files have changed after
  // compilation.
  time_t time_of_compilation = 0;
};
