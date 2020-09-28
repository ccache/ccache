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

#include <functional>
#include <string>

class Context;

extern const char CCACHE_VERSION[];

enum class GuessedCompiler { clang, gcc, nvcc, pump, unknown };

const uint32_t SLOPPY_INCLUDE_FILE_MTIME = 1 << 0;
const uint32_t SLOPPY_INCLUDE_FILE_CTIME = 1 << 1;
const uint32_t SLOPPY_TIME_MACROS = 1 << 2;
const uint32_t SLOPPY_PCH_DEFINES = 1 << 3;
// Allow us to match files based on their stats (size, mtime, ctime), without
// looking at their contents.
const uint32_t SLOPPY_FILE_STAT_MATCHES = 1 << 4;
// Allow us to not include any system headers in the manifest include files,
// similar to -MM versus -M for dependencies.
const uint32_t SLOPPY_SYSTEM_HEADERS = 1 << 5;
// Allow us to ignore ctimes when comparing file stats, so we can fake mtimes
// if we want to (it is much harder to fake ctimes, requires changing clock)
const uint32_t SLOPPY_FILE_STAT_MATCHES_CTIME = 1 << 6;
// Allow us to not include the -index-store-path option in the manifest hash.
const uint32_t SLOPPY_CLANG_INDEX_STORE = 1 << 7;
// Ignore locale settings.
const uint32_t SLOPPY_LOCALE = 1 << 8;
// Allow caching even if -fmodules is used.
const uint32_t SLOPPY_MODULES = 1 << 9;

using FindExecutableFunction =
  std::function<std::string(const Context& ctx,
                            const std::string& name,
                            const std::string& exclude_name)>;

// Tested by unittests
void find_compiler(Context& ctx,
                   const FindExecutableFunction& find_executable_function);
