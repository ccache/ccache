// Copyright (C) 2002-2007 Andrew Tridgell
// Copyright (C) 2009-2020 Joel Rosdahl and other contributors
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
#include "Counters.hpp"
#include "stats.hpp"

#include "third_party/nonstd/optional.hpp"

struct ArgsInfo;
class Context;
class Config;

#ifndef MYNAME
#  define MYNAME "ccache"
#endif

extern const char CCACHE_VERSION[];

enum class GuessedCompiler { clang, gcc, nvcc, pump, msvc, unknown };

#define SLOPPY_INCLUDE_FILE_MTIME (1U << 0)
#define SLOPPY_INCLUDE_FILE_CTIME (1U << 1)
#define SLOPPY_TIME_MACROS (1U << 2)
#define SLOPPY_PCH_DEFINES (1U << 3)
// Allow us to match files based on their stats (size, mtime, ctime), without
// looking at their contents.
#define SLOPPY_FILE_STAT_MATCHES (1U << 4)
// Allow us to not include any system headers in the manifest include files,
// similar to -MM versus -M for dependencies.
#define SLOPPY_SYSTEM_HEADERS (1U << 5)
// Allow us to ignore ctimes when comparing file stats, so we can fake mtimes
// if we want to (it is much harder to fake ctimes, requires changing clock)
#define SLOPPY_FILE_STAT_MATCHES_CTIME (1U << 6)
// Allow us to not include the -index-store-path option in the manifest hash.
#define SLOPPY_CLANG_INDEX_STORE (1U << 7)
// Ignore locale settings.
#define SLOPPY_LOCALE (1U << 8)
// Allow caching even if -fmodules is used.
#define SLOPPY_MODULES (1U << 9)

void block_signals();
void unblock_signals();
bool is_precompiled_header(const char* path);
