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

#ifdef _MSC_VER
#  define CCACHE_FUNCTION __func__
#else
#  define CCACHE_FUNCTION __PRETTY_FUNCTION__
#endif

// ASSERT is like the standard C `assert` macro but enabled in both debug and
// release builds.
#define ASSERT(condition)                                                      \
  do {                                                                         \
    if (!(condition)) {                                                        \
      handle_failed_assertion(                                                 \
        __FILE__, __LINE__, CCACHE_FUNCTION, #condition);                      \
    }                                                                          \
  } while (false)

// DEBUG_ASSERT is like the standard C `assert` macro, i.e. only enabled in
// debug builds.
#ifdef NDEBUG
#  define DEBUG_ASSERT(condition) ((void)0)
#else
#  define DEBUG_ASSERT(condition) ASSERT(condition)
#endif

[[noreturn]] void handle_failed_assertion(const char* file,
                                          size_t line,
                                          const char* function,
                                          const char* condition);
