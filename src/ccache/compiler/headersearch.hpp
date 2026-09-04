// Copyright (C) 2026 Joel Rosdahl and other contributors
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

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace compiler {

using Dirs = std::vector<std::filesystem::path>;

// Header search directories reported by GCC and Clang when the preprocessor
// runs in verbose mode (-Wp,-v), in search order and spelled like the
// preprocessor printed them (relative to its working directory if given as
// relative on the command line).
struct HeaderSearchPaths
{
  // Searched for #include "..." before angle_dirs (-iquote).
  Dirs quote_dirs;

  // Searched for #include <...> and, after quote_dirs, for #include "..." (-I,
  // -isystem, -idirafter and built-in directories).
  Dirs angle_dirs;

  // Requested but ignored by the preprocessor since they don't exist.
  Dirs nonexistent_dirs;
};

struct HeaderSearchOutput
{
  // Nullopt if the stderr data contains no search list.
  std::optional<HeaderSearchPaths> paths;

  // The stderr data without the report lines.
  std::string remaining_stderr;
};

// Parse and remove the header search directory report that GCC and Clang print
// to stderr when the preprocessor runs in verbose mode (-Wp,-v). Other lines
// are kept. Header maps are skipped since they aren't directories, framework
// directories are included and GCC's #embed search list is removed but not
// parsed. Directories from multiple reports (e.g. CUDA host and device
// compilation) are concatenated.
HeaderSearchOutput parse_header_search_output(std::string_view stderr_data);

} // namespace compiler
