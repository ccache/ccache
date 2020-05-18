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

#include "Config.hpp"
#include "Context.hpp"
#include "FormatNonstdStringView.hpp"
#include "logging.hpp"

#ifdef _WIN32
#  include "win32compat.hpp"
#endif

#include <algorithm>
#include <fstream>

using nonstd::string_view;

namespace {

size_t
path_max(const char* path)
{
#ifdef PATH_MAX
  (void)path;
  return PATH_MAX;
#elif defined(MAXPATHLEN)
  (void)path;
  return MAXPATHLEN;
#elif defined(_PC_PATH_MAX)
  long maxlen = pathconf(path, _PC_PATH_MAX);
  return maxlen >= 4096 ? maxlen : 4096;
#endif
}

template<typename T>
std::vector<T>
split_at(string_view input, const char* separators)
{
  assert(separators != nullptr && separators[0] != '\0');

  std::vector<T> result;

  size_t start = 0;
  while (start < input.size()) {
    size_t end = input.find_first_of(separators, start);

    if (end == string_view::npos) {
      result.emplace_back(input.data() + start, input.size() - start);
      break;
    } else if (start != end) {
      result.emplace_back(input.data() + start, end - start);
    }

    start = end + 1;
  }

  return result;
}

} // namespace

namespace Util {

string_view
base_name(string_view path)
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
change_extension(string_view path, string_view new_ext)
{
  string_view without_ext = Util::remove_extension(path);
  return std::string(without_ext).append(new_ext.data(), new_ext.length());
}

size_t
common_dir_prefix_length(string_view dir, string_view path)
{
  if (dir.empty() || path.empty() || dir == "/" || path == "/") {
    return 0;
  }

  assert(dir[0] == '/');
  assert(path[0] == '/');

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
#ifdef _WIN32
  const char delim[] = "/\\";
#else
  const char delim[] = "/";
#endif
  size_t n = path.find_last_of(delim);
  if (n == std::string::npos) {
    return ".";
  } else {
    return n == 0 ? "/" : path.substr(0, n);
  }
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

std::string
get_actual_cwd()
{
  char buffer[PATH_MAX];
  if (getcwd(buffer, sizeof(buffer))) {
#ifndef _WIN32
    return buffer;
#else
    std::string cwd = buffer;
    std::replace(cwd.begin(), cwd.end(), '\\', '/');
    return cwd;
#endif
  } else {
    return {};
  }
}

std::string
get_apparent_cwd(const std::string& actual_cwd)
{
#ifdef _WIN32
  return actual_cwd;
#else
  auto pwd = getenv("PWD");
  if (!pwd) {
    return actual_cwd;
  }

  auto pwd_stat = Stat::stat(pwd);
  auto cwd_stat = Stat::stat(actual_cwd);
  if (!pwd_stat || !cwd_stat || !pwd_stat.same_inode_as(cwd_stat)) {
    return actual_cwd;
  }
  std::string normalized_pwd = normalize_absolute_path(pwd);
  return normalized_pwd == pwd
             || Stat::stat(normalized_pwd).same_inode_as(pwd_stat)
           ? normalized_pwd
           : pwd;
#endif
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

std::string
get_relative_path(string_view dir, string_view path)
{
  assert(Util::is_absolute_path(dir));
  assert(Util::is_absolute_path(path));

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
is_absolute_path(string_view path)
{
#ifdef _WIN32
  if (path.length() >= 2 && path[1] == ':'
      && (path[2] == '/' || path[2] == '\\')) {
    return true;
  }
#endif
  return !path.empty() && path[0] == '/';
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
      winpath = fmt::format("{}:/{}", path[1], path.substr(3));
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
    Util::get_relative_path(ctx.apparent_cwd, normalized_path),
  };
  // Move best (= shortest) match first:
  if (relpath_candidates[0].length() > relpath_candidates[1].length()) {
    std::swap(relpath_candidates[0], relpath_candidates[1]);
  }

  for (const auto& relpath : relpath_candidates) {
    if (Stat::stat(relpath).same_inode_as(path_stat)) {
      return relpath + path_suffix;
    }
  }

  // No match so nothing else to do than to return the unmodified path.
  return std::string(original_path);
}

bool
matches_dir_prefix_or_file(nonstd::string_view dir_prefix_or_file,
                           nonstd::string_view path)
{
  return !dir_prefix_or_file.empty() && !path.empty()
         && dir_prefix_or_file.length() <= path.length()
         && path.starts_with(dir_prefix_or_file)
         && (dir_prefix_or_file.length() == path.length()
             || is_dir_separator(path[dir_prefix_or_file.length()])
             || is_dir_separator(dir_prefix_or_file.back()));
}

std::string
normalize_absolute_path(string_view path)
{
  if (!is_absolute_path(path)) {
    return std::string(path);
  }

#ifdef _WIN32
  if (path.find("\\") != string_view::npos) {
    std::string new_path(path);
    std::replace(new_path.begin(), new_path.end(), '\\', '/');
    return normalize_absolute_path(new_path);
  }

  std::string drive(path.substr(0, 2));
  path = path.substr(2);
#endif

  std::string result = "/";
  const size_t npos = string_view::npos;
  size_t left = 1;

  while (true) {
    if (left >= path.length()) {
      break;
    }
    const auto right = path.find('/', left);
    string_view part = path.substr(left, right == npos ? npos : right - left);
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

int
parse_int(const std::string& value)
{
  size_t end;
  long result;
  bool failed = false;
  try {
    result = std::stoi(value, &end, 10);
  } catch (std::exception&) {
    failed = true;
  }
  if (failed || end != value.size()) {
    throw Error(fmt::format("invalid integer: \"{}\"", value));
  }
  return result;
}

std::string
read_file(const std::string& path)
{
  std::ifstream file(path);
  if (!file) {
    throw Error(strerror(errno));
  }
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

#ifndef _WIN32
std::string
read_link(const std::string& path)
{
  size_t buffer_size = path_max(path.c_str());
  std::unique_ptr<char[]> buffer(new char[buffer_size]);
  ssize_t len = readlink(path.c_str(), buffer.get(), buffer_size - 1);
  if (len == -1) {
    return "";
  }
  buffer[len] = 0;
  return buffer.get();
}
#endif

std::string
real_path(const std::string& path, bool return_empty_on_error)
{
  const char* c_path = path.c_str();
  size_t buffer_size = path_max(c_path);
  std::unique_ptr<char[]> managed_buffer(new char[buffer_size]);
  char* buffer = managed_buffer.get();
  char* resolved = nullptr;

#if HAVE_REALPATH
  resolved = realpath(c_path, buffer);
#elif defined(_WIN32)
  if (c_path[0] == '/') {
    c_path++; // Skip leading slash.
  }
  HANDLE path_handle = CreateFile(c_path,
                                  GENERIC_READ,
                                  FILE_SHARE_READ,
                                  NULL,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  NULL);
  if (INVALID_HANDLE_VALUE != path_handle) {
#  ifdef HAVE_GETFINALPATHNAMEBYHANDLEW
    GetFinalPathNameByHandle(
      path_handle, buffer, buffer_size, FILE_NAME_NORMALIZED);
#  else
    GetFileNameFromHandle(path_handle, buffer, buffer_size);
#  endif
    CloseHandle(path_handle);
    resolved = buffer + 4; // Strip \\?\ from the file name.
  } else {
    snprintf(buffer, buffer_size, "%s", c_path);
    resolved = buffer;
  }
#else
  // Yes, there are such systems. This replacement relies on the fact that when
  // we call x_realpath we only care about symlinks.
  {
    ssize_t len = readlink(c_path, buffer, buffer_size - 1);
    if (len != -1) {
      buffer[len] = 0;
      resolved = buffer;
    }
  }
#endif

  return resolved ? resolved : (return_empty_on_error ? "" : path);
}

string_view
remove_extension(string_view path)
{
  return path.substr(0, path.length() - get_extension(path).length());
}

std::vector<string_view>
split_into_views(string_view s, const char* separators)
{
  return split_at<string_view>(s, separators);
}

std::vector<std::string>
split_into_strings(string_view s, const char* separators)
{
  return split_at<std::string>(s, separators);
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
#ifdef _DIRENT_HAVE_D_TYPE
      if (entry->d_type != DT_UNKNOWN) {
        is_dir = entry->d_type == DT_DIR;
      } else
#endif
      {
        auto stat = Stat::lstat(entry_path);
        if (!stat) {
          if (stat.error_number() == ENOENT || stat.error_number() == ESTALE) {
            continue;
          }
          throw Error(fmt::format("failed to lstat {}: {}",
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
    throw Error(
      fmt::format("failed to open directory {}: {}", path, strerror(errno)));
  }
}

bool
unlink_safe(const std::string& path, UnlinkLog unlink_log)
{
  int saved_errno = 0;

  // If path is on an NFS share, unlink isn't atomic, so we rename to a temp
  // file. We don't care if the temp file is trashed, so it's always safe to
  // unlink it first.
  std::string tmp_name = path + ".ccache.rm.tmp";

  bool success = true;
  if (x_rename(path.c_str(), tmp_name.c_str()) != 0) {
    success = false;
    saved_errno = errno;
  } else if (unlink(tmp_name.c_str()) != 0) {
    // It's OK if it was unlinked in a race.
    if (errno != ENOENT && errno != ESTALE) {
      success = false;
      saved_errno = errno;
    }
  }

  if (success || unlink_log == UnlinkLog::log_failure) {
    cc_log("Unlink %s via %s", path.c_str(), tmp_name.c_str());
    if (!success) {
      cc_log("Unlink failed: %s", strerror(saved_errno));
    }
  }

  errno = saved_errno;
  return success;
}

bool
unlink_tmp(const std::string& path, UnlinkLog unlink_log)
{
  int saved_errno = 0;

  bool success =
    unlink(path.c_str()) == 0 || (errno == ENOENT || errno == ESTALE);
  if (success || unlink_log == UnlinkLog::log_failure) {
    cc_log("Unlink %s", path.c_str());
    if (!success) {
      cc_log("Unlink failed: %s", strerror(saved_errno));
    }
  }

  errno = saved_errno;
  return success;
}

void
wipe_path(const std::string& path)
{
  if (!Stat::lstat(path)) {
    return;
  }
  traverse(path, [](const std::string& p, bool is_dir) {
    if (is_dir) {
      if (rmdir(p.c_str()) != 0 && errno != ENOENT && errno != ESTALE) {
        throw Error(fmt::format("failed to rmdir {}: {}", p, strerror(errno)));
      }
    } else if (unlink(p.c_str()) != 0 && errno != ENOENT && errno != ESTALE) {
      throw Error(fmt::format("failed to unlink {}: {}", p, strerror(errno)));
    }
  });
}

void
write_file(const std::string& path,
           const std::string& data,
           std::ios_base::openmode open_mode)
{
  open_mode |= std::ios::out;
  std::ofstream file(path, open_mode);
  if (!file) {
    throw Error(strerror(errno));
  }
  file << data;
}

} // namespace Util
