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
#include "Fd.hpp"
#include "Logging.hpp"
#include "TemporaryFile.hpp"
#include "core/FormatNonstdStringView.hpp"
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

namespace {

// Search for the first match of the following regular expression:
//
//   \x1b\[[\x30-\x3f]*[\x20-\x2f]*[Km]
//
// The primary reason for not using std::regex is that it's not available for
// GCC 4.8. It's also a bit bloated. The reason for not using POSIX regex
// functionality is that it's are not available in MinGW.
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

size_t
path_max(const std::string& path)
{
#ifdef PATH_MAX
  (void)path;
  return PATH_MAX;
#elif defined(MAXPATHLEN)
  (void)path;
  return MAXPATHLEN;
#elif defined(_PC_PATH_MAX)
  long maxlen = pathconf(path.c_str(), _PC_PATH_MAX);
  return maxlen >= 4096 ? maxlen : 4096;
#endif
}

template<typename T>
std::vector<T>
split_at(string_view input, const char* separators)
{
  ASSERT(separators != nullptr && separators[0] != '\0');

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

std::string
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

} // namespace

namespace Util {

std::string
change_extension(string_view path, string_view new_ext)
{
  string_view without_ext = Util::remove_extension(path);
  return std::string(without_ext).append(new_ext.data(), new_ext.length());
}

#ifdef FILE_CLONING_SUPPORTED
void
clone_file(const std::string& src, const std::string& dest, bool via_tmp_file)
{
#  if defined(__linux__)
  Fd src_fd(open(src.c_str(), O_RDONLY));
  if (!src_fd) {
    throw Error("{}: {}", src, strerror(errno));
  }

  Fd dest_fd;
  std::string tmp_file;
  if (via_tmp_file) {
    TemporaryFile temp_file(dest);
    dest_fd = std::move(temp_file.fd);
    tmp_file = temp_file.path;
  } else {
    dest_fd =
      Fd(open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
    if (!dest_fd) {
      throw Error("{}: {}", src, strerror(errno));
    }
  }

  if (ioctl(*dest_fd, FICLONE, *src_fd) != 0) {
    throw Error(strerror(errno));
  }

  dest_fd.close();
  src_fd.close();

  if (via_tmp_file) {
    Util::rename(tmp_file, dest);
  }
#  elif defined(__APPLE__)
  (void)via_tmp_file;
  if (clonefile(src.c_str(), dest.c_str(), CLONE_NOOWNERCOPY) != 0) {
    throw Error(strerror(errno));
  }
#  else
  (void)src;
  (void)dest;
  (void)via_tmp_file;
  throw Error(strerror(EOPNOTSUPP));
#  endif
}
#endif // FILE_CLONING_SUPPORTED

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
      clone_file(source, dest, via_tmp_file);
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

size_t
common_dir_prefix_length(string_view dir, string_view path)
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

void
copy_fd(int fd_in, int fd_out)
{
  read_fd(fd_in,
          [=](const void* data, size_t size) { write_fd(fd_out, data, size); });
}

void
copy_file(const std::string& src, const std::string& dest, bool via_tmp_file)
{
  Fd src_fd(open(src.c_str(), O_RDONLY));
  if (!src_fd) {
    throw Error("{}: {}", src, strerror(errno));
  }

  Fd dest_fd;
  std::string tmp_file;
  if (via_tmp_file) {
    TemporaryFile temp_file(dest);
    dest_fd = std::move(temp_file.fd);
    tmp_file = temp_file.path;
  } else {
    dest_fd =
      Fd(open(dest.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_BINARY, 0666));
    if (!dest_fd) {
      throw Error("{}: {}", dest, strerror(errno));
    }
  }

  copy_fd(*src_fd, *dest_fd);
  dest_fd.close();
  src_fd.close();

  if (via_tmp_file) {
    Util::rename(tmp_file, dest);
  }
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

std::string
expand_environment_variables(const std::string& str)
{
  std::string result;
  const char* left = str.c_str();
  for (const char* right = left; *right; ++right) {
    if (*right == '$') {
      result.append(left, right - left);

      left = right + 1;
      bool curly = *left == '{';
      if (curly) {
        ++left;
      }
      right = left;
      while (isalnum(*right) || *right == '_') {
        ++right;
      }
      if (curly && *right != '}') {
        throw Error("syntax error: missing '}}' after \"{}\"", left);
      }
      if (right == left) {
        // Special case: don't consider a single $ the left of a variable.
        result += '$';
        --right;
      } else {
        std::string name(left, right - left);
        const char* value = getenv(name.c_str());
        if (!value) {
          throw Error("environment variable \"{}\" not set", name);
        }
        result += value;
        if (!curly) {
          --right;
        }
        left = right + 1;
      }
    }
  }
  result += left;
  return result;
}

int
fallocate(int fd, long new_size)
{
#ifdef HAVE_POSIX_FALLOCATE
  return posix_fallocate(fd, 0, new_size);
#else
  off_t saved_pos = lseek(fd, 0, SEEK_END);
  off_t old_size = lseek(fd, 0, SEEK_END);
  if (old_size == -1) {
    int err = errno;
    lseek(fd, saved_pos, SEEK_SET);
    return err;
  }
  if (old_size >= new_size) {
    lseek(fd, saved_pos, SEEK_SET);
    return 0;
  }
  long bytes_to_write = new_size - old_size;
  void* buf = calloc(bytes_to_write, 1);
  if (!buf) {
    lseek(fd, saved_pos, SEEK_SET);
    return ENOMEM;
  }
  int err = 0;
  try {
    write_fd(fd, buf, bytes_to_write);
  } catch (Error&) {
    err = errno;
  }
  lseek(fd, saved_pos, SEEK_SET);
  free(buf);
  return err;
#endif
}

void
for_each_level_1_subdir(const std::string& cache_dir,
                        const SubdirVisitor& visitor,
                        const ProgressReceiver& progress_receiver)
{
  for (int i = 0; i <= 0xF; i++) {
    double progress = 1.0 * i / 16;
    progress_receiver(progress);
    std::string subdir_path = FMT("{}/{:x}", cache_dir, i);
    visitor(subdir_path, [&](double inner_progress) {
      progress_receiver(progress + inner_progress / 16);
    });
  }
  progress_receiver(1.0);
}

std::string
format_argv_for_logging(const char* const* argv)
{
  std::string result;
  for (size_t i = 0; argv[i]; ++i) {
    if (i != 0) {
      result += ' ';
    }
    for (const char* arg = argv[i]; *arg; ++arg) {
      result += *arg;
    }
  }
  return result;
}

std::string
format_base16(const uint8_t* data, size_t size)
{
  static const char digits[] = "0123456789abcdef";
  std::string result;
  result.resize(2 * size);
  for (size_t i = 0; i < size; ++i) {
    result[i * 2] = digits[data[i] >> 4];
    result[i * 2 + 1] = digits[data[i] & 0xF];
  }
  return result;
}

std::string
format_base32hex(const uint8_t* data, size_t size)
{
  const size_t bytes_to_reserve = size * 8 / 5 + 1;
  std::string result(bytes_to_reserve, 0);
  const size_t actual_size = base32hex(&result[0], data, size);
  result.resize(actual_size);
  return result;
}

std::string
format_human_readable_size(uint64_t size)
{
  if (size >= 1000 * 1000 * 1000) {
    return FMT("{:.1f} GB", size / ((double)(1000 * 1000 * 1000)));
  } else if (size >= 1000 * 1000) {
    return FMT("{:.1f} MB", size / ((double)(1000 * 1000)));
  } else {
    return FMT("{:.1f} kB", size / 1000.0);
  }
}

std::string
format_parsable_size_with_suffix(uint64_t size)
{
  if (size >= 1000 * 1000 * 1000) {
    return FMT("{:.1f}G", size / ((double)(1000 * 1000 * 1000)));
  } else if (size >= 1000 * 1000) {
    return FMT("{:.1f}M", size / ((double)(1000 * 1000)));
  } else {
    return FMT("{}", size);
  }
}

void
ensure_dir_exists(nonstd::string_view dir)
{
  if (!create_dir(dir)) {
    throw Fatal("Failed to create directory {}: {}", dir, strerror(errno));
  }
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
get_home_directory()
{
  const char* p = getenv("HOME");
  if (p) {
    return p;
  }
#ifdef _WIN32
  p = getenv("APPDATA");
  if (p) {
    return p;
  }
#endif
#ifdef HAVE_GETPWUID
  {
    struct passwd* pwd = getpwuid(getuid());
    if (pwd) {
      return pwd->pw_dir;
    }
  }
#endif
  throw Fatal("Could not determine home directory from $HOME or getpwuid(3)");
}

const char*
get_hostname()
{
  static char hostname[260] = "";

  if (hostname[0]) {
    return hostname;
  }

  if (gethostname(hostname, sizeof(hostname)) != 0) {
    strcpy(hostname, "unknown");
  }
  hostname[sizeof(hostname) - 1] = 0;
  return hostname;
}

std::string
get_relative_path(string_view dir, string_view path)
{
  ASSERT(Util::is_absolute_path(dir));
  ASSERT(Util::is_absolute_path(path));

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
get_path_in_cache(string_view cache_dir, uint8_t level, string_view name)
{
  ASSERT(level >= 1 && level <= 8);
  ASSERT(name.length() >= level);

  std::string path(cache_dir);
  path.reserve(path.size() + level * 2 + 1 + name.length() - level);

  for (uint8_t i = 0; i < level; ++i) {
    path.push_back('/');
    path.push_back(name.at(i));
  }

  path.push_back('/');
  string_view name_remaining = name.substr(level);
  path.append(name_remaining.data(), name_remaining.length());

  return path;
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

#if defined(HAVE_LINUX_FS_H) || defined(HAVE_STRUCT_STATFS_F_FSTYPENAME)
int
is_nfs_fd(int fd, bool* is_nfs)
{
  struct statfs buf;
  if (fstatfs(fd, &buf) != 0) {
    return errno;
  }
#  ifdef HAVE_LINUX_FS_H
  *is_nfs = buf.f_type == NFS_SUPER_MAGIC;
#  else // Mac OS X and some other BSD flavors
  *is_nfs = strcmp(buf.f_fstypename, "nfs") == 0;
#  endif
  return 0;
}
#else
int
is_nfs_fd(int /*fd*/, bool* /*is_nfs*/)
{
  return -1;
}
#endif

bool
is_precompiled_header(string_view path)
{
  string_view ext = get_extension(path);
  return ext == ".gch" || ext == ".pch" || ext == ".pth"
         || get_extension(dir_name(path)) == ".gch";
}

optional<tm>
localtime(optional<time_t> time)
{
  time_t timestamp = time ? *time : ::time(nullptr);
  tm result;
  if (localtime_r(&timestamp, &result)) {
    return result;
  } else {
    return nullopt;
  }
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

bool
matches_dir_prefix_or_file(string_view dir_prefix_or_file, string_view path)
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

uint64_t
parse_duration(const std::string& duration)
{
  uint64_t factor = 0;
  char last_ch = duration.empty() ? '\0' : duration[duration.length() - 1];

  switch (last_ch) {
  case 'd':
    factor = 24 * 60 * 60;
    break;
  case 's':
    factor = 1;
    break;
  default:
    throw Error("invalid suffix (supported: d (day) and s (second)): \"{}\"",
                duration);
  }

  return factor * parse_unsigned(duration.substr(0, duration.length() - 1));
}

int64_t
parse_signed(const std::string& value,
             optional<int64_t> min_value,
             optional<int64_t> max_value,
             string_view description)
{
  std::string stripped_value = strip_whitespace(value);

  size_t end = 0;
  long long result = 0;
  bool failed = false;
  try {
    // Note: sizeof(long long) is guaranteed to be >= sizeof(int64_t)
    result = std::stoll(stripped_value, &end, 10);
  } catch (std::exception&) {
    failed = true;
  }
  if (failed || end != stripped_value.size()) {
    throw Error("invalid integer: \"{}\"", stripped_value);
  }

  int64_t min = min_value ? *min_value : INT64_MIN;
  int64_t max = max_value ? *max_value : INT64_MAX;
  if (result < min || result > max) {
    throw Error("{} must be between {} and {}", description, min, max);
  }
  return result;
}

uint64_t
parse_size(const std::string& value)
{
  errno = 0;

  char* p;
  double result = strtod(value.c_str(), &p);
  if (errno != 0 || result < 0 || p == value.c_str() || value.empty()) {
    throw Error("invalid size: \"{}\"", value);
  }

  while (isspace(*p)) {
    ++p;
  }

  if (*p != '\0') {
    unsigned multiplier = *(p + 1) == 'i' ? 1024 : 1000;
    switch (*p) {
    case 'T':
      result *= multiplier;
    // Fallthrough.
    case 'G':
      result *= multiplier;
    // Fallthrough.
    case 'M':
      result *= multiplier;
    // Fallthrough.
    case 'K':
    case 'k':
      result *= multiplier;
      break;
    default:
      throw Error("invalid size: \"{}\"", value);
    }
  } else {
    // Default suffix: G.
    result *= 1000 * 1000 * 1000;
  }
  return static_cast<uint64_t>(result);
}

uint64_t
parse_unsigned(const std::string& value,
               optional<uint64_t> min_value,
               optional<uint64_t> max_value,
               string_view description)
{
  std::string stripped_value = strip_whitespace(value);

  size_t end = 0;
  unsigned long long result = 0;
  bool failed = false;
  if (Util::starts_with(stripped_value, "-")) {
    failed = true;
  } else {
    try {
      // Note: sizeof(unsigned long long) is guaranteed to be >=
      // sizeof(uint64_t)
      result = std::stoull(stripped_value, &end, 10);
    } catch (std::exception&) {
      failed = true;
    }
  }
  if (failed || end != stripped_value.size()) {
    throw Error("invalid unsigned integer: \"{}\"", stripped_value);
  }

  uint64_t min = min_value ? *min_value : 0;
  uint64_t max = max_value ? *max_value : UINT64_MAX;
  if (result < min || result > max) {
    throw Error("{} must be between {} and {}", description, min, max);
  }
  return result;
}

bool
read_fd(int fd, DataReceiver data_receiver)
{
  ssize_t n;
  char buffer[READ_BUFFER_SIZE];
  while ((n = read(fd, buffer, sizeof(buffer))) != 0) {
    if (n == -1 && errno != EINTR) {
      break;
    }
    if (n > 0) {
      data_receiver(buffer, n);
    }
  }
  return n >= 0;
}

std::string
read_file(const std::string& path, size_t size_hint)
{
  if (size_hint == 0) {
    auto stat = Stat::stat(path);
    if (!stat) {
      throw Error(strerror(errno));
    }
    size_hint = stat.size();
  }

  // +1 to be able to detect EOF in the first read call
  size_hint = (size_hint < 1024) ? 1024 : size_hint + 1;

  Fd fd(open(path.c_str(), O_RDONLY | O_BINARY));
  if (!fd) {
    throw Error(strerror(errno));
  }

  ssize_t ret = 0;
  size_t pos = 0;
  std::string result;
  result.resize(size_hint);

  while (true) {
    if (pos > result.size()) {
      result.resize(2 * result.size());
    }
    const size_t max_read = result.size() - pos;
    ret = read(*fd, &result[pos], max_read);
    if (ret == 0 || (ret == -1 && errno != EINTR)) {
      break;
    }
    if (ret > 0) {
      pos += ret;
      if (static_cast<size_t>(ret) < max_read) {
        break;
      }
    }
  }

  if (ret == -1) {
    LOG("Failed reading {}", path);
    throw Error(strerror(errno));
  }

  result.resize(pos);
  return result;
}

#ifndef _WIN32
std::string
read_link(const std::string& path)
{
  size_t buffer_size = path_max(path);
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
  size_t buffer_size = path_max(path);
  std::unique_ptr<char[]> managed_buffer(new char[buffer_size]);
  char* buffer = managed_buffer.get();
  char* resolved = nullptr;

#ifdef HAVE_REALPATH
  resolved = realpath(path.c_str(), buffer);
#elif defined(_WIN32)
  const char* c_path = path.c_str();
  if (c_path[0] == '/') {
    c_path++; // Skip leading slash.
  }
  HANDLE path_handle = CreateFile(c_path,
                                  GENERIC_READ,
                                  FILE_SHARE_READ,
                                  nullptr,
                                  OPEN_EXISTING,
                                  FILE_ATTRIBUTE_NORMAL,
                                  nullptr);
  if (INVALID_HANDLE_VALUE != path_handle) {
    bool ok = GetFinalPathNameByHandle(
      path_handle, buffer, buffer_size, FILE_NAME_NORMALIZED);
    CloseHandle(path_handle);
    if (!ok) {
      return path;
    }
    resolved = buffer + 4; // Strip \\?\ from the file name.
  } else {
    snprintf(buffer, buffer_size, "%s", c_path);
    resolved = buffer;
  }
#else
  // Yes, there are such systems. This replacement relies on the fact that when
  // we call x_realpath we only care about symlinks.
  {
    ssize_t len = readlink(path.c_str(), buffer, buffer_size - 1);
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

void
rename(const std::string& oldpath, const std::string& newpath)
{
#ifndef _WIN32
  if (::rename(oldpath.c_str(), newpath.c_str()) != 0) {
    throw Error(
      "failed to rename {} to {}: {}", oldpath, newpath, strerror(errno));
  }
#else
  // Windows' rename() won't overwrite an existing file, so need to use
  // MoveFileEx instead.
  if (!MoveFileExA(
        oldpath.c_str(), newpath.c_str(), MOVEFILE_REPLACE_EXISTING)) {
    DWORD error = GetLastError();
    throw Error("failed to rename {} to {}: {}",
                oldpath,
                newpath,
                Win32Util::error_message(error));
  }
#endif
}

bool
same_program_name(nonstd::string_view program_name,
                  nonstd::string_view canonical_program_name)
{
#ifdef _WIN32
  std::string lowercase_program_name = Util::to_lowercase(program_name);
  return lowercase_program_name == canonical_program_name
         || lowercase_program_name == FMT("{}.exe", canonical_program_name);
#else
  return program_name == canonical_program_name;
#endif
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
set_cloexec_flag(int fd)
{
#ifndef _WIN32
  int flags = fcntl(fd, F_GETFD, 0);
  if (flags >= 0) {
    fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
  }
#else
  (void)fd;
#endif
}

void
setenv(const std::string& name, const std::string& value)
{
#ifdef HAVE_SETENV
  ::setenv(name.c_str(), value.c_str(), true);
#else
  char* string;
  asprintf(&string, "%s=%s", name.c_str(), value.c_str());
  putenv(string); // Leak to environment.
#endif
}

std::vector<string_view>
split_into_views(string_view input, const char* separators)
{
  return split_at<string_view>(input, separators);
}

std::vector<std::string>
split_into_strings(string_view input, const char* separators)
{
  return split_at<std::string>(input, separators);
}

std::string
strip_ansi_csi_seqs(string_view string)
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

std::string
strip_whitespace(string_view string)
{
  auto is_space = [](int ch) { return std::isspace(ch); };
  auto start = std::find_if_not(string.begin(), string.end(), is_space);
  auto end = std::find_if_not(string.rbegin(), string.rend(), is_space).base();
  return start < end ? std::string(start, end) : std::string();
}

std::string
to_lowercase(string_view string)
{
  std::string result;
  result.resize(string.length());
  std::transform(string.begin(), string.end(), result.begin(), tolower);
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
          throw Error("failed to lstat {}: {}",
                      entry_path,
                      strerror(stat.error_number()));
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
    throw Error("failed to open directory {}: {}", path, strerror(errno));
  }
}

#else // If not available, use the C++17 std::filesystem implementation.

void
traverse(const std::string& path, const TraverseVisitor& visitor)
{
  if (std::filesystem::is_directory(path)) {
    for (auto&& p : std::filesystem::directory_iterator(path)) {
      std::string entry = p.path().string();

      if (p.is_directory()) {
        traverse(entry, visitor);
      } else {
        visitor(entry, false);
      }
    }
    visitor(path, true);
  } else if (std::filesystem::exists(path)) {
    visitor(path, false);
  } else {
    throw Error("failed to open directory {}: {}", path, strerror(errno));
  }
}

#endif

bool
unlink_safe(const std::string& path, UnlinkLog unlink_log)
{
  int saved_errno = 0;

  // If path is on an NFS share, unlink isn't atomic, so we rename to a temp
  // file. We don't care if the temp file is trashed, so it's always safe to
  // unlink it first.
  std::string tmp_name = path + ".ccache.rm.tmp";

  bool success = true;
  try {
    Util::rename(path, tmp_name);
  } catch (Error&) {
    success = false;
    saved_errno = errno;
  }
  if (success && unlink(tmp_name.c_str()) != 0) {
    // It's OK if it was unlinked in a race.
    if (errno != ENOENT && errno != ESTALE) {
      success = false;
      saved_errno = errno;
    }
  }
  if (success || unlink_log == UnlinkLog::log_failure) {
    LOG("Unlink {} via {}", path, tmp_name);
    if (!success) {
      LOG("Unlink failed: {}", strerror(saved_errno));
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
  saved_errno = errno;
  if (success || unlink_log == UnlinkLog::log_failure) {
    LOG("Unlink {}", path);
    if (!success) {
      LOG("Unlink failed: {}", strerror(saved_errno));
    }
  }

  errno = saved_errno;
  return success;
}

void
unsetenv(const std::string& name)
{
#ifdef HAVE_UNSETENV
  ::unsetenv(name.c_str());
#else
  putenv(strdup(name.c_str())); // Leak to environment.
#endif
}

void
update_mtime(const std::string& path)
{
#ifdef HAVE_UTIMES
  utimes(path.c_str(), nullptr);
#else
  utime(path.c_str(), nullptr);
#endif
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
        throw Error("failed to rmdir {}: {}", p, strerror(errno));
      }
    } else if (unlink(p.c_str()) != 0 && errno != ENOENT && errno != ESTALE) {
      throw Error("failed to unlink {}: {}", p, strerror(errno));
    }
  });
}

void
write_fd(int fd, const void* data, size_t size)
{
  ssize_t written = 0;
  do {
    ssize_t count =
      write(fd, static_cast<const uint8_t*>(data) + written, size - written);
    if (count == -1) {
      if (errno != EAGAIN && errno != EINTR) {
        throw Error(strerror(errno));
      }
    } else {
      written += count;
    }
  } while (static_cast<size_t>(written) < size);
}

void
write_file(const std::string& path,
           const std::string& data,
           std::ios_base::openmode open_mode)
{
  if (path.empty()) {
    throw Error("No such file or directory");
  }

  open_mode |= std::ios::out;
  std::ofstream file(path, open_mode);
  if (!file) {
    throw Error(strerror(errno));
  }
  file << data;
}

} // namespace Util
