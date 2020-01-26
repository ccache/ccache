// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include "ccache.hpp"
#include "hash.hpp"

#include <unordered_map>

struct args;

struct Context : NonCopyable
{
  Context() = default;
  ~Context();

  Config config;
  ArgsInfo args_info;

  // Current working directory taken from $PWD, or getcwd() if $PWD is bad.
  char* current_working_dir = nullptr;

  // Full path to the statistics file in the subdirectory where the cached
  // result belongs (<cache_dir>/<x>/stats).
  char* stats_file = nullptr;

  // The original argument list.
  struct args* orig_args = nullptr;

  // Name (represented as a struct digest) of the file containing the cached
  // result.
  struct digest* result_name = nullptr;

  // Full path to the file containing the result
  // (cachedir/a/b/cdef[...]-size.result).
  char* result_path = nullptr;

  // Full path to the file containing the manifest
  // (cachedir/a/b/cdef[...]-size.manifest).
  char* manifest_path = nullptr;

  // Time of compilation. Used to see if include files have changed after
  // compilation.
  time_t time_of_compilation = 0;

  // Files included by the preprocessor and their hashes. Key: file path. Value:
  // struct digest.
  std::unordered_map<std::string, digest> included_files;

  // Uses absolute path for some include files.
  bool has_absolute_include_headers = false;

  // The name of the temporary preprocessed file.
  char* i_tmpfile = nullptr;

  // The name of the cpp stderr file.
  char* cpp_stderr = nullptr;

  // The stats file to use for the manifest.
  char* manifest_stats_file = nullptr;

  // Compiler guessing is currently only based on the compiler name, so nothing
  // should hard-depend on it if possible.
  GuessedCompiler guessed_compiler = GuessedCompiler::unknown;

  // The .gch/.pch/.pth file used for compilation.
  char* included_pch_file = nullptr;

};
