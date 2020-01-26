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
#include "stats.hpp"

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

  // Statistics fields in display order.
  StatsInfo stats_info[33] = {
    {STATS_ZEROTIMESTAMP,
     "stats_zeroed_timestamp",
     "stats zeroed",
     format_timestamp,
     FLAG_ALWAYS},
    {STATS_CACHEHIT_DIR,
     "direct_cache_hit",
     "cache hit (direct)",
     NULL,
     FLAG_ALWAYS},
    {STATS_CACHEHIT_CPP,
     "preprocessed_cache_hit",
     "cache hit (preprocessed)",
     NULL,
     FLAG_ALWAYS},
    {STATS_CACHEMISS, "cache_miss", "cache miss", NULL, FLAG_ALWAYS},
    {STATS_LINK, "called_for_link", "called for link", NULL, 0},
    {STATS_PREPROCESSING,
     "called_for_preprocessing",
     "called for preprocessing",
     NULL,
     0},
    {STATS_MULTIPLE, "multiple_source_files", "multiple source files", NULL, 0},
    {STATS_STDOUT,
     "compiler_produced_stdout",
     "compiler produced stdout",
     NULL,
     0},
    {STATS_NOOUTPUT,
     "compiler_produced_no_output",
     "compiler produced no output",
     NULL,
     0},
    {STATS_EMPTYOUTPUT,
     "compiler_produced_empty_output",
     "compiler produced empty output",
     NULL,
     0},
    {STATS_STATUS, "compile_failed", "compile failed", NULL, 0},
    {STATS_ERROR, "internal_error", "ccache internal error", NULL, 0},
    {STATS_PREPROCESSOR, "preprocessor_error", "preprocessor error", NULL, 0},
    {STATS_CANTUSEPCH,
     "could_not_use_precompiled_header",
     "can't use precompiled header",
     NULL,
     0},
    {STATS_CANTUSEMODULES,
     "could_not_use_modules",
     "can't use modules",
     NULL,
     0},
    {STATS_COMPILER,
     "could_not_find_compiler",
     "couldn't find the compiler",
     NULL,
     0},
    {STATS_MISSING, "missing_cache_file", "cache file missing", NULL, 0},
    {STATS_ARGS, "bad_compiler_arguments", "bad compiler arguments", NULL, 0},
    {STATS_SOURCELANG,
     "unsupported_source_language",
     "unsupported source language",
     NULL,
     0},
    {STATS_COMPCHECK,
     "compiler_check_failed",
     "compiler check failed",
     NULL,
     0},
    {STATS_CONFTEST, "autoconf_test", "autoconf compile/link", NULL, 0},
    {STATS_UNSUPPORTED_OPTION,
     "unsupported_compiler_option",
     "unsupported compiler option",
     NULL,
     0},
    {STATS_UNSUPPORTED_DIRECTIVE,
     "unsupported_code_directive",
     "unsupported code directive",
     NULL,
     0},
    {STATS_OUTSTDOUT, "output_to_stdout", "output to stdout", NULL, 0},
    {STATS_BADOUTPUTFILE,
     "bad_output_file",
     "could not write to output file",
     NULL,
     0},
    {STATS_NOINPUT, "no_input_file", "no input file", NULL, 0},
    {STATS_BADEXTRAFILE,
     "error_hashing_extra_file",
     "error hashing extra file",
     NULL,
     0},
    {STATS_NUMCLEANUPS,
     "cleanups_performed",
     "cleanups performed",
     NULL,
     FLAG_ALWAYS},
    {STATS_NUMFILES,
     "files_in_cache",
     "files in cache",
     NULL,
     FLAG_NOZERO | FLAG_ALWAYS},
    {STATS_TOTALSIZE,
     "cache_size_kibibyte",
     "cache size",
     format_size_times_1024,
     FLAG_NOZERO | FLAG_ALWAYS},
    {STATS_OBSOLETE_MAXFILES,
     "OBSOLETE",
     "OBSOLETE",
     NULL,
     FLAG_NOZERO | FLAG_NEVER},
    {STATS_OBSOLETE_MAXSIZE,
     "OBSOLETE",
     "OBSOLETE",
     NULL,
     FLAG_NOZERO | FLAG_NEVER},
    {STATS_NONE, NULL, NULL, NULL, 0}};
};
