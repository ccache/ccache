// Copyright (C) 2010-2019 Joel Rosdahl and other contributors
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

extern char** environ;

#ifndef ESTALE
#  define ESTALE -1
#endif
