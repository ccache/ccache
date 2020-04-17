// Copyright (C) 2010-2020 Joel Rosdahl and other contributors
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

#ifdef __MINGW32__
#  define __USE_MINGW_ANSI_STDIO 1
#  define __STDC_FORMAT_MACROS 1
#  define ATTRIBUTE_FORMAT_PRINTF __MINGW_PRINTF_FORMAT
#else
#  define ATTRIBUTE_FORMAT_PRINTF printf
#endif

#include "config.h"

#include <sys/file.h>
#ifdef HAVE_SYS_MMAN_H
#  include <sys/mman.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_SYS_WAIT_H
#  include <sys/wait.h>
#endif

#include <cassert>
#include <cctype>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <csignal>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <strings.h>
#include <unistd.h>
#include <utime.h>

// AIX/PASE does not properly define usleep within its headers. However, the
// function is available in libc.a. This extern define ensures that it is
// usable within the ccache code base.
#ifdef _AIX
extern int usleep(useconds_t);
#endif

extern char** environ;

#ifdef __GNUC__
#  define ATTR_FORMAT(x, y, z)                                                 \
    __attribute__((format(ATTRIBUTE_FORMAT_PRINTF, y, z)))
#  define ATTR_NORETURN __attribute__((noreturn))
#else
#  define ATTR_FORMAT(x, y, z)
#  define ATTR_NORETURN
#endif

#define str_eq(s1, s2) (strcmp((s1), (s2)) == 0)
#define str_startswith(s, prefix)                                              \
  (strncmp((s), (prefix), strlen((prefix))) == 0)
#define str_endswith(s, suffix)                                                \
  (strlen(s) >= strlen(suffix)                                                 \
   && str_eq((s) + strlen(s) - strlen(suffix), (suffix)))
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

// Buffer size for I/O operations. Should be a multiple of 4 KiB.
#define READ_BUFFER_SIZE 65536

#ifndef ESTALE
#  define ESTALE -1
#endif

#ifdef _WIN32
#  ifndef _WIN32_WINNT
// _WIN32_WINNT is set in the generated header config.h
#    error _WIN32_WINNT is undefined
#  endif
#  include <windows.h>
#  define mkdir(a, b) mkdir(a)
#  define link(src, dst) (CreateHardLink(dst, src, NULL) ? 0 : -1)
#  define execv(a, b) win32execute(a, b, 0, -1, -1)
#  define DIR_DELIM_CH '\\'
#  define PATH_DELIM ";"
#  define F_RDLCK 0
#  define F_WRLCK 0
#else
#  define DIR_DELIM_CH '/'
#  define PATH_DELIM ":"
#endif

// Work with silly DOS binary open.
#ifndef O_BINARY
#  define O_BINARY 0
#endif
