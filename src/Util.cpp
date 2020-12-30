// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include "Context.hpp"
#include "core/Config.hpp"
#include "core/Fd.hpp"
#include "core/FormatNonstdStringView.hpp"
#include "core/Logging.hpp"
#include "core/TemporaryFile.hpp"
#include "core/Util.hpp"
#include "core/fmtmacros.hpp"

extern "C" {
#include "third_party/base32hex.h"
}

#include <algorithm>
#include <fstream>

#ifndef HAVE_DIRENT_H
#  include <filesystem>
#endif

#ifdef HAVE_PWD_H
#  include <pwd.h>
#endif

#ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
#endif

#ifdef HAVE_LINUX_FS_H
#  include <linux/magic.h>
#  include <sys/statfs.h>
#elif defined(HAVE_STRUCT_STATFS_F_FSTYPENAME)
#  include <sys/mount.h>
#  include <sys/param.h>
#endif

#ifdef _WIN32
#  include "Win32Util.hpp"
#endif

#ifdef __linux__
#  ifdef HAVE_SYS_IOCTL_H
#    include <sys/ioctl.h>
#  endif
#  ifdef HAVE_LINUX_FS_H
#    include <linux/fs.h>
#    ifndef FICLONE
#      define FICLONE _IOW(0x94, 9, int)
#    endif
#    define FILE_CLONING_SUPPORTED 1
#  endif
#endif

#ifdef __APPLE__
#  ifdef HAVE_SYS_CLONEFILE_H
#    include <sys/clonefile.h>
#    ifdef CLONE_NOOWNERCOPY
#      define FILE_CLONING_SUPPORTED 1
#    endif
#  endif
#endif

using nonstd::nullopt;
using nonstd::optional;
using nonstd::string_view;

namespace Util {

void
clone_hard_link_or_copy_file(const Context& ctx,
                             const std::string& source,
                             const std::string& dest,
                             bool via_tmp_file)
{
  if (ctx.config.file_clone()) {
#ifdef FILE_CLONING_SUPPORTED
    LOG("Cloning {} to {}", source, dest);
    try {
      Util::clone_file(source, dest, via_tmp_file);
      return;
    } catch (Error& e) {
      LOG("Failed to clone: {}", e.what());
    }
#else
    LOG("Not cloning {} to {} since it's unsupported", source, dest);
#endif
  }
  if (ctx.config.hard_link()) {
    unlink(dest.c_str());
    LOG("Hard linking {} to {}", source, dest);
    int ret = link(source.c_str(), dest.c_str());
    if (ret == 0) {
      if (chmod(dest.c_str(), 0444) != 0) {
        LOG("Failed to chmod: {}", strerror(errno));
      }
      return;
    }
    LOG("Failed to hard link: {}", strerror(errno));
  }

  LOG("Copying {} to {}", source, dest);
  copy_file(source, dest, via_tmp_file);
}

std::string
make_relative_path(const Context& ctx, string_view path)
{
  if (ctx.config.base_dir().empty()
      || !Util::starts_with(path, ctx.config.base_dir())) {
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
  string_view original_path = path;
  std::string path_suffix;
  Stat path_stat;
  while (!(path_stat = Stat::stat(std::string(path)))) {
    path = Util::dir_name(path);
  }
  path_suffix = std::string(original_path.substr(path.length()));

  std::string path_str(path);
  std::string normalized_path = Util::normalize_absolute_path(path_str);
  std::vector<std::string> relpath_candidates = {
    Util::get_relative_path(ctx.actual_cwd, normalized_path),
  };
  if (ctx.apparent_cwd != ctx.actual_cwd) {
    relpath_candidates.emplace_back(
      Util::get_relative_path(ctx.apparent_cwd, normalized_path));
    // Move best (= shortest) match first:
    if (relpath_candidates[0].length() > relpath_candidates[1].length()) {
      std::swap(relpath_candidates[0], relpath_candidates[1]);
    }
  }

  for (const auto& relpath : relpath_candidates) {
    if (Stat::stat(relpath).same_inode_as(path_stat)) {
      return relpath + path_suffix;
    }
  }

  // No match so nothing else to do than to return the unmodified path.
  return std::string(original_path);
}

string_view
find_first_ansi_csi_seq(string_view string)
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

static std::string
rewrite_stderr_to_absolute_paths(string_view text)
{
  static const std::string in_file_included_from = "In file included from ";

  std::string result;
  for (auto line : Util::split_into_views(text, "\n")) {
    // Rewrite <path> to <absolute path> in the following two cases, where X may
    // be optional ANSI CSI sequences:
    //
    // In file included from X<path>X:1:
    // X<path>X:1:2: ...

    if (Util::starts_with(line, in_file_included_from)) {
      result += in_file_included_from;
      line = line.substr(in_file_included_from.length());
    }
    while (!line.empty() && line[0] == 0x1b) {
      auto csi_seq = find_first_ansi_csi_seq(line);
      result.append(csi_seq.data(), csi_seq.length());
      line = line.substr(csi_seq.length());
    }
    size_t path_end = line.find(':');
    if (path_end == string_view::npos) {
      result.append(line.data(), line.length());
    } else {
      std::string path(line.substr(0, path_end));
      if (Stat::stat(path)) {
        result += Util::real_path(path);
        auto tail = line.substr(path_end);
        result.append(tail.data(), tail.length());
      } else {
        result.append(line.data(), line.length());
      }
    }
    result += '\n';
  }
  return result;
}

void
send_to_stderr(const Context& ctx, const std::string& text)
{
  const std::string* text_to_send = &text;
  std::string modified_text;

  if (ctx.args_info.strip_diagnostics_colors) {
    try {
      modified_text = strip_ansi_csi_seqs(text);
      text_to_send = &modified_text;
    } catch (const Error&) {
      // Fall through
    }
  }

  if (ctx.config.absolute_paths_in_stderr()) {
    modified_text = rewrite_stderr_to_absolute_paths(*text_to_send);
    text_to_send = &modified_text;
  }

  try {
    write_fd(STDERR_FILENO, text_to_send->data(), text_to_send->length());
  } catch (Error& e) {
    throw Error("Failed to write to stderr: {}", e.what());
  }
}

void
get_level_1_files(const std::string& dir,
                  const ProgressReceiver& progress_receiver,
                  std::vector<std::shared_ptr<CacheFile>>& files)
{
  if (!Stat::stat(dir)) {
    return;
  }

  size_t level_2_directories = 0;

  Util::traverse(dir, [&](const std::string& path, bool is_dir) {
    auto name = Util::base_name(path);
    if (name == "CACHEDIR.TAG" || name == "stats" || name.starts_with(".nfs")) {
      return;
    }

    if (!is_dir) {
      files.push_back(std::make_shared<CacheFile>(path));
    } else if (path != dir
               && path.find('/', dir.size() + 1) == std::string::npos) {
      ++level_2_directories;
      progress_receiver(level_2_directories / 16.0);
    }
  });

  progress_receiver(1.0);
}

} // namespace Util
