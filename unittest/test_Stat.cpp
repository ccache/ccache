// Copyright (C) 2019-2021 Joel Rosdahl and other contributors
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

#include "../src/Finalizer.hpp"
#include "../src/Stat.hpp"
#include "../src/Util.hpp"
#include "TestUtil.hpp"

#include <core/exceptions.hpp>
#include <core/wincompat.hpp>

#include "third_party/doctest.h"

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef _WIN32
#  include <shlobj.h>
#endif

using TestUtil::TestContext;

namespace {

bool
running_under_wine()
{
#ifdef _WIN32
  static bool is_wine =
    GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")
    != nullptr;
  return is_wine;
#else
  return false;
#endif
}

bool
symlinks_supported()
{
#ifdef _WIN32
  // Windows only supports symlinks if the user has the required privilege (e.g.
  // they're an admin) or if developer mode is enabled.

  // See: https://stackoverflow.com/a/41232108/192102
  const char* dev_mode_key =
    "SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\AppModelUnlock";
  const char* dev_mode_value = "AllowDevelopmentWithoutDevLicense";

  DWORD dev_mode_enabled = 0;
  DWORD buf_size = sizeof(dev_mode_enabled);

  return !running_under_wine()
         && (IsUserAnAdmin()
             || (RegGetValueA(HKEY_LOCAL_MACHINE,
                              dev_mode_key,
                              dev_mode_value,
                              RRF_RT_DWORD,
                              nullptr,
                              &dev_mode_enabled,
                              &buf_size)
                   == ERROR_SUCCESS
                 && dev_mode_enabled));
#else
  return true;
#endif
}

#ifdef _WIN32
bool
win32_is_junction(const std::string& path)
{
  HANDLE handle =
    CreateFileA(path.c_str(),
                FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  FILE_ATTRIBUTE_TAG_INFO reparse_info = {};
  bool is_junction =
    (GetFileType(handle) == FILE_TYPE_DISK)
    && GetFileInformationByHandleEx(
      handle, FileAttributeTagInfo, &reparse_info, sizeof(reparse_info))
    && (reparse_info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
    && (reparse_info.ReparseTag == IO_REPARSE_TAG_MOUNT_POINT);
  CloseHandle(handle);
  return is_junction;
}

bool
win32_get_file_info(const std::string& path, BY_HANDLE_FILE_INFORMATION* info)
{
  HANDLE handle =
    CreateFileA(path.c_str(),
                FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                nullptr);
  if (handle == INVALID_HANDLE_VALUE) {
    return false;
  }
  BOOL ret = GetFileInformationByHandle(handle, info);
  CloseHandle(handle);
  return ret;
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
#endif

} // namespace

TEST_SUITE_BEGIN("Stat");

TEST_CASE("Default constructor")
{
  Stat stat;
  CHECK(!stat);
  CHECK(stat.error_number() == -1);
  CHECK(stat.device() == 0);
  CHECK(stat.inode() == 0);
  CHECK(stat.mode() == 0);
  CHECK(stat.ctime() == 0);
  CHECK(stat.mtime() == 0);
  CHECK(stat.size() == 0);
  CHECK(stat.size_on_disk() == 0);
  CHECK(!stat.is_directory());
  CHECK(!stat.is_regular());
  CHECK(!stat.is_symlink());

  CHECK(stat.ctim().tv_sec == 0);
  CHECK(stat.ctim().tv_nsec == 0);

  CHECK(stat.mtim().tv_sec == 0);
  CHECK(stat.mtim().tv_nsec == 0);

#ifdef _WIN32
  CHECK(stat.file_attributes() == 0);
  CHECK(stat.reparse_tag() == 0);
#endif
}

TEST_CASE("Named constructors")
{
  CHECK(!Stat::stat("does_not_exist"));
  CHECK(!Stat::stat("does_not_exist", Stat::OnError::ignore));
  CHECK(!Stat::stat("does_not_exist", Stat::OnError::log));
  CHECK_THROWS_WITH(Stat::stat("does_not_exist", Stat::OnError::throw_error),
                    "failed to stat does_not_exist: No such file or directory");
}

TEST_CASE("Same i-node as")
{
  TestContext test_context;

  Util::write_file("a", "");
  Util::write_file("b", "");
  auto a_stat = Stat::stat("a");
  auto b_stat = Stat::stat("b");

  CHECK(a_stat.same_inode_as(a_stat));
  CHECK(!a_stat.same_inode_as(b_stat));

  Util::write_file("a", "change size");
  auto new_a_stat = Stat::stat("a");
  CHECK(new_a_stat.same_inode_as(a_stat));
}

TEST_CASE("Return values when file is missing")
{
  auto stat = Stat::stat("does_not_exist");
  CHECK(!stat);
  CHECK(stat.error_number() == ENOENT);
  CHECK(stat.device() == 0);
  CHECK(stat.inode() == 0);
  CHECK(stat.mode() == 0);
  CHECK(stat.ctime() == 0);
  CHECK(stat.mtime() == 0);
  CHECK(stat.size() == 0);
  CHECK(stat.size_on_disk() == 0);
  CHECK(!stat.is_directory());
  CHECK(!stat.is_regular());
  CHECK(!stat.is_symlink());

  CHECK(stat.ctim().tv_sec == 0);
  CHECK(stat.ctim().tv_nsec == 0);

  CHECK(stat.mtim().tv_sec == 0);
  CHECK(stat.mtim().tv_nsec == 0);

#ifdef _WIN32
  CHECK(stat.file_attributes() == 0);
  CHECK(stat.reparse_tag() == 0);
#endif
}

TEST_CASE("Return values when file exists")
{
  TestContext test_context;

  Util::write_file("file", "1234567");

  auto stat = Stat::stat("file");
  CHECK(stat);
  CHECK(stat.error_number() == 0);
  CHECK(!stat.is_directory());
  CHECK(stat.is_regular());
  CHECK(!stat.is_symlink());
  CHECK(stat.size() == 7);

#ifdef _WIN32
  BY_HANDLE_FILE_INFORMATION info = {};
  CHECK(win32_get_file_info("file", &info));

  CHECK(stat.device() == info.dwVolumeSerialNumber);
  CHECK((stat.inode() >> 32) == info.nFileIndexHigh);
  CHECK((stat.inode() & ((1ULL << 32) - 1)) == info.nFileIndexLow);
  CHECK(S_ISREG(stat.mode()));
  CHECK((stat.mode() & ~S_IFMT) == 0666);

  struct timespec creation_time =
    win32_filetime_to_timespec(info.ftCreationTime);
  struct timespec last_write_time =
    win32_filetime_to_timespec(info.ftLastWriteTime);

  CHECK(stat.ctime() == creation_time.tv_sec);
  CHECK(stat.mtime() == last_write_time.tv_sec);

  CHECK(stat.ctim().tv_sec == creation_time.tv_sec);
  CHECK(stat.ctim().tv_nsec == creation_time.tv_nsec);
  CHECK(stat.mtim().tv_sec == last_write_time.tv_sec);
  CHECK(stat.mtim().tv_nsec == last_write_time.tv_nsec);

  CHECK(stat.size_on_disk() == ((stat.size() + 1023) & ~1023));
  CHECK(stat.file_attributes() == info.dwFileAttributes);
  CHECK(stat.reparse_tag() == 0);

#else
  struct stat st;
  CHECK(::stat("file", &st) == 0);

  CHECK(stat.device() == st.st_dev);
  CHECK(stat.inode() == st.st_ino);
  CHECK(stat.mode() == st.st_mode);
  CHECK(stat.ctime() == st.st_ctime);
  CHECK(stat.mtime() == st.st_mtime);
  CHECK(stat.size_on_disk() == st.st_blocks * 512);

#  ifdef HAVE_STRUCT_STAT_ST_CTIM
  CHECK(stat.ctim().tv_sec == st.st_ctim.tv_sec);
  CHECK(stat.ctim().tv_nsec == st.st_ctim.tv_nsec);
#  else
  CHECK(stat.ctim().tv_sec == st.st_ctime);
  CHECK(stat.ctim().tv_nsec == 0);
#  endif

#  ifdef HAVE_STRUCT_STAT_ST_MTIM
  CHECK(stat.mtim().tv_sec == st.st_mtim.tv_sec);
  CHECK(stat.mtim().tv_nsec == st.st_mtim.tv_nsec);
#  else
  CHECK(stat.mtim().tv_sec == st.st_mtime);
  CHECK(stat.mtim().tv_nsec == 0);
#  endif
#endif
}

TEST_CASE("Directory")
{
  TestContext test_context;

  REQUIRE(mkdir("directory", 0456) == 0);
  auto stat = Stat::stat("directory");

  CHECK(stat);
  CHECK(stat.error_number() == 0);
  CHECK(stat.is_directory());
  CHECK(!stat.is_regular());
  CHECK(!stat.is_symlink());
  CHECK(S_ISDIR(stat.mode()));
#ifdef _WIN32
  CHECK((stat.mode() & ~S_IFMT) == 0777);
  CHECK((stat.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
  CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
  CHECK(stat.reparse_tag() == 0);
#endif
}

TEST_CASE("Symlinks" * doctest::skip(!symlinks_supported()))
{
  TestContext test_context;

  Util::write_file("file", "1234567");

#ifdef _WIN32
  REQUIRE(CreateSymbolicLinkA(
    "symlink", "file", 0x2 /*SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE*/));
#else
  REQUIRE(symlink("file", "symlink") == 0);
#endif

  SUBCASE("file lstat")
  {
    auto stat = Stat::lstat("file", Stat::OnError::ignore);
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(!stat.is_directory());
    CHECK(stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(S_ISREG(stat.mode()));
    CHECK(stat.size() == 7);
#ifdef _WIN32
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
    CHECK(stat.reparse_tag() == 0);
#endif
  }

  SUBCASE("file stat")
  {
    auto stat = Stat::stat("file", Stat::OnError::ignore);
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(!stat.is_directory());
    CHECK(stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(S_ISREG(stat.mode()));
    CHECK(stat.size() == 7);
#ifdef _WIN32
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
    CHECK(stat.reparse_tag() == 0);
#endif
  }

  SUBCASE("symlink lstat")
  {
    auto stat = Stat::lstat("symlink", Stat::OnError::ignore);
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(!stat.is_directory());
    CHECK(!stat.is_regular());
    CHECK(stat.is_symlink());
    CHECK(S_ISLNK(stat.mode()));
#ifdef _WIN32
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
    CHECK((stat.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
    CHECK(stat.reparse_tag() == IO_REPARSE_TAG_SYMLINK);
#else
    CHECK(stat.size() == 4);
#endif
  }

  SUBCASE("symlink stat")
  {
    auto stat = Stat::stat("symlink", Stat::OnError::ignore);
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(!stat.is_directory());
    CHECK(stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(S_ISREG(stat.mode()));
    CHECK(stat.size() == 7);
#ifdef _WIN32
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
    CHECK(stat.reparse_tag() == 0);
#endif
  }
}

TEST_CASE("Hard links")
{
  TestContext test_context;

  Util::write_file("a", "");

#ifdef _WIN32
  REQUIRE(CreateHardLinkA("b", "a", nullptr));
#else
  REQUIRE(link("a", "b") == 0);
#endif

  auto stat_a = Stat::stat("a");
  CHECK(stat_a);
  CHECK(stat_a.error_number() == 0);
  CHECK(!stat_a.is_directory());
  CHECK(stat_a.is_regular());
  CHECK(!stat_a.is_symlink());
  CHECK(stat_a.size() == 0);

  auto stat_b = Stat::stat("b");
  CHECK(stat_b);
  CHECK(stat_b.error_number() == 0);
  CHECK(!stat_b.is_directory());
  CHECK(stat_b.is_regular());
  CHECK(!stat_b.is_symlink());
  CHECK(stat_b.size() == 0);

  CHECK(stat_a.device() == stat_b.device());
  CHECK(stat_a.inode() == stat_b.inode());
  CHECK(stat_a.same_inode_as(stat_b));

  Util::write_file("a", "1234567");
  stat_a = Stat::stat("a");
  stat_b = Stat::stat("b");

  CHECK(stat_a.size() == 7);
  CHECK(stat_b.size() == 7);
}

TEST_CASE("Special" * doctest::skip(running_under_wine()))
{
  SUBCASE("tty")
  {
#ifdef _WIN32
    auto stat = Stat::stat("\\\\.\\CON");
#else
    auto stat = Stat::stat("/dev/tty");
#endif
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(!stat.is_directory());
    CHECK(!stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(S_ISCHR(stat.mode()));
#ifdef _WIN32
    CHECK(stat.file_attributes() == 0);
    CHECK(stat.reparse_tag() == 0);
#endif
  }

  SUBCASE("null")
  {
#ifdef _WIN32
    auto stat = Stat::stat("\\\\.\\NUL");
#else
    auto stat = Stat::stat("/dev/null");
#endif
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(!stat.is_directory());
    CHECK(!stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(S_ISCHR(stat.mode()));
#ifdef _WIN32
    CHECK(stat.file_attributes() == 0);
    CHECK(stat.reparse_tag() == 0);
#endif
  }

  SUBCASE("pipe")
  {
#ifdef _WIN32
    const char* pipe_path = "\\\\.\\pipe\\InitShutdown"; // Well-known pipe name
#else
    const char* pipe_path = "my_pipe";
    REQUIRE(mkfifo(pipe_path, 0600) == 0);
#endif

    auto stat = Stat::stat(pipe_path);
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(!stat.is_directory());
    CHECK(!stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(S_ISFIFO(stat.mode()));
#ifdef _WIN32
    CHECK(stat.file_attributes() == 0);
    CHECK(stat.reparse_tag() == 0);
#endif
  }

#ifdef _WIN32
  SUBCASE("block device")
  {
    auto stat = Stat::stat("\\\\.\\C:");
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(!stat.is_directory());
    CHECK(!stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(S_ISBLK(stat.mode()));
    CHECK(stat.file_attributes() == 0);
    CHECK(stat.reparse_tag() == 0);
  }
#endif
}

#ifdef _WIN32
TEST_CASE("Win32 Readonly File")
{
  TestContext test_context;

  Util::write_file("file", "");

  DWORD prev_attrs = GetFileAttributesA("file");
  REQUIRE(prev_attrs != INVALID_FILE_ATTRIBUTES);
  REQUIRE(SetFileAttributesA("file", prev_attrs | FILE_ATTRIBUTE_READONLY));

  auto stat = Stat::stat("file");
  REQUIRE(SetFileAttributesA("file", prev_attrs));

  CHECK(stat);
  CHECK(stat.error_number() == 0);
  CHECK(S_ISREG(stat.mode()));
  CHECK((stat.mode() & ~S_IFMT) == 0444);
  CHECK((stat.file_attributes() & FILE_ATTRIBUTE_READONLY));
  CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
  CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
  CHECK(stat.reparse_tag() == 0);
}

TEST_CASE("Win32 Executable File")
{
  TestContext test_context;

  const char* comspec = getenv("COMSPEC");
  REQUIRE(comspec != nullptr);

  auto stat = Stat::stat(comspec);
  CHECK(stat);
  CHECK(stat.error_number() == 0);
  CHECK(!stat.is_directory());
  CHECK(stat.is_regular());
  CHECK(!stat.is_symlink());
  CHECK(S_ISREG(stat.mode()));
  CHECK((stat.mode() & ~S_IFMT) == 0777);
  CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
  CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
  CHECK(stat.reparse_tag() == 0);
}

TEST_CASE("Win32 Pending Delete" * doctest::skip(running_under_wine()))
{
  TestContext test_context;

  HANDLE handle =
    CreateFileA("file",
                GENERIC_READ | GENERIC_WRITE | DELETE,
                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr,
                CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL,
                nullptr);
  REQUIRE_MESSAGE(handle != INVALID_HANDLE_VALUE, "err=" << GetLastError());
  Finalizer cleanup([&] { CloseHandle(handle); });

  // Mark file as deleted. This puts it into a "pending delete" state that
  // will persist until the handle is closed. Until the file is closed, new
  // handles cannot be created to the file; attempts to do so fail with
  // ERROR_ACCESS_DENIED/STATUS_DELETE_PENDING. Our stat implementation maps
  // these to ENOENT.
  FILE_DISPOSITION_INFO info{};
  info.DeleteFile = TRUE;
  REQUIRE_MESSAGE(SetFileInformationByHandle(
                    handle, FileDispositionInfo, &info, sizeof(info)),
                  "err=" << GetLastError());

  SUBCASE("stat file pending delete")
  {
    auto st = Stat::stat("file");
    CHECK(!st);
    CHECK(st.error_number() == ENOENT);
  }

  SUBCASE("lstat file pending delete")
  {
    auto st = Stat::lstat("file");
    CHECK(!st);
    CHECK(st.error_number() == ENOENT);
  }
}

// Our Win32 Stat implementation should open files using FILE_READ_ATTRIBUTES,
// which bypasses sharing restrictions.
TEST_CASE("Win32 No Sharing")
{
  TestContext test_context;

  HANDLE handle = CreateFileA("file",
                              GENERIC_READ | GENERIC_WRITE,
                              0 /* no sharing */,
                              nullptr,
                              CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL,
                              nullptr);
  REQUIRE_MESSAGE(handle != INVALID_HANDLE_VALUE, "err=" << GetLastError());
  Finalizer cleanup([&] { CloseHandle(handle); });

  // Sanity check we can't open the file for read/write access.
  REQUIRE_THROWS_AS(Util::read_file("file"), const core::Error&);

  SUBCASE("stat file no sharing")
  {
    auto stat = Stat::stat("file");
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(!stat.is_directory());
    CHECK(stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(S_ISREG(stat.mode()));
    CHECK(stat.size() == 0);
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
    CHECK(stat.reparse_tag() == 0);
  }

  SUBCASE("lstat file no sharing")
  {
    auto stat = Stat::lstat("file");
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(!stat.is_directory());
    CHECK(stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(S_ISREG(stat.mode()));
    CHECK(stat.size() == 0);
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
    CHECK(stat.reparse_tag() == 0);
  }
}

// Creating a directory junction for test purposes is tricky on Windows.
// Instead, test a well-known junction that has existed in all Windows versions
// since Vista. (Not present on Wine.)
TEST_CASE("Win32 Directory Junction"
          * doctest::skip(!win32_is_junction(Util::expand_environment_variables(
            "${ALLUSERSPROFILE}\\Application Data"))))
{
  TestContext test_context;

  SUBCASE("junction stat")
  {
    auto stat = Stat::stat(Util::expand_environment_variables(
      "${ALLUSERSPROFILE}\\Application Data"));
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(stat.is_directory());
    CHECK(!stat.is_regular());
    CHECK(!stat.is_symlink());
    CHECK(S_ISDIR(stat.mode()));
    CHECK((stat.mode() & ~S_IFMT) == 0777);
    CHECK((stat.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
    CHECK(!(stat.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
    CHECK(stat.reparse_tag() == 0);
  }

  SUBCASE("junction lstat")
  {
    auto stat = Stat::lstat(Util::expand_environment_variables(
      "${ALLUSERSPROFILE}\\Application Data"));
    CHECK(stat);
    CHECK(stat.error_number() == 0);
    CHECK(!stat.is_directory());
    CHECK(!stat.is_regular());
    CHECK(!stat.is_symlink()); // Should only be true for bona fide symlinks
    CHECK((stat.mode() & S_IFMT) == 0); // Not a symlink/file/directory
    CHECK((stat.mode() & ~S_IFMT) == 0777);
    CHECK((stat.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
    CHECK((stat.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
    CHECK(stat.reparse_tag() == IO_REPARSE_TAG_MOUNT_POINT);
  }
}
#endif

TEST_SUITE_END();
