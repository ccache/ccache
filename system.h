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

#ifdef WIN32
#	include "config_win32.h"
#   include <windows.h>
#   include <direct.h>
#   include <io.h>
#   include <process.h>
#   include <stdint.h>
#else
#	include "config.h"
#   include <sys/file.h>
#   include <dirent.h>
#   include <unistd.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_SYS_MMAN_H
#   include <sys/mman.h>
#endif
#ifdef HAVE_SYS_WAIT_H
#   include <sys/wait.h>
#endif
#ifdef HAVE_INTTYPES_H
#   include <inttypes.h>
#endif
#ifdef HAVE_STRING_H
#   include <string.h>
#endif
#ifdef HAVE_STRINGS_H
#   include <strings.h>
#endif


#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef WIN32
	_CRTIMP extern char **environ;
	typedef int pid_t;
	typedef SSIZE_T ssize_t;
#else
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

#ifdef HAVE_STDBOOL_H
#  include <stdbool.h>
#else
#  ifndef HAVE__BOOL
#    ifdef __cplusplus
typedef bool _Bool;
#    else
#      define _Bool signed char
#    endif
#  endif
#  define bool _Bool
#  define false 0
#  define true 1
#  define __bool_true_false_are_defined 1
#endif
#endif // CCACHE_SYSTEM_H
