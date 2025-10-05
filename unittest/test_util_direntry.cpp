// Copyright (C) 2019-2025 Joel Rosdahl and other contributors
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

#include <ccache/util/defer.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/environment.hpp>
#include <ccache/util/fd.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/wincompat.hpp>

#include <doctest/doctest.h>

#include <fcntl.h>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef _WIN32
#  include <shlobj.h>
#endif

using TestUtil::TestContext;
using util::DirEntry;

namespace fs = util::filesystem;

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

TEST_SUITE_BEGIN("util::DirEntry");

TEST_CASE("Default constructor")
{
  DirEntry entry;
  CHECK(!entry);
  CHECK(!entry.exists());
  CHECK(entry.error_number() == ENOENT);
  CHECK(entry.path() == "");
  CHECK(entry.device() == 0);
  CHECK(entry.inode() == 0);
  CHECK(entry.mode() == 0);
  CHECK(util::sec(entry.ctime()) == 0);
  CHECK(util::nsec_part(entry.ctime()) == 0);
  CHECK(util::sec(entry.mtime()) == 0);
  CHECK(util::nsec_part(entry.mtime()) == 0);
  CHECK(entry.size() == 0);
  CHECK(entry.size_on_disk() == 0);
  CHECK(!entry.is_directory());
  CHECK(!entry.is_regular_file());
  CHECK(!entry.is_symlink());

#ifdef _WIN32
  CHECK(entry.file_attributes() == 0);
  CHECK(entry.reparse_tag() == 0);
#endif
}

TEST_CASE("Construction for missing entry")
{
  DirEntry entry("does_not_exist");
  CHECK(!entry);
  CHECK(!entry.exists());
  CHECK(entry.error_number() == ENOENT);
  CHECK(entry.path() == "does_not_exist");
  CHECK(entry.device() == 0);
  CHECK(entry.inode() == 0);
  CHECK(entry.mode() == 0);
  CHECK(util::sec(entry.ctime()) == 0);
  CHECK(util::nsec_part(entry.ctime()) == 0);
  CHECK(util::sec(entry.mtime()) == 0);
  CHECK(util::nsec_part(entry.mtime()) == 0);
  CHECK(entry.size() == 0);
  CHECK(entry.size_on_disk() == 0);
  CHECK(!entry.is_directory());
  CHECK(!entry.is_regular_file());
  CHECK(!entry.is_symlink());

#ifdef _WIN32
  CHECK(entry.file_attributes() == 0);
  CHECK(entry.reparse_tag() == 0);
#endif
}

#ifndef _WIN32
TEST_CASE("Stat file descriptor")
{
  TestContext test_context;

  REQUIRE(util::write_file("a", "123"));

  util::Fd fd(open("a", O_RDONLY));
  DirEntry entry("a", *fd);
  CHECK(entry);
  CHECK(entry.exists());
  CHECK(!entry.is_symlink());
  CHECK(entry.size() == 3);
  CHECK(entry.path() == "a");
}
#endif

TEST_CASE("Caching and refresh")
{
  TestContext test_context;

  REQUIRE(util::write_file("a", ""));

  DirEntry entry("a");
  CHECK(entry.size() == 0);

  REQUIRE(util::write_file("a", "123", util::WriteFileMode::in_place));
  CHECK(entry.size() == 0);
  entry.refresh();
  CHECK(entry.size() == 3);
}

TEST_CASE("Same i-node as")
{
  TestContext test_context;

  REQUIRE(util::write_file("a", ""));
  REQUIRE(util::write_file("b", ""));
  DirEntry entry_a("a");
  DirEntry entry_b("b");

  CHECK(entry_a.same_inode_as(entry_a));
  CHECK(!entry_a.same_inode_as(entry_b));

  REQUIRE(util::write_file("a", "change size", util::WriteFileMode::in_place));
  CHECK(DirEntry("a").same_inode_as(entry_a));

  CHECK(!DirEntry("nonexistent").same_inode_as(DirEntry("nonexistent")));
}

TEST_CASE("Get path")
{
  TestContext test_context;

  REQUIRE(util::write_file("a", ""));
  CHECK(DirEntry("a").path() == "a");
  CHECK(DirEntry("does_not_exist").path() == "does_not_exist");
}

