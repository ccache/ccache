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
#endif

#include "config.h"

#ifdef HAVE_SYS_FILE_H
#  include <sys/file.h>
#endif

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
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#ifdef HAVE_DIRENT_H
#  include <dirent.h>
#endif

#include <fcntl.h>

#ifdef HAVE_STRINGS_H
#  include <strings.h>
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef HAVE_UTIME_H
#  include <utime.h>
#elif defined(HAVE_SYS_UTIME_H)
#  include <sys/utime.h>
#endif

#ifdef HAVE_VARARGS_H
#  include <varargs.h>
#endif

// AIX/PASE does not properly define usleep within its headers. However, the
// function is available in libc.a. This extern define ensures that it is
// usable within the ccache code base.
#ifdef _AIX
extern int usleep(useconds_t);
#endif

#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))

// Buffer size for I/O operations. Should be a multiple of 4 KiB.
const size_t READ_BUFFER_SIZE = 65536;

#ifndef ESTALE
#  define ESTALE -1
#endif

#ifdef _WIN32
#  ifndef _WIN32_WINNT
// _WIN32_WINNT is set in the generated header config.h
#    error _WIN32_WINNT is undefined
#  endif

#  ifdef _MSC_VER
typedef int mode_t;
typedef int pid_t;
#  endif

#  ifndef __MINGW32__
typedef int64_t ssize_t;
#  endif

// Defined in Win32Util.cpp
void usleep(int64_t usec);
struct tm* localtime_r(time_t* _clock, struct tm* _result);

#  ifdef _MSC_VER
int gettimeofday(struct timeval* tp, struct timezone* tzp);
int asprintf(char** strp, const char* fmt, ...);
#  endif

// From:
// http://mesos.apache.org/api/latest/c++/3rdparty_2stout_2include_2stout_2windows_8hpp_source.html
#  ifdef _MSC_VER
const mode_t S_IRUSR = mode_t(_S_IREAD);
const mode_t S_IWUSR = mode_t(_S_IWRITE);
#  endif

// From https://stackoverflow.com/a/62371749/262458
#  define _CRT_INTERNAL_NONSTDC_NAMES 1
#  include <sys/stat.h>
#  if !defined(S_ISREG) && defined(S_IFMT) && defined(S_IFREG)
#    define S_ISREG(m) (((m)&S_IFMT) == S_IFREG)
#  endif
#  if !defined(S_ISDIR) && defined(S_IFMT) && defined(S_IFDIR)
#    define S_ISDIR(m) (((m)&S_IFMT) == S_IFDIR)
#  endif

#  include <direct.h>
#  include <io.h>
#  include <process.h>
#  define NOMINMAX 1
#  include <windows.h>
#  define mkdir(a, b) _mkdir(a)
#  define execv(a, b) win32execute(a, b, 0, -1, -1)
#  define strncasecmp _strnicmp
#  define strcasecmp _stricmp

#  ifdef _MSC_VER
#    define PATH_MAX MAX_PATH
#  endif

#  ifdef _MSC_VER
#    define DLLIMPORT __declspec(dllimport)
#  else
#    define DLLIMPORT
#  endif

#  define STDIN_FILENO 0
#  define STDOUT_FILENO 1
#  define STDERR_FILENO 2
#  define DIR_DELIM_CH '\\'
#  define PATH_DELIM ";"
#else
#  define DLLIMPORT
#  define DIR_DELIM_CH '/'
#  define PATH_DELIM ":"
#endif

DLLIMPORT extern char** environ;

// Work with silly DOS binary open.
#ifndef O_BINARY
#  define O_BINARY 0
#endif

#ifdef HAVE_SYS_MMAN_H
#  define INODE_CACHE_SUPPORTED
#endif

// Workaround for missing std::is_trivially_copyable in GCC < 5.
#if __GNUG__ && __GNUC__ < 5
#  define IS_TRIVIALLY_COPYABLE(T) __has_trivial_copy(T)
#else
#  define IS_TRIVIALLY_COPYABLE(T) std::is_trivially_copyable<T>::value
#endif

// GCC version of a couple of standard C++ attributes
#ifdef __GNUC__
#  define nodiscard gnu::warn_unused_result
#  define maybe_unused gnu::unused
#endif
