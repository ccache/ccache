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
#include "Fd.hpp"
#include "Logging.hpp"
#include "TemporaryFile.hpp"
#include "Win32Util.hpp"

#include <Config.hpp>
#include <Finalizer.hpp>
#include <Stat.hpp>
#include <core/exceptions.hpp>
#include <core/wincompat.hpp>
#include <fmtmacros.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>
#include <util/path.hpp>
#include <util/string.hpp>

#ifdef HAVE_DIRENT_H
#  include <dirent.h>
#endif

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#include <fcntl.h>

using IncludeDelimiter = util::Tokenizer::IncludeDelimiter;

namespace fs = util::filesystem;

namespace {

// Search for the first match of the following regular expression:
//
//   \x1b\[[\x30-\x3f]*[\x20-\x2f]*[Km]
//
// The primary reason for not using std::regex is that it's not available for
// GCC 4.8. It's also a bit bloated. The reason for not using POSIX regex
// functionality is that it's are not available in MinGW.
std::string_view
find_first_ansi_csi_seq(std::string_view string)
{
  size_t pos = 0;
  while (pos < string.length() && string[pos] != 0x1b) {
    ++pos;
  }
  if (pos + 1 >= string.length() || string[pos + 1] != '[') {
    return {};
  }
  size_t start = pos;
  pos += 2;
  while (pos < string.length()
         && (string[pos] >= 0x30 && string[pos] <= 0x3f)) {
    ++pos;
  }
  while (pos < string.length()
         && (string[pos] >= 0x20 && string[pos] <= 0x2f)) {
    ++pos;
  }
  if (pos < string.length() && (string[pos] == 'K' || string[pos] == 'm')) {
    return string.substr(start, pos + 1 - start);
  } else {
    return {};
  }
}

std::string
rewrite_stderr_to_absolute_paths(std::string_view text)
{
  static const std::string in_file_included_from = "In file included from ";

  std::string result;
  using util::Tokenizer;
  for (auto line : Tokenizer(text,
                             "\n",
                             Tokenizer::Mode::include_empty,
                             Tokenizer::IncludeDelimiter::yes)) {
    // Rewrite <path> to <absolute path> in the following two cases, where X may
    // be optional ANSI CSI sequences:
    //
    // In file included from X<path>X:1:
    // X<path>X:1:2: ...

    if (util::starts_with(line, in_file_included_from)) {
      result += in_file_included_from;
      line = line.substr(in_file_included_from.length());
    }
    while (!line.empty() && line[0] == 0x1b) {
      auto csi_seq = find_first_ansi_csi_seq(line);
      result.append(csi_seq.data(), csi_seq.length());
      line = line.substr(csi_seq.length());
    }
    size_t path_end = line.find(':');
    if (path_end == std::string_view::npos) {
      result.append(line.data(), line.length());
    } else {
      std::string path(line.substr(0, path_end));
      if (Stat::stat(path)) {
        result += util::real_path(path);
        auto tail = line.substr(path_end);
        result.append(tail.data(), tail.length());
      } else {
        result.append(line.data(), line.length());
      }
    }
  }
  return result;
}

} // namespace

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
  Stat path_stat;
  while (!(path_stat = Stat::stat(std::string(path)))) {
    path = Util::dir_name(path);
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
    if (Stat::stat(relpath).same_inode_as(path_stat)) {
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

bool
matches_dir_prefix_or_file(std::string_view dir_prefix_or_file,
                           std::string_view path)
{
  return !dir_prefix_or_file.empty() && !path.empty()
         && dir_prefix_or_file.length() <= path.length()
         && util::starts_with(path, dir_prefix_or_file)
         && (dir_prefix_or_file.length() == path.length()
             || is_dir_separator(path[dir_prefix_or_file.length()])
             || is_dir_separator(dir_prefix_or_file.back()));
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
          || Stat::stat(normalized_path).same_inode_as(Stat::stat(path)))
           ? normalized_path
           : path;
}

std::string_view
remove_extension(std::string_view path)
{
  return path.substr(0, path.length() - get_extension(path).length());
}

void
send_to_fd(const Context& ctx, std::string_view text, int fd)
{
  std::string_view text_to_send = text;
  std::string modified_text;

#ifdef _WIN32
  // stdout/stderr are normally opened in text mode, which would convert
  // newlines a second time since we treat output as binary data. Make sure to
  // switch to binary mode.
  int oldmode = _setmode(fd, _O_BINARY);
  Finalizer binary_mode_restorer([=] { _setmode(fd, oldmode); });
#endif

  if (ctx.args_info.strip_diagnostics_colors) {
    try {
      modified_text = strip_ansi_csi_seqs(text);
      text_to_send = modified_text;
    } catch (const core::Error&) {
      // Ignore.
    }
  }

  if (ctx.config.absolute_paths_in_stderr()) {
    modified_text = rewrite_stderr_to_absolute_paths(text_to_send);
    text_to_send = modified_text;
  }

  const auto result =
    util::write_fd(fd, text_to_send.data(), text_to_send.length());
  if (!result) {
    throw core::Error(FMT("Failed to write to {}: {}", fd, result.error()));
  }
}

std::string
strip_ansi_csi_seqs(std::string_view string)
{
  size_t pos = 0;
  std::string result;

  while (true) {
    auto seq_span = find_first_ansi_csi_seq(string.substr(pos));
    auto data_start = string.data() + pos;
    auto data_length =
      seq_span.empty() ? string.length() - pos : seq_span.data() - data_start;
    result.append(data_start, data_length);
    if (seq_span.empty()) {
      // Reached tail.
      break;
    }
    pos += data_length + seq_span.length();
  }

  return result;
}

#ifdef HAVE_DIRENT_H

void
traverse(const std::string& path, const TraverseVisitor& visitor)
{
  DIR* dir = opendir(path.c_str());
  if (dir) {
    struct dirent* entry;
    while ((entry = readdir(dir))) {
      if (strcmp(entry->d_name, "") == 0 || strcmp(entry->d_name, ".") == 0
          || strcmp(entry->d_name, "..") == 0) {
        continue;
      }

      std::string entry_path = path + "/" + entry->d_name;
      bool is_dir;
#  ifdef _DIRENT_HAVE_D_TYPE
      if (entry->d_type != DT_UNKNOWN) {
        is_dir = entry->d_type == DT_DIR;
      } else
#  endif
      {
        auto stat = Stat::lstat(entry_path);
        if (!stat) {
          if (stat.error_number() == ENOENT || stat.error_number() == ESTALE) {
            continue;
          }
          throw core::Error(FMT("failed to lstat {}: {}",
                                entry_path,
                                strerror(stat.error_number())));
        }
        is_dir = stat.is_directory();
      }
      if (is_dir) {
        traverse(entry_path, visitor);
      } else {
        visitor(entry_path, false);
      }
    }
    closedir(dir);
    visitor(path, true);
  } else if (errno == ENOTDIR) {
    visitor(path, false);
  } else {
    throw core::Error(
      FMT("failed to open directory {}: {}", path, strerror(errno)));
  }
}

#else // If not available, use the C++17 std::filesystem implementation.

void
traverse(const std::string& path, const TraverseVisitor& visitor)
{
  if (fs::is_directory(path)) {
    for (auto&& p : fs::directory_iterator(path)) {
      std::string entry = p.path().string();

      if (p.is_directory()) {
        traverse(entry, visitor);
      } else {
        visitor(entry, false);
      }
    }
    visitor(path, true);
  } else if (fs::exists(path)) {
    visitor(path, false);
  } else {
    throw core::Error(
      FMT("failed to open directory {}: {}", path, strerror(errno)));
  }
}

#endif

} // namespace Util
