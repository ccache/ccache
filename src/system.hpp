// Copyright (C) 2010-2021 Joel Rosdahl and other contributors
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
extern "C" int usleep(useconds_t);
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

#  ifndef S_IFIFO
#    define S_IFIFO 0x1000
#  endif

#  ifndef S_IFBLK
#    define S_IFBLK 0x6000
#  endif

#  ifndef S_IFLNK
#    define S_IFLNK 0xA000
#  endif

#  ifndef S_ISREG
#    define S_ISREG(m) (((m)&S_IFMT) == S_IFREG)
#  endif
#  ifndef S_ISDIR
#    define S_ISDIR(m) (((m)&S_IFMT) == S_IFDIR)
#  endif
#  ifndef S_ISFIFO
#    define S_ISFIFO(m) (((m)&S_IFMT) == S_IFIFO)
#  endif
#  ifndef S_ISCHR
#    define S_ISCHR(m) (((m)&S_IFMT) == S_IFCHR)
#  endif
#  ifndef S_ISLNK
#    define S_ISLNK(m) (((m)&S_IFMT) == S_IFLNK)
#  endif
#  ifndef S_ISBLK
#    define S_ISBLK(m) (((m)&S_IFMT) == S_IFBLK)
#  endif

#  include <direct.h>
#  include <io.h>
#  include <process.h>
#  define NOMINMAX 1
#  define WIN32_NO_STATUS
// clang-format off
#  include <windows.h>
#  include <bcrypt.h> // NTSTATUS
#  include <winsock2.h> // struct timeval
// clang-format on
#  undef WIN32_NO_STATUS
#  include <ntstatus.h>
#  define mkdir(a, b) _mkdir(a)
#  define execv(a, b)                                                          \
    do_not_call_execv_on_windows // to protect against incidental use of MinGW
                                 // execv
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
#  define PATH_DELIM ";"
#else
#  define DLLIMPORT
#  define PATH_DELIM ":"
#endif

DLLIMPORT extern char** environ;

// Work with silly DOS binary open.
#ifndef O_BINARY
#  define O_BINARY 0
#endif

#if defined(HAVE_SYS_MMAN_H) && defined(HAVE_PTHREAD_MUTEXATTR_SETPSHARED)
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
