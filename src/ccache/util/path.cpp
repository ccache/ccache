// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include "path.hpp"

#include <ccache/Util.hpp>
#include <ccache/util/DirEntry.hpp>
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
actual_cwd()
{
  auto cwd = fs::current_path();
  if (!cwd) {
    return {};
  }
  auto cwd_str = cwd->string();
#ifdef _WIN32
  std::replace(cwd_str.begin(), cwd_str.end(), '\\', '/');
#endif
  return cwd_str;
}

std::string
add_exe_suffix(const std::string& program)
{
  std::string ext = util::to_lowercase(fs::path(program).extension().string());
  if (ext == ".exe" || ext == ".bat" || ext == ".sh") {
    return program;
  } else {
    return program + ".exe";
  }
}

std::string
apparent_cwd(const std::string& actual_cwd)
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
  return !pwd_de || !cwd_de || !pwd_de.same_inode_as(cwd_de)
           ? actual_cwd
           : Util::normalize_concrete_absolute_path(pwd);
#endif
}

const char*
get_dev_null_path()
{
  return k_dev_null_path;
}

bool
path_starts_with(std::string_view path, std::string_view prefix)
{
  if (path.empty()) {
    return false;
  }
  for (size_t i = 0, j = 0; i < path.length() && j < prefix.length();
       ++i, ++j) {
#ifdef _WIN32
    // Skip escaped backslashes \\\\ as seen by the preprocessor.
    if (i > 0 && path[i] == '\\' && path[i - 1] == '\\') {
      ++i;
    }
    if (j > 0 && prefix[j] == '\\' && prefix[j - 1] == '\\') {
      ++j;
    }

    // Handle back and forward slashes as equal.
    if (path[i] == '/' && prefix[j] == '\\') {
      continue;
    }
    if (path[i] == '\\' && prefix[j] == '/') {
      continue;
    }
    if (std::tolower(path[i]) != std::tolower(prefix[j])) {
      return false;
    }
#else
    if (path[i] != prefix[j]) {
      return false;
    }
#endif
  }
  return true;
}

} // namespace util
