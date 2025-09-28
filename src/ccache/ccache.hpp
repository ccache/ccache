// Copyright (C) 2002-2007 Andrew Tridgell
// Copyright (C) 2009-2025 Joel Rosdahl and other contributors
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

#include <ccache/config.hpp>
#include <ccache/util/args.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class Context;

extern const char CCACHE_VERSION[];

using FindExecutableFunction =
  std::function<std::string(const Context& ctx,
                            const std::string& name,
                            const std::string& exclude_path)>;

int ccache_main(int argc, const char* const* argv);

// Tested by unit tests.
struct ArgvParts
{
  bool masquerading_as_compiler = true;
  std::vector<std::string> config_settings;
  util::Args compiler_and_args;
};

ArgvParts split_argv(int argc, const char* const* argv);

void find_compiler(Context& ctx,
                   const FindExecutableFunction& find_executable_function,
                   bool masquerading_as_compiler);

CompilerType guess_compiler(const std::filesystem::path& path);

bool is_ccache_executable(const std::filesystem::path& path);

bool file_path_matches_dir_prefix_or_file(
  const std::filesystem::path& dir_prefix_or_file,
  const std::filesystem::path& file_path);
