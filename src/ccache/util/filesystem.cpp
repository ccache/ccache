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

#include <ccache/util/wincompat.hpp>

#ifdef _WIN32
#  include <win32/winerror_to_errno.h>
#endif

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
  if (!MoveFileExA(old_p.string().c_str(),
                   new_p.string().c_str(),
                   MOVEFILE_REPLACE_EXISTING)) {
    return tl::unexpected(std::error_code(winerror_to_errno(GetLastError()),
                                          std::system_category()));
  }
#endif
  return {};
}

} // namespace util::filesystem