TEST_CASE("Return values when file exists")
{
  TestContext test_context;

  REQUIRE(util::write_file("file", "1234567"));

  DirEntry de("file");
  CHECK(de);
  CHECK(de.exists());
  CHECK(de.error_number() == 0);
  CHECK(de.path() == "file");
  CHECK(!de.is_directory());
  CHECK(de.is_regular_file());
  CHECK(!de.is_symlink());
  CHECK(de.size() == 7);

#ifdef _WIN32
  BY_HANDLE_FILE_INFORMATION info = {};
  CHECK(win32_get_file_info("file", &info));

  CHECK(de.device() == info.dwVolumeSerialNumber);
  CHECK((de.inode() >> 32) == info.nFileIndexHigh);
  CHECK((de.inode() & ((1ULL << 32) - 1)) == info.nFileIndexLow);
  CHECK(S_ISREG(de.mode()));
  CHECK((de.mode() & ~S_IFMT) == 0666);

  struct timespec creation_time =
    win32_filetime_to_timespec(info.ftCreationTime);
  struct timespec last_write_time =
    win32_filetime_to_timespec(info.ftLastWriteTime);

  CHECK(util::sec(de.ctime()) == creation_time.tv_sec);
  CHECK(util::nsec_part(de.ctime()) == creation_time.tv_nsec);
  CHECK(util::sec(de.mtime()) == last_write_time.tv_sec);
  CHECK(util::nsec_part(de.mtime()) == last_write_time.tv_nsec);

  CHECK(de.size_on_disk() == ((de.size() + 4095) & ~4095));
  CHECK(de.file_attributes() == info.dwFileAttributes);
  CHECK(de.reparse_tag() == 0);

#else
  struct stat st;
  CHECK(::stat("file", &st) == 0);

  CHECK(de.device() == st.st_dev);
  CHECK(de.inode() == st.st_ino);
  CHECK(de.mode() == st.st_mode);
  CHECK(de.size_on_disk() == util::likely_size_on_disk(st.st_size));

#  ifdef HAVE_STRUCT_STAT_ST_CTIM
  CHECK(util::sec(de.ctime()) == st.st_ctim.tv_sec);
  CHECK(util::nsec_part(de.ctime()) == st.st_ctim.tv_nsec);
#  elif defined(HAVE_STRUCT_STAT_ST_CTIMESPEC)
  CHECK(util::sec(de.ctime()) == st.st_ctimespec.tv_sec);
  CHECK(util::nsec_part(de.ctime()) == st.st_ctimespec.tv_nsec);
#  else
  CHECK(util::sec(de.ctime()) == st.st_ctime);
  CHECK(util::nsec_part(de.ctime()) == 0);
#  endif

#  ifdef HAVE_STRUCT_STAT_ST_MTIM
  CHECK(util::sec(de.mtime()) == st.st_mtim.tv_sec);
  CHECK(util::nsec_part(de.mtime()) == st.st_mtim.tv_nsec);
#  elif defined(HAVE_STRUCT_STAT_ST_MTIMESPEC)
  CHECK(util::sec(de.mtime()) == st.st_mtimespec.tv_sec);
  CHECK(util::nsec_part(de.mtime()) == st.st_mtimespec.tv_nsec);
#  else
  CHECK(util::sec(de.mtime()) == st.st_mtime);
  CHECK(util::nsec_part(de.mtime()) == 0);
#  endif
#endif
}

