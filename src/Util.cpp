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

#include "ccache.hpp"
#include "util.hpp"

#include <algorithm>
#include <fstream>

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
    std::string name = de->d_name;
    if (name == "" || name == "." || name == ".." || name == "CACHEDIR.TAG"
        || name == "stats" || Util::starts_with(name, ".nfs")) {
      continue;
    }

    if (name.length() == 1) {
      directories.push_back(name);
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

std::string
base_name(const std::string& path)
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
create_dir(const std::string& dir)
{
  struct stat st;
  if (stat(dir.c_str(), &st) == 0) {
    if (S_ISDIR(st.st_mode)) {
      return true;
    } else {
      errno = ENOTDIR;
      return false;
    }
  } else {
    if (!create_dir(Util::dir_name(dir))) {
      return false;
    }
    int result = mkdir(dir.c_str(), 0777);
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
create_temp_fd(const std::string& path_prefix)
{
  char* tmp_path = x_strdup(path_prefix.c_str());
  int fd = create_tmp_fd(&tmp_path);
  std::string actual_path = tmp_path;
  free(tmp_path);
  return {fd, actual_path};
}

std::string
dir_name(const std::string& path)
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

bool
ends_with(const std::string& string, const std::string& suffix)
{
  return suffix.length() <= string.length()
         && string.compare(
              string.length() - suffix.length(), suffix.length(), suffix)
              == 0;
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
starts_with(const std::string& string, const std::string& prefix)
{
  return prefix.length() <= string.length()
         && string.compare(0, prefix.length(), prefix) == 0;
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
