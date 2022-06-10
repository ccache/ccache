// Copyright (C) 2020-2022 Joel Rosdahl and other contributors
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

#include "Fd.hpp"

#include <optional>
#include <string>

class Context;

int execute(Context& ctx, const char* const* argv, Fd&& fd_out, Fd&& fd_err);

void execute_noreturn(const char* const* argv, const std::string& temp_dir);

// Find an executable named `name` in `$PATH`. Exclude any executables that are
// links to `exclude_path`.
std::string find_executable(const Context& ctx,
                            const std::string& name,
                            const std::string& exclude_path);

std::string
find_executable_in_path(const std::string& name,
                        const std::string& path_list,
                        std::optional<std::string> exclude_path = std::nullopt);

#ifdef _WIN32
std::string win32getshell(const std::string& path);
#endif
