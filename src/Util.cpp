// Copyright (C) 2019 Joel Rosdahl and other contributors
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
#include "FormatNonstdStringView.hpp"
#include "ccache.hpp"

#include <algorithm>
#include <fstream>

using nonstd::string_view;

namespace {

void
get_cache_files_internal(const std::string& dir,
                         uint8_t level,
                         const Util::ProgressReceiver& progress_receiver,
                         std::vector<std::shared_ptr<CacheFile>>& files)
{
  DIR* d = opendir(dir.c_str());
  if (!d) {
    return;
  }

  std::vector<std::string> directories;
  dirent* de;
  while ((de = readdir(d))) {
    string_view name(de->d_name);
    if (name == "" || name == "." || name == ".." || name == "CACHEDIR.TAG"
        || name == "stats" || name.starts_with(".nfs")) {
      continue;
    }

    if (name.length() == 1) {
      directories.emplace_back(name);
    } else {
      files.push_back(
        std::make_shared<CacheFile>(fmt::format("{}/{}", dir, name)));
    }
  }
  closedir(d);

  if (level == 1) {
    progress_receiver(1.0 / (directories.size() + 1));
  }

  for (size_t i = 0; i < directories.size(); ++i) {
    get_cache_files_internal(
      dir + "/" + directories[i], level + 1, progress_receiver, files);
    if (level == 1) {
      progress_receiver(1.0 * (i + 1) / (directories.size() + 1));
    }
  }
}

} // namespace

namespace Util {

string_view
base_name(string_view path)
{
  size_t n = path.rfind('/');
#ifdef _WIN32
  size_t n2 = path.rfind('\\');
  if (n2 != std::string::npos && n2 > n) {
    n = n2;
  }
#endif
  return n == std::string::npos ? path : path.substr(n + 1);
}

bool
create_dir(string_view dir)
{
  std::string dir_str(dir);
  auto st = Stat::stat(dir_str);
  if (st) {
    if (st.is_directory()) {
      return true;
    } else {
      errno = ENOTDIR;
      return false;
    }
  } else {
    if (!create_dir(Util::dir_name(dir))) {
      return false;
    }
    int result = mkdir(dir_str.c_str(), 0777);
    // Treat an already existing directory as OK since the file system could
    // have changed in between calling stat and actually creating the
    // directory. This can happen when there are multiple instances of ccache
    // running and trying to create the same directory chain, which usually is
    // the case when the cache root does not initially exist. As long as one of
    // the processes creates the directories then our condition is satisfied
    // and we avoid a race condition.
    return result == 0 || errno == EEXIST;
  }
}

std::pair<int, std::string>
create_temp_fd(string_view path_prefix)
{
  char* tmp_path = x_strndup(path_prefix.data(), path_prefix.length());
  int fd = create_tmp_fd(&tmp_path);
  std::string actual_path = tmp_path;
  free(tmp_path);
  return {fd, actual_path};
}

string_view
dir_name(string_view path)
{
  size_t n = path.rfind('/');
#ifdef _WIN32
  size_t n2 = path.rfind('\\');
  if (n2 != std::string::npos && n2 > n) {
    n = n2;
  }
#endif
  if (n == std::string::npos) {
    return ".";
  }
  return n == 0 ? "/" : path.substr(0, n);
}

string_view
get_extension(string_view path)
{
#ifndef _WIN32
  const char stop_at_chars[] = "./";
#else
  const char stop_at_chars[] = "./\\";
#endif
  size_t pos = path.find_last_of(stop_at_chars);
  if (pos == string_view::npos || path.at(pos) == '/') {
    return {};
#ifdef _WIN32
  } else if (path.at(pos) == '\\') {
    return {};
#endif
  } else {
    return path.substr(pos);
  }
}

string_view
remove_extension(string_view path)
{
  return path.substr(0, path.length() - get_extension(path).length());
}

std::string
change_extension(string_view path, string_view new_ext)
{
  string_view without_ext = Util::remove_extension(path);
  return std::string(without_ext).append(new_ext.data(), new_ext.length());
}

string_view
get_truncated_base_name(string_view path, size_t max_length)
{
  string_view input_base = Util::base_name(path);
  size_t dot_pos = input_base.find('.');
  size_t truncate_pos =
    std::min(max_length, std::min(input_base.size(), dot_pos));
  return input_base.substr(0, truncate_pos);
}

bool
ends_with(string_view string, string_view suffix)
{
  return string.ends_with(suffix);
}

void
for_each_level_1_subdir(const std::string& cache_dir,
                        const SubdirVisitor& subdir_visitor,
                        const ProgressReceiver& progress_receiver)
{
  for (int i = 0; i <= 0xF; i++) {
    double progress = 1.0 * i / 16;
    progress_receiver(progress);
    std::string subdir_path = fmt::format("{}/{:x}", cache_dir, i);
    subdir_visitor(subdir_path, [&](double inner_progress) {
      progress_receiver(progress + inner_progress / 16);
    });
  }
  progress_receiver(1.0);
}

void
get_level_1_files(const std::string& dir,
                  const ProgressReceiver& progress_receiver,
                  std::vector<std::shared_ptr<CacheFile>>& files)
{
  get_cache_files_internal(dir, 1, progress_receiver, files);
}

std::string
get_path_in_cache(string_view cache_dir,
                  uint32_t levels,
                  string_view name,
                  string_view suffix)
{
  assert(levels >= 1 && levels <= 8);
  assert(levels < name.length());

  std::string path(cache_dir);
  path.reserve(path.size() + levels * 2 + 1 + name.length() - levels
               + suffix.length());

  unsigned level = 0;
  for (; level < levels; ++level) {
    path.push_back('/');
    path.push_back(name.at(level));
  }

  path.push_back('/');
  string_view name_remaining = name.substr(level);
  path.append(name_remaining.data(), name_remaining.length());
  path.append(suffix.data(), suffix.length());

  return path;
}

int
parse_int(const std::string& value)
{
  size_t end;
  long result;
  bool failed = false;
  try {
    result = std::stol(value, &end, 10);
  } catch (std::exception&) {
    failed = true;
  }
  if (failed || end != value.size() || result < std::numeric_limits<int>::min()
      || result > std::numeric_limits<int>::max()) {
    throw Error(fmt::format("invalid integer: \"{}\"", value));
  }
  return result;
}

std::string
read_file(const std::string& path)
{
  std::ifstream file(path);
  if (!file) {
    throw Error(fmt::format("{}: {}", path, strerror(errno)));
  }
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

bool
starts_with(string_view string, string_view prefix)
{
  return string.starts_with(prefix);
}

std::string
strip_whitespace(const std::string& string)
{
  auto is_space = [](int ch) { return std::isspace(ch); };
  auto start = std::find_if_not(string.begin(), string.end(), is_space);
  auto end = std::find_if_not(string.rbegin(), string.rend(), is_space).base();
  return start < end ? std::string(start, end) : std::string();
}

std::string
to_lowercase(const std::string& string)
{
  std::string result = string;
  std::transform(result.begin(), result.end(), result.begin(), tolower);
  return result;
}

// Write file data from a string.
void
write_file(const std::string& path, const std::string& data, bool binary)
{
  std::ofstream file(path,
                     binary ? std::ios::out | std::ios::binary : std::ios::out);
  if (!file) {
    throw Error(fmt::format("{}: {}", path, strerror(errno)));
  }
  file << data;
}

} // namespace Util
