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

#ifdef _WIN32
#  include <sys/stat.h>

#  define NOMINMAX 1
#  define STDIN_FILENO 0
#  define STDOUT_FILENO 1
#  define STDERR_FILENO 2

#  ifdef _MSC_VER
#    define PATH_MAX MAX_PATH
#  endif // _MSC_VER

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
#  include <fcntl.h>
#  include <io.h>
#  include <process.h>
#  define NOMINMAX 1
#  define WIN32_NO_STATUS
// clang-format off
#  include <winsock2.h> // struct timeval
// windows must be included after winsock2
// https://stackoverflow.com/questions/1372480/c-redefinition-header-files-winsock2-h
#  include <windows.h>
//  bccrypt must go after windows.h
// https://stackoverflow.com/questions/57472787/compile-errors-when-using-c-and-bcrypt-header
#  include <bcrypt.h> // NTSTATUS
// clang-format on
#  undef WIN32_NO_STATUS
#  include <ntstatus.h>
#  define mkdir(a, b) _mkdir(a)

// Protect against incidental use of MinGW execv.
#  define execv(a, b) do_not_call_execv_on_windows

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

#  ifndef O_BINARY
#    define O_BINARY 0
#  endif

#else
#  define DLLIMPORT
#endif // _WIN32
