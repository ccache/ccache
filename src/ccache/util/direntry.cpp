// Copyright (C) 2019-2024 Joel Rosdahl and other contributors
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

#include "direntry.hpp"

#include <ccache/util/defer.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/pathstring.hpp>
#include <ccache/util/wincompat.hpp>

#ifdef _WIN32
#  include <win32/winerror_to_errno.h>
#endif

#include <cstring>

namespace {

#ifdef _WIN32

template<typename Proc>
Proc*
get_proc_address(HMODULE module, const char* proc_name)
{
#  if __GNUC__ >= 8
#    pragma GCC diagnostic push
#    pragma GCC diagnostic ignored "-Wcast-function-type"
#  endif
#  if __clang_major__ >= 19
#    pragma clang diagnostic push
#    pragma clang diagnostic ignored "-Wcast-function-type-mismatch"
#  endif
  return reinterpret_cast<Proc*>(GetProcAddress(module, proc_name));
#  if __clang_major__ >= 19
#    pragma clang diagnostic pop
#  endif
#  if __GNUC__ >= 8
#    pragma GCC diagnostic pop
#  endif
}

// Returns the last NTSTATUS code. (These can be more specific than the
// corresponding Win32 error code.)
NTSTATUS
get_last_ntstatus()
{
  static auto* get_last_ntstatus_fn = get_proc_address<NTSTATUS NTAPI()>(
    GetModuleHandleA("ntdll.dll"), "RtlGetLastNtStatus");
  return get_last_ntstatus_fn();
}

uint16_t
win32_file_attributes_to_stat_mode(DWORD attr)
{
  uint16_t m = 0;
  if (attr & FILE_ATTRIBUTE_DIRECTORY) {
    m |= S_IFDIR | 0111;
  } else {
    m |= S_IFREG;
  }
  if (attr & FILE_ATTRIBUTE_READONLY) {
    m |= 0444;
  } else {
    m |= 0666;
  }
  return m;
}

struct timespec
win32_filetime_to_timespec(FILETIME ft)
{
  static const int64_t SECS_BETWEEN_EPOCHS = 11644473600;
  uint64_t v =
    (static_cast<uint64_t>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;

  struct timespec ts = {};
  ts.tv_sec = (v / 10000000) - SECS_BETWEEN_EPOCHS;
  ts.tv_nsec = (v % 10000000) * 100;
  return ts;
}

void
win32_file_information_to_stat(const BY_HANDLE_FILE_INFORMATION& file_info,
                               const FILE_ATTRIBUTE_TAG_INFO& reparse_info,
                               const char* path,
                               util::DirEntry::stat_t* st)
{
  st->st_dev = file_info.dwVolumeSerialNumber;
  st->st_ino = (static_cast<uint64_t>(file_info.nFileIndexHigh) << 32)
               | file_info.nFileIndexLow;
  st->st_mode = win32_file_attributes_to_stat_mode(file_info.dwFileAttributes);
  st->st_nlink = file_info.nNumberOfLinks;
  st->st_size = (static_cast<uint64_t>(file_info.nFileSizeHigh) << 32)
                | file_info.nFileSizeLow;
  st->st_atim = win32_filetime_to_timespec(file_info.ftLastAccessTime);
  st->st_mtim = win32_filetime_to_timespec(file_info.ftLastWriteTime);
  st->st_ctim = win32_filetime_to_timespec(file_info.ftCreationTime);
  st->st_file_attributes = file_info.dwFileAttributes;
  st->st_reparse_tag = reparse_info.ReparseTag;

  if ((file_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
      && IsReparseTagNameSurrogate(reparse_info.ReparseTag)) {
    // Don't consider name surrogate reparse points (symlinks and junctions) as
    // regular files or directories.
    st->st_mode &= ~S_IFMT;
    // Set S_IFLNK bit if this is a Windows symlink.
    st->st_mode |=
      (reparse_info.ReparseTag == IO_REPARSE_TAG_SYMLINK) ? S_IFLNK : 0;
  }

  // Add the executable permission using the same goofy logic used by
  // Microsoft's C runtime. See ucrt\filesystem\stat.cpp in the Windows 10 SDK.
  if (!(file_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
    if (const char* file_extension = strchr(path, '.')) {
      if (_stricmp(file_extension, ".exe") == 0
          || _stricmp(file_extension, ".bat") == 0
          || _stricmp(file_extension, ".cmd") == 0
          || _stricmp(file_extension, ".com") == 0) {
        st->st_mode |= 0111;
      }
    }
  }
}

bool
win32_stat_impl(const char* path,
                bool traverse_links,
                util::DirEntry::stat_t* st)
{
  *st = {};

  DWORD access = FILE_READ_ATTRIBUTES;
  DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
  DWORD flags = traverse_links
                  ? FILE_FLAG_BACKUP_SEMANTICS
                  : FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT;

  HANDLE handle = CreateFileA(
    path, access, share_mode, nullptr, OPEN_EXISTING, flags, nullptr);
  if (handle == INVALID_HANDLE_VALUE
      && GetLastError() == ERROR_INVALID_PARAMETER) {
    // For some special paths (e.g. "CON") FILE_READ_ATTRIBUTES is insufficient.
    access |= GENERIC_READ;
    handle = CreateFileA(
      path, access, share_mode, nullptr, OPEN_EXISTING, flags, nullptr);
  }

  if (handle == INVALID_HANDLE_VALUE) {
    if (GetLastError() == ERROR_ACCESS_DENIED
        && get_last_ntstatus() == STATUS_DELETE_PENDING) {
      // Treat a 'pending delete' as a nonexistent file.
      SetLastError(ERROR_FILE_NOT_FOUND);
    }
    return false;
  }

  DEFER(CloseHandle(handle));

  switch (GetFileType(handle)) {
  case FILE_TYPE_DISK: {
    FILE_ATTRIBUTE_TAG_INFO reparse_info = {};
    if (!traverse_links
        && GetFileInformationByHandleEx(
          handle, FileAttributeTagInfo, &reparse_info, sizeof(reparse_info))
        && (reparse_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        && !IsReparseTagNameSurrogate(reparse_info.ReparseTag)) {
      // We've opened a non name-surrogate reparse point. These types
      // of reparse points should be followed even by lstat().
      return win32_stat_impl(path, true, st);
    }

    BY_HANDLE_FILE_INFORMATION file_info = {};
    if (GetFileInformationByHandle(handle, &file_info)) {
      win32_file_information_to_stat(file_info, reparse_info, path, st);
    } else if (GetLastError() == ERROR_INVALID_FUNCTION) {
      st->st_mode |= S_IFBLK;
    } else {
      return false;
    }
    return true;
  }

  case FILE_TYPE_CHAR:
    st->st_mode = S_IFCHR;
    return true;

  case FILE_TYPE_PIPE:
    st->st_mode = S_IFIFO;
    return true;

  case FILE_TYPE_UNKNOWN:
  default:
    return true;
  }
}

int
lstat_func(const char* path, util::DirEntry::stat_t* st)
{
  bool ok = win32_stat_impl(path, false, st);
  if (ok) {
    return 0;
  }
  errno = winerror_to_errno(GetLastError());
  return -1;
}

int
stat_func(const char* path, util::DirEntry::stat_t* st)
{
  bool ok = win32_stat_impl(path, true, st);
  if (ok) {
    return 0;
  }
  errno = winerror_to_errno(GetLastError());
  return -1;
}

#else

auto lstat_func = ::lstat;
auto stat_func = ::stat;

#endif // _WIN32

} // namespace

namespace util {

uint64_t
DirEntry::size_on_disk() const
{
  return util::likely_size_on_disk(size());
}

const DirEntry::stat_t&
DirEntry::do_stat() const
{
  if (m_initialized) {
    return m_stat;
  }

  m_exists = false;
  m_is_symlink = false;

  int result = 0;

#ifndef _WIN32
  if (m_fd != -1) {
    result = fstat(m_fd, &m_stat);
    if (result == 0) {
      m_exists = true;
    }
  } else
#endif
  {
    util::PathString mpath(m_path);
    result = lstat_func(mpath.c_str(), &m_stat);
    if (result == 0) {
      if (S_ISLNK(m_stat.st_mode)
#ifdef _WIN32
          || (m_stat.st_file_attributes & FILE_ATTRIBUTE_REPARSE_POINT)
#endif
      ) {
        m_is_symlink = true;
        stat_t st;
        if (stat_func(mpath.c_str(), &st) == 0) {
          m_stat = st;
          m_exists = true;
        }
      } else {
        m_exists = true;
      }
    }
  }

  if (result == 0) {
    m_errno = 0;
  } else {
    m_errno = errno;
    if (m_log_on_error == LogOnError::yes) {
#ifdef _WIN32
      LOG("Failed to stat {}: {}", m_path, strerror(m_errno));
#else
      LOG("Failed to {} {}: {}",
          m_fd == -1 ? "stat" : "fstat",
          m_path,
          strerror(m_errno));
#endif
    }
  }

  if (!m_exists) {
    // The file is missing, so just zero fill the stat structure. This will make
    // the is_*() methods return false and mtime() will be 0, etc.
    memset(&m_stat, '\0', sizeof(m_stat));
  }

  m_initialized = true;
  return m_stat;
}

} // namespace util
