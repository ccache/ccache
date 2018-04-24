// Copyright (C) 2010-2016 Joel Rosdahl
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

#ifndef CCACHE_SYSTEM_H
#define CCACHE_SYSTEM_H

#include "config.h"

#ifdef _WIN32
#include <winsock2.h>
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0501
#endif
#include <windows.h>
#include <direct.h>
#endif

#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_SYS_FILE_H
#include <sys/file.h>
#endif
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif
#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#endif
#ifdef HAVE_STDARG_H
#include <stdarg.h>
#endif
#ifdef HAVE_STDDEF_H
#include <stddef.h>
#endif
#ifdef HAVE_STRINGS_H
#include <strings.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#ifdef HAVE_UTIME_H
#include <utime.h>
#endif
#ifdef HAVE_DIRENT_H
#include <dirent.h>
#elif defined (_MSC_VER)
#include "dirent/dirent.h"
#endif

#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif

#ifdef _MSC_VER
#define strcasecmp _stricmp
#endif

#ifndef R_OK
#define R_OK 4
#endif

#ifndef STDERR_FILENO
#define STDERR_FILENO 2
#endif

#ifdef _MSC_VER
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#endif

#ifdef _MSC_VER
#define pid_t int
#endif

#ifndef _MSC_VER
extern char **environ;
#endif

#ifndef ESTALE
#define ESTALE -1
#endif

#if !HAVE_VSNPRINTF
  int rpl_vsnprintf(char *, size_t, const char *, va_list);
  #define vsnprintf rpl_vsnprintf
#endif
#if !HAVE_SNPRINTF
  int rpl_snprintf(char *, size_t, const char *, ...);
  #define snprintf rpl_snprintf
#endif
#if !HAVE_VASPRINTF
  int rpl_vasprintf(char **, const char *, va_list);
  #define vasprintf rpl_vasprintf
#endif
#if !HAVE_ASPRINTF
  int rpl_asprintf(char **, const char *, ...);
  #define asprintf rpl_asprintf
#endif

#endif // CCACHE_SYSTEM_H
