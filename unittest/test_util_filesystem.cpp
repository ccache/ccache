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

#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/wincompat.hpp>

#include <doctest/doctest.h>

#include <chrono>
#include <future>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace fs = util::filesystem;

using TestUtil::TestContext;

TEST_CASE("util::filesystem::rename of a symlink")
{
  TestContext test_context;

  REQUIRE(util::write_file("target", "content"));
  if (!fs::create_symlink("target", "link") || !fs::exists("link")) {
    // Creating symlinks on Windows requires a privilege or developer mode, and
    // Wine 10 and older only pretends to.
    return;
  }

  REQUIRE(fs::rename("link", "moved"));
  CHECK(fs::read_symlink("moved").value_or("") == "target");
  CHECK(util::read_file<std::string>("target").value_or("") == "content");
}

#ifdef _WIN32
namespace {

bool
running_under_wine()
{
  return GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")
         != nullptr;
}

} // namespace

TEST_CASE("util::filesystem::rename onto a directory")
{
  TestContext test_context;

  REQUIRE(util::write_file("src", "new"));
  REQUIRE(fs::create_directory("dest"));

  const auto result = fs::rename("src", "dest");
  REQUIRE(!result);
  CHECK(result.error() == std::errc::permission_denied);
}

TEST_CASE("util::filesystem::rename while the source or destination is open")
{
  TestContext test_context;

  REQUIRE(util::write_file("src", "new"));
  REQUIRE(util::write_file("dest", "old"));

  // Hold a file open for a short while, like a virus scanner would.
  const auto hold_open = [](const char* path) {
    HANDLE handle = CreateFileA(path,
                                GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr,
                                OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL,
                                nullptr);
    REQUIRE_MESSAGE(handle != INVALID_HANDLE_VALUE, "error=" << GetLastError());
    // Wait until the thread runs. Under heavy load, it could otherwise start
    // after all retries are done.
    std::promise<void> running;
    auto running_future = running.get_future();
    std::thread thread([handle, running = std::move(running)]() mutable {
      running.set_value();
      std::this_thread::sleep_for(20ms);
      CloseHandle(handle);
    });
    running_future.wait();
    return thread;
  };

  std::thread closer;
  SUBCASE("destination open")
  {
    // Fails with ERROR_SHARING_VIOLATION (MoveFileEx: ERROR_ACCESS_DENIED).
    closer = hold_open("dest");
  }
  SUBCASE("source open")
  {
    // Fails with ERROR_SHARING_VIOLATION.
    closer = hold_open("src");
  }

  const auto result = fs::rename("src", "dest");
  closer.join();

  CHECK(result);
  CHECK(util::read_file<std::string>("dest").value_or("") == "new");
}

// Wine doesn't support renaming with POSIX semantics.
TEST_CASE("util::filesystem::rename while the destination stays open"
          * doctest::skip(running_under_wine()))
{
  TestContext test_context;

  REQUIRE(util::write_file("src", "new"));
  REQUIRE(util::write_file("dest", "old"));

  // Microsoft Defender for Endpoint allows delete sharing.
  HANDLE handle =
    CreateFileA("dest",
                GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
  REQUIRE_MESSAGE(handle != INVALID_HANDLE_VALUE, "error=" << GetLastError());
  const auto result = fs::rename("src", "dest");
  CloseHandle(handle);

  CHECK(result);
  CHECK(util::read_file<std::string>("dest").value_or("") == "new");
}
#endif
