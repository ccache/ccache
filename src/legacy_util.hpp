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

#include <string>

void fatal(const char* format, ...) ATTR_FORMAT(printf, 1, 2) ATTR_NORETURN;

bool write_fd(int fd, const void* buf, size_t size);
bool copy_fd(int fd_in, int fd_out);
bool clone_file(const char* src, const char* dest, bool via_tmp_file);
bool copy_file(const char* src, const char* dest, bool via_tmp_file);
bool move_file(const char* src, const char* dest);
const char* get_hostname();
char* format(const char* format, ...) ATTR_FORMAT(printf, 1, 2);
void reformat(char** ptr, const char* format, ...) ATTR_FORMAT(printf, 2, 3);
char* x_strdup(const char* s);
char* x_strndup(const char* s, size_t n);
void x_setenv(const char* name, const char* value);
void x_unsetenv(const char* name);
#ifndef HAVE_LOCALTIME_R
struct tm* localtime_r(const time_t* timep, struct tm* result);
#endif
const char* get_home_directory();
bool same_executable_name(const char* s1, const char* s2);
bool is_full_path(const char* path);
void update_mtime(const char* path);
void x_exit(int status) ATTR_NORETURN;
int x_rename(const char* oldpath, const char* newpath);
void set_cloexec_flag(int fd);
double time_seconds();
