// Copyright (C) 2010-2019 Joel Rosdahl
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

#include <sys/file.h>
#ifdef HAVE_SYS_MMAN_H
#include <sys/mman.h>
#endif
#include <sys/stat.h>
#include <sys/types.h>
#ifdef HAVE_SYS_WAIT_H
#include <sys/wait.h>
#endif

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

// AIX/PASE does not properly define usleep within its headers. However, the
// function is available in libc.a. This extern define ensures that it is
// usable within the ccache code base.
#ifdef _AIX
extern int usleep(useconds_t);
#endif

extern char **environ;

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
