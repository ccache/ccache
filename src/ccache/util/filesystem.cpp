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
#include <thread>

using namespace std::chrono_literals;

namespace util::filesystem {

namespace fs = std::filesystem;

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
  // Windows' rename() won't overwrite an existing file, so need to use
  // MoveFileEx instead.
  //
  // MoveFileEx fails with ERROR_ACCESS_DENIED while another process has the
  // destination open, and with ERROR_SHARING_VIOLATION while another process
  // has the source open without FILE_SHARE_DELETE. Antivirus and endpoint
  // security software often opens files briefly after they have been written,
  // so retry a few times on those errors. (ERROR_ACCESS_DENIED can also be
  // permanent, for instance for a read-only destination.)
  auto delay = 1ms;
  while (!MoveFileExA(old_p.string().c_str(),
                      new_p.string().c_str(),
                      MOVEFILE_REPLACE_EXISTING)) {
    const DWORD error = GetLastError();
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
