// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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

#include "path.hpp"

#include <ccache/util/direntry.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/string.hpp>

#ifdef _WIN32
const char k_dev_null_path[] = "nul:";
#else
const char k_dev_null_path[] = "/dev/null";
#endif

namespace fs = util::filesystem;

namespace util {

std::string
add_exe_suffix(const std::string& program)
{
  return fs::path(program).has_extension() ? program : program + ".exe";
}

fs::path
apparent_cwd(const fs::path& actual_cwd)
{
#ifdef _WIN32
  return actual_cwd;
#else
  auto pwd = getenv("PWD");
  if (!pwd || !fs::path(pwd).is_absolute()) {
    return actual_cwd;
  }

  DirEntry pwd_de(pwd);
  DirEntry cwd_de(actual_cwd);
  return !pwd_de || !cwd_de || !pwd_de.same_inode_as(cwd_de) ? actual_cwd : pwd;
#endif
}

const char*
get_dev_null_path()
{
  return k_dev_null_path;
}

fs::path
lexically_normal(const fs::path& path)
{
  auto result = path.lexically_normal();
  return result.has_filename() ? result : result.parent_path();
}

fs::path
make_relative_path(const fs::path& actual_cwd,
                   const fs::path& apparent_cwd,
                   const fs::path& path)
{
  DEBUG_ASSERT(actual_cwd.is_absolute());
  DEBUG_ASSERT(apparent_cwd.is_absolute());
  DEBUG_ASSERT(path.is_absolute());

  fs::path normalized_path = util::lexically_normal(path);
  fs::path closest_existing_path = normalized_path;
  std::vector<fs::path> relpath_candidates;
  fs::path path_suffix;
  while (!fs::exists(closest_existing_path)) {
    if (path_suffix.empty()) {
      path_suffix = closest_existing_path.filename();
    } else {
      path_suffix = closest_existing_path.filename() / path_suffix;
    }
    closest_existing_path = closest_existing_path.parent_path();
  }

  relpath_candidates.push_back(
    closest_existing_path.lexically_relative(actual_cwd));
  if (apparent_cwd != actual_cwd) {
    relpath_candidates.emplace_back(
      closest_existing_path.lexically_relative(apparent_cwd));
  }

  // Find best (i.e. shortest existing) match:
  std::sort(relpath_candidates.begin(),
            relpath_candidates.end(),
            [](const auto& path1, const auto& path2) {
              return util::pstr(path1).str().length()
                     < util::pstr(path2).str().length();
            });
  for (const auto& relpath : relpath_candidates) {
    if (fs::equivalent(relpath, closest_existing_path)) {
      return path_suffix.empty() ? relpath
                                 : (relpath / path_suffix).lexically_normal();
    }
  }

  // No match so nothing else to do than to return the unmodified path.
  return path;
}

bool
path_starts_with(const fs::path& path, const fs::path& prefix)
{
#ifdef _WIN32
  // Note: Not all paths on Windows are case insensitive, but for our purposes
  // (checking whether a path is below the base directory) users will expect
  // them to be.
  fs::path p1 = util::to_lowercase(util::lexically_normal(path).string());
  fs::path p2 = util::to_lowercase(util::lexically_normal(prefix).string());
#else
  const fs::path& p1 = path;
  const fs::path& p2 = prefix;
#endif

  // Skip empty part at the end that originates from a trailing slash.
  auto p2_end = p2.end();
  if (!p2.empty()) {
    --p2_end;
    if (!p2_end->empty()) {
      ++p2_end;
    }
  }

  return std::mismatch(p1.begin(), p1.end(), p2.begin(), p2_end).second
         == p2_end;
}

bool
path_starts_with(const std::filesystem::path& path,
                 const std::vector<std::filesystem::path>& prefixes)
{
  return std::any_of(
    std::begin(prefixes), std::end(prefixes), [&](const fs::path& prefix) {
      return path_starts_with(path, prefix);
    });
}

} // namespace util
