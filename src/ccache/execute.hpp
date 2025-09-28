// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
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

#include <ccache/util/fd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

class Context;

int execute(Context& ctx,
            const char* const* argv,
            util::Fd&& fd_out,
            util::Fd&& fd_err);

void execute_noreturn(const char* const* argv,
                      const std::filesystem::path& temp_dir);

// Find an executable named `name` in `$PATH`. Exclude any executables that are
// links to `exclude_path`.
std::string find_executable(const Context& ctx,
                            const std::string& name,
                            const std::string& exclude_path);

std::filesystem::path find_executable_in_path(
  const std::string& name,
  const std::vector<std::filesystem::path>& path_list,
  const std::optional<std::filesystem::path>& exclude_path = std::nullopt);
