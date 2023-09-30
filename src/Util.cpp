// Copyright (C) 2019-2023 Joel Rosdahl and other contributors
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

#include "Util.hpp"

#include "Config.hpp"
#include "Context.hpp"
#include "Logging.hpp"
#include "Win32Util.hpp"

#include <Config.hpp>
#include <core/exceptions.hpp>
#include <util/DirEntry.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>
#include <util/fmtmacros.hpp>
#include <util/path.hpp>
#include <util/string.hpp>
#include <util/wincompat.hpp>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <fcntl.h>

namespace fs = util::filesystem;

using util::DirEntry;

namespace Util {

std::string_view
base_name(std::string_view path)
{
#ifdef _WIN32
  const char delim[] = "/\\";
#else
  const char delim[] = "/";
#endif
  size_t n = path.find_last_of(delim);
  return n == std::string::npos ? path : path.substr(n + 1);
}

std::string
change_extension(std::string_view path, std::string_view new_ext)
{
  std::string_view without_ext = Util::remove_extension(path);
  return std::string(without_ext).append(new_ext.data(), new_ext.length());
}

size_t
common_dir_prefix_length(std::string_view dir, std::string_view path)
{
  if (dir.empty() || path.empty() || dir == "/" || path == "/") {
    return 0;
  }

  ASSERT(dir[0] == '/');
  ASSERT(path[0] == '/');

  const size_t limit = std::min(dir.length(), path.length());
  size_t i = 0;

  while (i < limit && dir[i] == path[i]) {
    ++i;
  }

  if ((i == dir.length() && i == path.length())
      || (i == dir.length() && path[i] == '/')
      || (i == path.length() && dir[i] == '/')) {
    return i;
  }

  do {
    --i;
  } while (i > 0 && dir[i] != '/' && path[i] != '/');

  return i;
}

std::string_view
dir_name(std::string_view path)
{
#ifdef _WIN32
  const char delim[] = "/\\";
#else
  const char delim[] = "/";
#endif
  size_t n = path.find_last_of(delim);
  if (n == std::string::npos) {
    // "foo" -> "."
    return ".";
  } else if (n == 0) {
    // "/" -> "/" (Windows: or "\\" -> "\\")
    return path.substr(0, 1);
#ifdef _WIN32
  } else if (n == 2 && path[1] == ':') {
    // Windows: "C:\\foo" -> "C:\\" or "C:/foo" -> "C:/"
    return path.substr(0, 3);
#endif
  } else {
    // "/dir/foo" -> "/dir" (Windows: or "C:\\dir\\foo" -> "C:\\dir")
    return path.substr(0, n);
  }
}

std::string_view
get_extension(std::string_view path)
{
#ifndef _WIN32
  const char stop_at_chars[] = "./";
#else
  const char stop_at_chars[] = "./\\";
#endif
  size_t pos = path.find_last_of(stop_at_chars);
  if (pos == std::string_view::npos || path.at(pos) == '/') {
    return {};
#ifdef _WIN32
  } else if (path.at(pos) == '\\') {
    return {};
#endif
  } else {
    return path.substr(pos);
  }
}

std::string
get_relative_path(std::string_view dir, std::string_view path)
{
  ASSERT(util::is_absolute_path(dir));
  ASSERT(util::is_absolute_path(path));

#ifdef _WIN32
  // Paths can be escaped by a slash for use with e.g. -isystem.
  if (dir.length() >= 3 && dir[0] == '/' && dir[2] == ':') {
    dir = dir.substr(1);
  }
  if (path.length() >= 3 && path[0] == '/' && path[2] == ':') {
    path = path.substr(1);
  }
  if (dir[0] != path[0]) {
    // Drive letters differ.
    return std::string(path);
  }
  dir = dir.substr(2);
  path = path.substr(2);
#endif

  std::string result;
  size_t common_prefix_len = Util::common_dir_prefix_length(dir, path);
  if (common_prefix_len > 0 || dir != "/") {
    for (size_t i = common_prefix_len; i < dir.length(); ++i) {
      if (dir[i] == '/') {
        if (!result.empty()) {
          result += '/';
        }
        result += "..";
      }
    }
  }
  if (path.length() > common_prefix_len) {
    if (!result.empty()) {
      result += '/';
    }
    result += std::string(path.substr(common_prefix_len + 1));
  }
  result.erase(result.find_last_not_of('/') + 1);
  return result.empty() ? "." : result;
}

std::optional<size_t>
is_absolute_path_with_prefix(std::string_view path)
{
#ifdef _WIN32
  const char delim[] = "/\\";
#else
  const char delim[] = "/";
#endif
  auto split_pos = path.find_first_of(delim);
  if (split_pos != std::string::npos) {
#ifdef _WIN32
    // -I/C:/foo and -I/c/foo will already be handled by delim_pos correctly
    // resulting in -I and /C:/foo or /c/foo respectively. -IC:/foo will not as
    // we would get -IC: and /foo.
    if (split_pos > 0 && path[split_pos - 1] == ':') {
      split_pos = split_pos - 2;
    }
#endif
    // This is not redundant on some platforms, so nothing to simplify.
    // NOLINTNEXTLINE(readability-simplify-boolean-expr)
    return split_pos;
  }
  return std::nullopt;
}

std::string
make_relative_path(const std::string& base_dir,
                   const std::string& actual_cwd,
                   const std::string& apparent_cwd,
                   std::string_view path)
{
  if (base_dir.empty() || !util::path_starts_with(path, base_dir)) {
    return std::string(path);
  }

#ifdef _WIN32
  std::string winpath;
  if (path.length() >= 3 && path[0] == '/') {
    if (isalpha(path[1]) && path[2] == '/') {
      // Transform /c/path... to c:/path...
      winpath = FMT("{}:/{}", path[1], path.substr(3));
      path = winpath;
    } else if (path[2] == ':') {
      // Transform /c:/path to c:/path
      winpath = std::string(path.substr(1));
      path = winpath;
    }
  }
#endif

  // The algorithm for computing relative paths below only works for existing
  // paths. If the path doesn't exist, find the first ancestor directory that
  // does exist and assemble the path again afterwards.

  std::vector<std::string> relpath_candidates;
  const auto original_path = path;
  DirEntry dir_entry(path);
  while (!dir_entry.exists()) {
    path = Util::dir_name(path);
    dir_entry = DirEntry(path);
  }
  const auto path_suffix = std::string(original_path.substr(path.length()));
  const auto real_path = util::real_path(path);

  const auto add_relpath_candidates = [&](auto p) {
    const std::string normalized_path =
      Util::normalize_abstract_absolute_path(p);
    relpath_candidates.push_back(
      Util::get_relative_path(actual_cwd, normalized_path));
    if (apparent_cwd != actual_cwd) {
      relpath_candidates.emplace_back(
        Util::get_relative_path(apparent_cwd, normalized_path));
    }
  };
  add_relpath_candidates(path);
  if (real_path != path) {
    add_relpath_candidates(real_path);
  }

  // Find best (i.e. shortest existing) match:
  std::sort(relpath_candidates.begin(),
            relpath_candidates.end(),
            [](const auto& path1, const auto& path2) {
              return path1.length() < path2.length();
            });
  for (const auto& relpath : relpath_candidates) {
    if (DirEntry(relpath).same_inode_as(dir_entry)) {
      return relpath + path_suffix;
    }
  }

  // No match so nothing else to do than to return the unmodified path.
  return std::string(original_path);
}

std::string
make_relative_path(const Context& ctx, std::string_view path)
{
  return make_relative_path(
    ctx.config.base_dir(), ctx.actual_cwd, ctx.apparent_cwd, path);
}

static std::string
do_normalize_abstract_absolute_path(std::string_view path)
{
  if (!util::is_absolute_path(path)) {
    return std::string(path);
  }

#ifdef _WIN32
  std::string drive(path.substr(0, 2));
  path = path.substr(2);
#endif

  std::string result = "/";
  const size_t npos = std::string_view::npos;
  size_t left = 1;

  while (true) {
    if (left >= path.length()) {
      break;
    }
    const auto right = path.find('/', left);
    std::string_view part =
      path.substr(left, right == npos ? npos : right - left);
    if (part == "..") {
      if (result.length() > 1) {
        // "/x/../part" -> "/part"
        result.erase(result.rfind('/', result.length() - 2) + 1);
      } else {
        // "/../part" -> "/part"
      }
    } else if (part == ".") {
      // "/x/." -> "/x"
    } else {
      result.append(part.begin(), part.end());
      if (result[result.length() - 1] != '/') {
        result += '/';
      }
    }
    if (right == npos) {
      break;
    }
    left = right + 1;
  }
  if (result.length() > 1) {
    result.erase(result.find_last_not_of('/') + 1);
  }

#ifdef _WIN32
  return drive + result;
#else
  return result;
#endif
}

std::string
normalize_abstract_absolute_path(std::string_view path)
{
#ifdef _WIN32
  std::string new_path(path);
  std::replace(new_path.begin(), new_path.end(), '\\', '/');
  return do_normalize_abstract_absolute_path(new_path);
#else
  return do_normalize_abstract_absolute_path(path);
#endif
}

std::string
normalize_concrete_absolute_path(const std::string& path)
{
  const auto normalized_path = normalize_abstract_absolute_path(path);
  return (normalized_path == path
          || DirEntry(normalized_path).same_inode_as(DirEntry(path)))
           ? normalized_path
           : path;
}

std::string_view
remove_extension(std::string_view path)
{
  return path.substr(0, path.length() - get_extension(path).length());
}

} // namespace Util
