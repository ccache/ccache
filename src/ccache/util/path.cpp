// Copyright (C) 2021-2026 Joel Rosdahl and other contributors
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

#include <ccache/util/assertions.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/wincompat.hpp>

#include <algorithm>
#include <utility>

#ifdef _WIN32
const char k_dev_null_path[] = "nul:";
#else
const char k_dev_null_path[] = "/dev/null";
#endif

namespace fs = util::filesystem;

namespace {

fs::path
lexically_relative_case_aware(const fs::path& path, const fs::path& base)
{
#ifdef _WIN32
  // Note: Case-insensitive comparison might in theory lead to an incorrect path
  // on Windows since not all filesystems are case-insensitive, but this is only
  // done to produce a candidate path that will be verified by the caller later.
  if (!util::path_components_equal_case_aware(path.root_name(),
                                              base.root_name())
      || path.is_absolute() != base.is_absolute()
      || (!path.has_root_directory() && base.has_root_directory())) {
    return {};
  }

  auto [path_it, base_it] =
    std::mismatch(path.begin(),
                  path.end(),
                  base.begin(),
                  base.end(),
                  util::path_components_equal_case_aware);
  if (path_it == path.end() && base_it == base.end()) {
    return ".";
  }

  int num_parents = 0;
  for (; base_it != base.end(); ++base_it) {
    if (*base_it == "..") {
      --num_parents;
    } else if (*base_it != "." && !base_it->empty()) {
      ++num_parents;
    }
  }
  if (num_parents < 0) {
    return {};
  }

  fs::path result;
  for (int i = 0; i < num_parents; ++i) {
    result /= "..";
  }
  for (; path_it != path.end(); ++path_it) {
    result /= *path_it;
  }
  return result;
#else
  return path.lexically_relative(base);
#endif
}

} // namespace

namespace util {

fs::path
add_exe_suffix(const fs::path& program)
{
  return program.has_extension() ? program
                                 : util::with_extension(program, ".exe");
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
make_relative_path(const fs::path& dir1,
                   const fs::path& dir2,
                   const fs::path& path)
{
  DEBUG_ASSERT(dir1.is_absolute());
  DEBUG_ASSERT(dir2.is_absolute());
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
    if (closest_existing_path == closest_existing_path.root_path()) {
      break;
    }
  }

  relpath_candidates.push_back(
    lexically_relative_case_aware(closest_existing_path, dir1));
  if (dir2 != dir1) {
    relpath_candidates.push_back(
      lexically_relative_case_aware(closest_existing_path, dir2));
  }

  // Find best (i.e. shortest existing) match:
  std::sort(relpath_candidates.begin(),
            relpath_candidates.end(),
            [](const auto& path1, const auto& path2) {
              return util::pstr(path1).str().length()
                     < util::pstr(path2).str().length();
            });
  for (const auto& relpath : relpath_candidates) {
    if (fs::equivalent(dir1 / relpath, closest_existing_path)) {
      return path_suffix.empty() ? relpath
                                 : (relpath / path_suffix).lexically_normal();
    }
  }

  // No match so nothing else to do than to return the unmodified path.
  return path;
}

bool
path_components_equal_case_aware(const fs::path& component1,
                                 const fs::path& component2)
{
#ifdef _WIN32
  const auto& string1 = component1.native();
  const auto& string2 = component2.native();
  if (string1.empty() || string2.empty()) {
    return string1.empty() && string2.empty();
  }
  if (!std::in_range<int>(string1.size())
      || !std::in_range<int>(string2.size())) {
    return false;
  }
  return CompareStringOrdinal(string1.data(),
                              static_cast<int>(string1.size()),
                              string2.data(),
                              static_cast<int>(string2.size()),
                              TRUE)
         == CSTR_EQUAL;
#else
  return component1.native() == component2.native();
#endif
}

bool
path_component_starts_with_case_aware(const fs::path& component,
                                      const fs::path& prefix)
{
  const auto& string = component.native();
  const auto& prefix_string = prefix.native();
  if (prefix_string.size() > string.size()) {
    return false;
  }
#ifdef _WIN32
  if (prefix_string.empty()) {
    return true;
  }
  if (!std::in_range<int>(prefix_string.size())) {
    return false;
  }
  return CompareStringOrdinal(string.data(),
                              static_cast<int>(prefix_string.size()),
                              prefix_string.data(),
                              static_cast<int>(prefix_string.size()),
                              TRUE)
         == CSTR_EQUAL;
#else
  return string.starts_with(prefix_string);
#endif
}

bool
path_starts_with(const fs::path& path, const fs::path& prefix)
{
#ifdef _WIN32
  // Note: Not all paths on Windows are case insensitive, but for our purposes
  // (checking whether a path is below the base directory) users will expect
  // them to be.
  fs::path p1 = util::lexically_normal(path);
  fs::path p2 = util::lexically_normal(prefix);
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

  return std::mismatch(p1.begin(),
                       p1.end(),
                       p2.begin(),
                       p2_end,
                       util::path_components_equal_case_aware)
           .second
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
