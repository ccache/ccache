// Copyright (C) 2021 Joel Rosdahl and other contributors
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

enum Sloppiness {
  SLOPPY_INCLUDE_FILE_MTIME = 1 << 0,
  SLOPPY_INCLUDE_FILE_CTIME = 1 << 1,
  SLOPPY_TIME_MACROS = 1 << 2,
  SLOPPY_PCH_DEFINES = 1 << 3,
  // Allow us to match files based on their stats (size, mtime, ctime), without
  // looking at their contents.
  SLOPPY_FILE_STAT_MATCHES = 1 << 4,
  // Allow us to not include any system headers in the manifest include files,
  // similar to -MM versus -M for dependencies.
  SLOPPY_SYSTEM_HEADERS = 1 << 5,
  // Allow us to ignore ctimes when comparing file stats, so we can fake mtimes
  // if we want to (it is much harder to fake ctimes, requires changing clock)
  SLOPPY_FILE_STAT_MATCHES_CTIME = 1 << 6,
  // Allow us to not include the -index-store-path option in the manifest hash.
  SLOPPY_CLANG_INDEX_STORE = 1 << 7,
  // Ignore locale settings.
  SLOPPY_LOCALE = 1 << 8,
  // Allow caching even if -fmodules is used.
  SLOPPY_MODULES = 1 << 9,
};
