// Copyright (C) 2020-2024 Joel Rosdahl and other contributors
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

#include <ccache/util/fd.hpp>

#include <filesystem>
#include <optional>
#include <string>

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

template<typename CharT>
std::filesystem::path
find_executable_in_path(const std::basic_string<CharT>& name,
                        const std::basic_string<CharT>& path_list,
                        const std::optional<std::filesystem::path>& exclude_path = std::nullopt)
{
  if (path_list.empty()) {
    return {};
  }

  auto real_exclude_path =
    exclude_path ? fs::canonical(*exclude_path).value_or("") : "";

  // Search the path list looking for the first compiler of the right name that
  // isn't us.
  for (const auto& dir : util::split_path_list(path_list)) {
    const std::vector<fs::path> candidates = {
      dir / name,
#ifdef _WIN32
      dir / FMT("{}.exe", name),
#endif
    };
    for (const auto& candidate : candidates) {
      // A valid candidate:
      //
      // 1. Must exist (e.g., should not be a broken symlink) and be an
      //    executable.
      // 2. Must not resolve to the same program as argv[0] (i.e.,
      //    exclude_path). This can happen if ccache is masquerading as the
      //    compiler (with or without using a symlink).
      // 3. As an extra safety measure: must not be a ccache executable after
      //    resolving symlinks. This can happen if the candidate compiler is a
      //    symlink to another ccache executable.
      const bool candidate_exists =
#ifdef _WIN32
        util::DirEntry(candidate).is_regular_file();
#else
        access(candidate.c_str(), X_OK) == 0;
#endif
      if (candidate_exists) {
        auto real_candidate = fs::canonical(candidate);
        if (real_candidate && *real_candidate != real_exclude_path
            && !is_ccache_executable(*real_candidate)) {
          return candidate;
        }
      }
    }
  }

  return {};
}

#ifdef _WIN32
const std::u16string win32getshell(const std::u16string& path);
#endif
