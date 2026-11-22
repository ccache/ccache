// Copyright (C) 2023-2024 Joel Rosdahl and other contributors
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

#include "filesystem.hpp"

#include <ccache/util/error.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/wincompat.hpp>

#ifdef _WIN32
#  include <win32/winerror_to_errno.h>
#endif

#include <chrono>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace util::filesystem {

namespace fs = std::filesystem;

#ifdef _WIN32
namespace {

// FileRenameInfoEx, its flags and the Flags member of FILE_RENAME_INFO aren't
// declared by all SDK and MinGW-w64 versions for the _WIN32_WINNT ccache uses.
const auto k_file_rename_info_ex = static_cast<FILE_INFO_BY_HANDLE_CLASS>(22);
#  ifndef FILE_RENAME_FLAG_REPLACE_IF_EXISTS
#    define FILE_RENAME_FLAG_REPLACE_IF_EXISTS 0x1
#  endif
#  ifndef FILE_RENAME_FLAG_POSIX_SEMANTICS
#    define FILE_RENAME_FLAG_POSIX_SEMANTICS 0x2
#  endif

struct RenameInfo
{
  DWORD Flags;
  HANDLE RootDirectory;
  DWORD FileNameLength;
  WCHAR FileName[1];
};

// Rename old_p to new_p, replacing new_p if it exists. Returns ERROR_SUCCESS or
// a Windows error code.
//
// Windows' rename() won't overwrite an existing file. MoveFileEx does, but it
// fails with ERROR_ACCESS_DENIED while another process has the destination
// open. A rename with POSIX semantics only fails (with ERROR_SHARING_VIOLATION)
// if that process doesn't allow FILE_SHARE_DELETE, so try that first. Both fail
// with ERROR_SHARING_VIOLATION while another process has the source open
// without FILE_SHARE_DELETE.
DWORD
win32_rename(const fs::path& old_p, const fs::path& new_p)
{
  const std::wstring& new_name = new_p.native();
  std::vector<uint8_t> buffer(sizeof(RenameInfo)
                              + new_name.size() * sizeof(WCHAR));
  auto* info = reinterpret_cast<RenameInfo*>(buffer.data());
  info->Flags =
    FILE_RENAME_FLAG_REPLACE_IF_EXISTS | FILE_RENAME_FLAG_POSIX_SEMANTICS;
  info->FileNameLength = static_cast<DWORD>(new_name.size() * sizeof(WCHAR));
  std::memcpy(
    info->FileName, new_name.c_str(), info->FileNameLength + sizeof(WCHAR));

  // With FILE_FLAG_OPEN_REPARSE_POINT, a symlink itself is renamed, like with
  // MoveFileEx, and not its target.
  HANDLE handle =
    CreateFileW(old_p.c_str(),
                DELETE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return GetLastError();
  }
  DWORD error =
    SetFileInformationByHandle(
      handle, k_file_rename_info_ex, info, static_cast<DWORD>(buffer.size()))
      ? ERROR_SUCCESS
      : GetLastError();
  CloseHandle(handle);

  if (error == ERROR_INVALID_PARAMETER || error == ERROR_NOT_SUPPORTED
      || error == ERROR_INVALID_FUNCTION) {
    // Not supported by this Windows version or file system.
    error = MoveFileExW(old_p.c_str(), new_p.c_str(), MOVEFILE_REPLACE_EXISTING)
              ? ERROR_SUCCESS
              : GetLastError();
  }
  return error;
}

} // namespace
#endif

tl::expected<void, std::error_code>
rename(const fs::path& old_p, const fs::path& new_p)
{
#ifndef _WIN32
  std::error_code ec;
  fs::rename(old_p, new_p, ec);
  if (ec) {
    return tl::unexpected(ec);
  }
#else
  // Antivirus software often opens a file briefly after it has been written, so
  // retry a few times while the file is in use (see win32_rename).
  // ERROR_ACCESS_DENIED can also be permanent, hence the limit.
  auto delay = 1ms;
  DWORD error;
  while ((error = win32_rename(old_p, new_p)) != ERROR_SUCCESS) {
    if ((error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION)
        || delay > 64ms) {
      return tl::unexpected(
        std::error_code(winerror_to_errno(error), std::generic_category()));
    }
    LOG("Failed to rename {} to {}: {} ({}), retrying in {} ms",
        old_p,
        new_p,
        util::win32_error_message(error),
        error,
        delay.count());
    std::this_thread::sleep_for(delay);
    delay *= 2;
  }
#endif
  return {};
}

} // namespace util::filesystem
