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

class Context;

int execute(const char* const* argv, int fd_out, int fd_err, pid_t* pid);
std::string
find_executable(const Context& ctx, const char* name, const char* exclude_name);
std::string find_executable_in_path(const char* name,
                                    const char* exclude_name,
                                    const char* path);

void print_command(FILE* fp, const char* const* argv);
char* format_command(const char* const* argv);

#ifdef _WIN32
char* win32argvtos(const char* prefix, const char* const* argv, int* length);
std::string win32getshell(const char* path);
int win32execute(const char* path,
                 const char* const* argv,
                 int doreturn,
                 int fd_stdout,
                 int fd_stderr);
void add_exe_ext_if_no_to_fullpath(char* full_path_win_ext,
                                   size_t max_size,
                                   const char* ext,
                                   const char* path);
#endif
