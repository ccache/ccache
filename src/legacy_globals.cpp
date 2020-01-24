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

#include "legacy_globals.hpp"

// Current working directory taken from $PWD, or getcwd() if $PWD is bad.
char* current_working_dir = nullptr;

// The original argument list.
extern struct args* orig_args;
struct args* orig_args = nullptr;

// Array for storing -arch options.
size_t arch_args_size = 0;
char* arch_args[MAX_ARCH_ARGS] = {nullptr};

// Name (represented as a struct digest) of the file containing the cached
// result.
struct digest* cached_result_name;

// Full path to the file containing the result
// (cachedir/a/b/cdef[...]-size.result).
char* cached_result_path;

// Full path to the file containing the manifest
// (cachedir/a/b/cdef[...]-size.manifest).
char* manifest_path;

// Time of compilation. Used to see if include files have changed after
// compilation.
time_t time_of_compilation;

// Files included by the preprocessor and their hashes. Key: file path. Value:
// struct digest.
std::unordered_map<std::string, digest> g_included_files;

// Uses absolute path for some include files.
bool has_absolute_include_headers = false;

// List of headers to ignore.
char** ignore_headers;

// Size of headers to ignore list.
size_t ignore_headers_len;

// The name of the temporary preprocessed file.
char* i_tmpfile;

// The name of the cpp stderr file.
char* cpp_stderr;

// Full path to the statistics file in the subdirectory where the cached result
// belongs (<cache_dir>/<x>/stats).
char* stats_file = nullptr;

// The stats file to use for the manifest.
char* manifest_stats_file;

// Compiler guessing is currently only based on the compiler name, so nothing
// should hard-depend on it if possible.
enum guessed_compiler guessed_compiler = GUESSED_UNKNOWN;

// Whether we are using a precompiled header (either via -include, #include or
// clang's -include-pch or -include-pth).
bool using_precompiled_header = false;

// The .gch/.pch/.pth file used for compilation.
char* included_pch_file = nullptr;

// How long (in microseconds) to wait before breaking a stale lock.
unsigned lock_staleness_limit = 2000000;
