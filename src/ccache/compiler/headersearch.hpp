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

#include <cstdint>
#include <filesystem>
#include <functional>
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

struct IncludedFile
{
  std::filesystem::path path;

  // Directories of the files that included it. They are searched first for
  // #include "..." without being header search directories.
  std::vector<std::filesystem::path> includer_dirs;
};

struct HasIncludeOperand
{
  std::string spelling;
  bool quoted; // "..." rather than <...>

  bool operator==(const HasIncludeOperand&) const = default;
};

struct HasIncludeOperands
{
  std::vector<HasIncludeOperand> literals;

  // Whether an operand is a macro, which can't be resolved.
  bool macro_operand = false;
};

// Return the operands of __has_include and __has_include_next in `source`.
HasIncludeOperands find_has_include_operands(std::string_view source);

// A __has_include operand and the file it was found in.
struct HasIncludeProbe
{
  std::filesystem::path includer;
  std::string spelling;
  bool quoted;

  bool operator==(const HasIncludeProbe&) const = default;
};

enum class PathKind : uint8_t { missing, file, directory };

struct ShadowPaths
{
  // Paths that must stay absent for the result to stay valid.
  std::vector<std::filesystem::path> paths;

  // Existing files found by __has_include probes. They must be tracked like
  // include files.
  std::vector<std::filesystem::path> probed_files;
};

// Return paths that don't exist but would make the preprocessor find another
// file for one of `included_files` or `probes` if they did: for each include
// file found in a search directory, the same relative path in every directory
// searched before it and in the directories of the files that included it (or
// the first missing parent directory of that path), plus the nonexistent search
// directories. A __has_include operand is treated like an include of that
// spelling from the probing file, with the file it resolves to (if any)
// returned in `probed_files`. Relative paths are relative to `cwd`. `stat` and
// `canonical` (which should return the path itself on failure) are called with
// absolute paths.
ShadowPaths find_shadow_paths(
  const HeaderSearchPaths& paths,
  const std::filesystem::path& cwd,
  const std::vector<IncludedFile>& included_files,
  const std::vector<HasIncludeProbe>& probes,
  const std::function<PathKind(const std::filesystem::path&)>& stat,
  const std::function<std::filesystem::path(const std::filesystem::path&)>&
    canonical);

} // namespace compiler
