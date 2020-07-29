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

#include "Fd.hpp"

#include <string>

class Context;

int execute(const char* const* argv, Fd&& fd_out, Fd&& fd_err, pid_t* pid);

// Find an executable named `name` in `$PATH`. Exclude any executables that are
// links to `exclude_name`.
std::string find_executable(const Context& ctx,
                            const std::string& name,
                            const std::string& exclude_name);

std::string find_executable_in_path(const std::string& name,
                                    const std::string& exclude_name,
                                    const std::string& path);

#ifdef _WIN32
std::string win32getshell(const std::string& path);
int win32execute(const char* path,
                 const char* const* argv,
                 int doreturn,
                 int fd_stdout,
                 int fd_stderr);
#endif
