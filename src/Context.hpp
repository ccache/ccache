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

#include "Args.hpp"
#include "ArgsInfo.hpp"
#include "Config.hpp"
#include "NonCopyable.hpp"
#include "ccache.hpp"
#include "hash.hpp"

#include "third_party/nonstd/optional.hpp"

#include <string>
#include <unordered_map>
#include <vector>

struct Context : NonCopyable
{
  Context();

  ArgsInfo args_info;
  Config config;

  // Current working directory as returned by getcwd(3).
  std::string actual_cwd;

  // Current working directory according to $PWD (falling back to getcwd(3)).
  std::string apparent_cwd;

  // The original argument list.
  Args orig_args;

  // Name (represented as a struct digest) of the file containing the cached
  // result.
  nonstd::optional<struct digest> result_name;

  // Full path to the file containing the result
  // (cachedir/a/b/cdef[...]-size.result).
  std::string result_path;

  // Full path to the file containing the manifest
  // (cachedir/a/b/cdef[...]-size.manifest).
  std::string manifest_path;

  // Time of compilation. Used to see if include files have changed after
  // compilation.
  time_t time_of_compilation = 0;

  // Files included by the preprocessor and their hashes.
  // Key: file path. Value: struct digest.
  std::unordered_map<std::string, digest> included_files;

  // Uses absolute path for some include files.
  bool has_absolute_include_headers = false;

  // The name of the temporary preprocessed file.
  std::string i_tmpfile;

  // The name of the cpp stderr file.
  std::string cpp_stderr;

  // The stats file to use for the manifest.
  std::string manifest_stats_file;

  // Compiler guessing is currently only based on the compiler name, so nothing
  // should hard-depend on it if possible.
  GuessedCompiler guessed_compiler = GuessedCompiler::unknown;

  // The .gch/.pch/.pth file used for compilation.
  std::string included_pch_file;

  // Headers (or directories with headers) to ignore in manifest mode.
  std::vector<std::string> ignore_header_paths;

  // Full path to the statistics file in the subdirectory where the cached
  // result belongs (<cache_dir>/<x>/stats).
  std::string stats_file;

  // Statistics which get written into the `stats_file` upon exit.
  Counters counter_updates;
};