TEST_CASE("Directory")
{
  TestContext test_context;

  REQUIRE(fs::create_directory("directory"));
  DirEntry entry("directory");

  CHECK(entry);
  CHECK(entry.exists());
  CHECK(entry.error_number() == 0);
  CHECK(entry.is_directory());
  CHECK(!entry.is_regular_file());
  CHECK(!entry.is_symlink());
  CHECK(S_ISDIR(entry.mode()));
#ifdef _WIN32
  CHECK((entry.mode() & ~S_IFMT) == 0777);
  CHECK((entry.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
  CHECK(!(entry.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
  CHECK(entry.reparse_tag() == 0);
#endif
}

TEST_CASE("Symlink to file" * doctest::skip(!symlinks_supported()))
{
  TestContext test_context;

  REQUIRE(util::write_file("file", "1234567"));

#ifdef _WIN32
  // SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE: 0x2
  REQUIRE(CreateSymbolicLinkA("symlink_to_file", "file", 0x2));
#else
  REQUIRE(symlink("file", "symlink_to_file") == 0);
#endif

  DirEntry entry("symlink_to_file");
  CHECK(entry);
  CHECK(entry.exists());
  CHECK(entry.error_number() == 0);
  CHECK(entry.path() == "symlink_to_file");
  CHECK(!entry.is_directory());
  CHECK(entry.is_regular_file());
  CHECK(entry.is_symlink());
  CHECK(S_ISREG(entry.mode()));
  CHECK(entry.size() == 7);
#ifdef _WIN32
  CHECK(!(entry.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
  CHECK(!(entry.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
  CHECK(entry.reparse_tag() == 0);
#endif
}

#ifndef _WIN32

TEST_CASE("Symlink to directory")
{
  TestContext test_context;

  REQUIRE(symlink(".", "symlink_to_dir") == 0);

  DirEntry entry("symlink_to_dir");
  CHECK(entry);
  CHECK(entry.path() == "symlink_to_dir");
  CHECK(entry.exists());
  CHECK(entry.error_number() == 0);
  CHECK(entry.is_directory());
  CHECK(!entry.is_regular_file());
  CHECK(entry.is_symlink());
  CHECK(S_ISDIR(entry.mode()));
}

TEST_CASE("Symlink to none")
{
  TestContext test_context;

  REQUIRE(symlink("does_not_exist", "symlink_to_none") == 0);

  DirEntry entry("symlink_to_none");
  CHECK(entry);
  CHECK(!entry.exists());
  CHECK(entry.error_number() == 0);
  CHECK(entry.path() == "symlink_to_none");
  CHECK(!entry.is_directory());
  CHECK(!entry.is_regular_file());
  CHECK(entry.is_symlink());
  CHECK(entry.mode() == 0);
  CHECK(entry.size() == 0);
}

#endif // _WIN32

TEST_CASE("Hard links")
{
  TestContext test_context;

  REQUIRE(util::write_file("a", ""));

#ifdef _WIN32
  REQUIRE(CreateHardLinkA("b", "a", nullptr));
#else
  REQUIRE(link("a", "b") == 0);
#endif

  DirEntry entry_a("a");
  CHECK(entry_a);
  CHECK(entry_a.exists());
  CHECK(entry_a.error_number() == 0);
  CHECK(!entry_a.is_directory());
  CHECK(entry_a.is_regular_file());
  CHECK(!entry_a.is_symlink());
  CHECK(entry_a.size() == 0);

  DirEntry entry_b("b");
  CHECK(entry_b.exists());
  CHECK(entry_b);
  CHECK(entry_b.error_number() == 0);
  CHECK(!entry_b.is_directory());
  CHECK(entry_b.is_regular_file());
  CHECK(!entry_b.is_symlink());
  CHECK(entry_b.size() == 0);

  CHECK(entry_a.device() == entry_b.device());
  CHECK(entry_a.inode() == entry_b.inode());
  CHECK(entry_a.same_inode_as(entry_b));

  REQUIRE(util::write_file("a", "1234567", util::WriteFileMode::in_place));
  entry_b.refresh();
  CHECK(entry_b.size() == 7);
}

TEST_CASE("Special" * doctest::skip(running_under_wine()))
{
  SUBCASE("tty")
  {
#ifdef _WIN32
    DirEntry entry("\\\\.\\CON");
#else
    DirEntry entry("/dev/tty");
#endif
    CHECK(entry);
    CHECK(entry.exists());
    CHECK(entry.error_number() == 0);
    CHECK(!entry.is_directory());
    CHECK(!entry.is_regular_file());
    CHECK(!entry.is_symlink());
    CHECK(S_ISCHR(entry.mode()));
#ifdef _WIN32
    CHECK(entry.file_attributes() == 0);
    CHECK(entry.reparse_tag() == 0);
#endif
  }

  SUBCASE("null")
  {
#ifdef _WIN32
    DirEntry entry("\\\\.\\NUL");
#else
    DirEntry entry("/dev/null");
#endif
    CHECK(entry);
    CHECK(entry.exists());
    CHECK(entry.error_number() == 0);
    CHECK(!entry.is_directory());
    CHECK(!entry.is_regular_file());
    CHECK(!entry.is_symlink());
    CHECK(S_ISCHR(entry.mode()));
#ifdef _WIN32
    CHECK(entry.file_attributes() == 0);
    CHECK(entry.reparse_tag() == 0);
#endif
  }

  SUBCASE("pipe")
  {
#ifdef _WIN32
    const char pipe_path[] = "\\\\.\\pipe\\InitShutdown"; // Well-known pipe
#else
    const char pipe_path[] = "my_pipe";
    REQUIRE(mkfifo(pipe_path, 0600) == 0);
#endif

    DirEntry entry(pipe_path);
    CHECK(entry);
    CHECK(entry.exists());
    CHECK(entry.error_number() == 0);
    CHECK(!entry.is_directory());
    CHECK(!entry.is_regular_file());
    CHECK(!entry.is_symlink());
    CHECK(S_ISFIFO(entry.mode()));
#ifdef _WIN32
    CHECK(entry.file_attributes() == 0);
    CHECK(entry.reparse_tag() == 0);
#endif
  }

  SUBCASE("block device")
  {
#ifdef _WIN32
    DirEntry entry("\\\\.\\C:");
    CHECK(entry);
    CHECK(entry.exists());
    CHECK(entry.error_number() == 0);
    CHECK(!entry.is_directory());
    CHECK(!entry.is_regular_file());
    CHECK(!entry.is_symlink());
    CHECK(S_ISBLK(entry.mode()));
    CHECK(entry.file_attributes() == 0);
    CHECK(entry.reparse_tag() == 0);
#endif
  }
}

#ifdef _WIN32
TEST_CASE("Win32 Readonly File")
{
  TestContext test_context;

  REQUIRE(util::write_file("file", ""));

  DWORD prev_attrs = GetFileAttributesA("file");
  REQUIRE(prev_attrs != INVALID_FILE_ATTRIBUTES);
  REQUIRE(SetFileAttributesA("file", prev_attrs | FILE_ATTRIBUTE_READONLY));

  DirEntry entry("file");
  entry.refresh();
  REQUIRE(SetFileAttributesA("file", prev_attrs));

  CHECK(entry);
  CHECK(entry.exists());
  CHECK(entry.error_number() == 0);
  CHECK(S_ISREG(entry.mode()));
  CHECK((entry.mode() & ~S_IFMT) == 0444);
  CHECK((entry.file_attributes() & FILE_ATTRIBUTE_READONLY));
  CHECK(!(entry.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
  CHECK(!(entry.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
  CHECK(entry.reparse_tag() == 0);
}

TEST_CASE("Win32 Executable File")
{
  TestContext test_context;

  const char* comspec = getenv("COMSPEC");
  REQUIRE(comspec != nullptr);

  DirEntry entry(comspec);
  CHECK(entry);
  CHECK(entry.exists());
  CHECK(entry.error_number() == 0);
  CHECK(!entry.is_directory());
  CHECK(entry.is_regular_file());
  CHECK(!entry.is_symlink());
  CHECK(S_ISREG(entry.mode()));
  CHECK((entry.mode() & ~S_IFMT) == 0777);
  CHECK(!(entry.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
  CHECK(!(entry.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
  CHECK(entry.reparse_tag() == 0);
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
  DEFER(CloseHandle(handle));

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

  DirEntry entry("file");
  CHECK(!entry);
  CHECK(!entry.exists());
  CHECK(entry.error_number() == ENOENT);
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
  DEFER(CloseHandle(handle));

  // Sanity check we can't open the file for read/write access.
  REQUIRE(!util::read_file<std::string>("file"));

  DirEntry entry("file");
  CHECK(entry);
  CHECK(entry.exists());
  CHECK(entry.error_number() == 0);
  CHECK(!entry.is_directory());
  CHECK(entry.is_regular_file());
  CHECK(!entry.is_symlink());
  CHECK(S_ISREG(entry.mode()));
  CHECK(entry.size() == 0);
  CHECK(!(entry.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
  CHECK(!(entry.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
  CHECK(entry.reparse_tag() == 0);
}

// Creating a directory junction for test purposes is tricky on Windows.
// Instead, test a well-known junction that has existed in all Windows versions
// since Vista. (Not present on Wine.)
TEST_CASE(
  "Win32 Directory Junction"
  * doctest::skip(!win32_is_junction(
    util::expand_environment_variables("${ALLUSERSPROFILE}\\Application Data")
      .value_or(""))))
{
  TestContext test_context;

  DirEntry entry(*util::expand_environment_variables(
    "${ALLUSERSPROFILE}\\Application Data"));
  CHECK(entry);
  CHECK(entry.exists());
  CHECK(entry.error_number() == 0);
  CHECK(entry.is_directory());
  CHECK(!entry.is_regular_file());
  CHECK(entry.is_symlink());
  CHECK(S_ISDIR(entry.mode()));
  CHECK((entry.mode() & ~S_IFMT) == 0777);
  CHECK((entry.file_attributes() & FILE_ATTRIBUTE_DIRECTORY));
  CHECK(!(entry.file_attributes() & FILE_ATTRIBUTE_REPARSE_POINT));
  CHECK(entry.reparse_tag() == 0);
}
#endif // _WIN32

TEST_SUITE_END();
