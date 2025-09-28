// Copyright (C) 2020-2024 Joel Rosdahl and other contributors
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

#include "temporaryfile.hpp"

#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/process.hpp>

#include <cstdlib>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef _WIN32
#  include <win32/mktemp.h>
#endif

namespace fs = util::filesystem;

namespace util {

TemporaryFile::TemporaryFile(Fd&& fd_, const fs::path& path_)
  : fd(std::move(fd_)),
    path(path_)
{
}

tl::expected<TemporaryFile, std::string>
TemporaryFile::create(const fs::path& path_prefix, std::string_view suffix)
{
  if (path_prefix.has_parent_path()) {
    if (auto ret = fs::create_directories(path_prefix.parent_path()); !ret) {
      return tl::unexpected(ret.error().message());
    }
  }
  std::string path_template =
    FMT("{}{}XXXXXX{}", path_prefix, TemporaryFile::tmp_file_infix, suffix);
#ifdef _WIN32
  // MSVC lacks mkstemps() and Mingw-w64's implementation[1] is problematic, as
  // it can reuse the names of recently-deleted files unless the caller
  // remembers to call srand().

  // [1]: <https://github.com/Alexpux/mingw-w64/blob/
  // d0d7f784833bbb0b2d279310ddc6afb52fe47a46/mingw-w64-crt/misc/mkstemp.c>
  Fd fd(bsd_mkstemps(path_template.data(), static_cast<int>(suffix.length())));
#else
  Fd fd(mkstemps(path_template.data(), static_cast<int>(suffix.length())));
#endif
  if (!fd) {
    return tl::unexpected(FMT("failed to create temporary file for {}: {}",
                              path_template,
                              strerror(errno)));
  }

  util::set_cloexec_flag(*fd);
#ifndef _WIN32
  fchmod(*fd, 0666 & ~util::get_umask());
#endif

  return TemporaryFile(std::move(fd), path_template);
}

bool
TemporaryFile::is_tmp_file(const fs::path& path)
{
  return util::pstr(path.filename()).str().find(tmp_file_infix)
         != std::string::npos;
}

} // namespace util
