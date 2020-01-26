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

// List of headers to ignore.
char** ignore_headers;

// Size of headers to ignore list.
size_t ignore_headers_len;

// Compiler guessing is currently only based on the compiler name, so nothing
// should hard-depend on it if possible.
GuessedCompiler guessed_compiler = GuessedCompiler::unknown;

// The .gch/.pch/.pth file used for compilation.
char* included_pch_file = nullptr;

// How long (in microseconds) to wait before breaking a stale lock.
unsigned lock_staleness_limit = 2000000;
