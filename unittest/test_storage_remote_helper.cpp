// Copyright (C) 2026 Joel Rosdahl and other contributors
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

#include "testutil.hpp"

#include <ccache/storage/remote/helper.hpp>
#include <ccache/util/defer.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/environment.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>

#include <doctest/doctest.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdlib>
#include <optional>
#include <string>

namespace fs = util::filesystem;

using TestUtil::TestContext;

TEST_SUITE_BEGIN("storage::remote");

TEST_CASE("get_helper_ipc_dir fallback")
{
  TestContext test_context;
  const char* xdg_runtime_dir = std::getenv("XDG_RUNTIME_DIR");
  const std::optional<std::string> saved_xdg_runtime_dir =
    xdg_runtime_dir ? std::make_optional<std::string>(xdg_runtime_dir)
                    : std::nullopt;
  util::unsetenv("XDG_RUNTIME_DIR");
  DEFER(
    if (saved_xdg_runtime_dir) {
      util::setenv("XDG_RUNTIME_DIR", *saved_xdg_runtime_dir);
    } else { util::unsetenv("XDG_RUNTIME_DIR"); });

  const fs::path name = FMT("ccache-tmp-{}", static_cast<uint64_t>(getuid()));

#if defined(O_DIRECTORY) && defined(O_NOFOLLOW)
  REQUIRE(fs::create_directory("new"));
  CHECK(storage::remote::get_helper_ipc_dir("new") == fs::path("new") / name);
  CHECK((util::DirEntry(fs::path("new") / name).mode() & 07777) == 0700);

  REQUIRE(fs::create_directory("existing"));
  REQUIRE(fs::create_directory(fs::path("existing") / name));
  REQUIRE(chmod((fs::path("existing") / name).c_str(), 0755) == 0);
  CHECK(storage::remote::get_helper_ipc_dir("existing")
        == fs::path("existing") / name);
  CHECK((util::DirEntry(fs::path("existing") / name).mode() & 07777) == 0700);

  REQUIRE(fs::create_directory("target"));
  REQUIRE(chmod("target", 0755) == 0);
  REQUIRE(fs::create_directory("link"));
  REQUIRE(fs::create_symlink("../target", fs::path("link") / name));
  CHECK_FALSE(storage::remote::get_helper_ipc_dir("link"));
  CHECK((util::DirEntry("target").mode() & 07777) == 0755);

  REQUIRE(fs::create_directory("file"));
  REQUIRE(util::write_file(fs::path("file") / name, "data"));
  CHECK_FALSE(storage::remote::get_helper_ipc_dir("file"));
#else
  CHECK_FALSE(storage::remote::get_helper_ipc_dir("."));
#endif
}

TEST_SUITE_END();
