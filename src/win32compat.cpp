// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "win32compat.hpp"

#ifdef _WIN32

#  include <psapi.h>
#  include <sys/locking.h>
#  include <tchar.h>

std::string
win32_error_message(DWORD error_code)
{
  LPSTR buffer;
  size_t size =
    FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
                     | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr,
                   error_code,
                   MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                   reinterpret_cast<LPSTR>(&buffer),
                   0,
                   nullptr);
  std::string message(buffer, size);
  LocalFree(buffer);
  return message;
}

#  if !defined(HAVE_REALPATH) && !defined(HAVE_GETFINALPATHNAMEBYHANDLEW)
BOOL
GetFileNameFromHandle(HANDLE file_handle, TCHAR* filename, WORD cch_filename)
{
  BOOL success = FALSE;

  // Get the file size.
  DWORD file_size_hi = 0;
  DWORD file_size_lo = GetFileSize(file_handle, &file_size_hi);
  if (file_size_lo == 0 && file_size_hi == 0) {
    // Cannot map a file with a length of zero.
    return FALSE;
  }

  // Create a file mapping object.
  HANDLE file_map =
    CreateFileMapping(file_handle, NULL, PAGE_READONLY, 0, 1, NULL);
  if (!file_map) {
    return FALSE;
  }

  // Create a file mapping to get the file name.
  void* mem = MapViewOfFile(file_map, FILE_MAP_READ, 0, 0, 1);
  if (mem) {
    if (GetMappedFileName(GetCurrentProcess(), mem, filename, cch_filename)) {
      // Translate path with device name to drive letters.
      TCHAR temp[512];
      temp[0] = '\0';

      if (GetLogicalDriveStrings(512 - 1, temp)) {
        TCHAR name[MAX_PATH];
        TCHAR drive[3] = TEXT(" :");
        BOOL found = FALSE;
        TCHAR* p = temp;

        do {
          // Copy the drive letter to the template string.
          *drive = *p;

          // Look up each device name.
          if (QueryDosDevice(drive, name, MAX_PATH)) {
            size_t name_len = _tcslen(name);
            if (name_len < MAX_PATH) {
              found = _tcsnicmp(filename, name, name_len) == 0
                      && *(filename + name_len) == _T('\\');
              if (found) {
                // Reconstruct filename using temp_file and replace device path
                // with DOS path.
                TCHAR temp_file[MAX_PATH];
                _sntprintf(temp_file,
                           MAX_PATH - 1,
                           TEXT("%s%s"),
                           drive,
                           filename + name_len);
                strcpy(filename, temp_file);
              }
            }
          }

          // Go to the next NULL character.
          while (*p++) {
            // Do nothing.
          }
        } while (!found && *p); // End of string.
      }
    }
    success = TRUE;
    UnmapViewOfFile(mem);
  }

  CloseHandle(file_map);
  return success;
}
#  endif

#endif
