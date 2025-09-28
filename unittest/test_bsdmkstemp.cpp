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

#include "testutil.hpp"

#include <ccache/util/defer.hpp>
#include <ccache/util/fd.hpp>
#include <ccache/util/wincompat.hpp>

#include <doctest/doctest.h>

#include <sddl.h>
#include <win32/mktemp.h>

#include <algorithm>
#include <memory>
#include <ostream>
#include <utility>

using TestUtil::TestContext;
using util::Fd;

namespace {

class ScopedHANDLE
{
public:
  ScopedHANDLE() = default;

  explicit ScopedHANDLE(HANDLE handle)
    : m_handle(handle)
  {
  }

  ScopedHANDLE(ScopedHANDLE&& other)
    : ScopedHANDLE(other.release())
  {
  }

  ~ScopedHANDLE()
  {
    if (m_handle != INVALID_HANDLE_VALUE) {
      CloseHandle(m_handle);
    }
  }

  ScopedHANDLE&
  operator=(ScopedHANDLE rhs)
  {
    std::swap(m_handle, rhs.m_handle);
    return *this;
  }

  explicit
  operator bool() const
  {
    return m_handle != INVALID_HANDLE_VALUE;
  }

  HANDLE
  get() const
  {
    return m_handle;
  }

  HANDLE
  release()
  {
    HANDLE handle = m_handle;
    m_handle = INVALID_HANDLE_VALUE;
    return handle;
  }

private:
  HANDLE m_handle = INVALID_HANDLE_VALUE;
};

} // namespace

TEST_SUITE_BEGIN("bsd_mkstemps");

TEST_CASE("bsd_mkstemps")
{
  TestContext test_context;

  static uint16_t rand_iter;
  rand_iter = 0;

  bsd_mkstemp_set_random_source([](void* buf, size_t nbytes) {
    std::fill_n(
      static_cast<uint16_t*>(buf), nbytes / sizeof(uint16_t), rand_iter);
    ++rand_iter;
  });

  DEFER(bsd_mkstemp_set_random_source(nullptr));

  SUBCASE("successful")
  {
    std::string path = "XXXXXX";
    Fd fd(bsd_mkstemps(&path[0], 0));
    CHECK_MESSAGE(fd, "errno=" << errno);
    CHECK(path == "AAAAAA");
  }

  SUBCASE("successful with suffix")
  {
    std::string path = "XXXXXX123";
    Fd fd(bsd_mkstemps(&path[0], 3));
    CHECK_MESSAGE(fd, "errno=" << errno);
    CHECK(path == "AAAAAA123");
  }

  SUBCASE("existing file")
  {
    ScopedHANDLE handle(CreateFileA("AAAAAA",
                                    GENERIC_READ | GENERIC_WRITE,
                                    0,
                                    nullptr,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr));
    CHECK_MESSAGE(handle, "errno=" << errno);

    std::string path = "XXXXXX";
    Fd fd(bsd_mkstemps(&path[0], 0));
    CHECK_MESSAGE(fd, "errno=" << errno);
    CHECK(path == "BBBBBB");
  }

  SUBCASE("existing file, pending delete")
  {
    ScopedHANDLE h;
    CHECK_MESSAGE(
      (h = ScopedHANDLE(CreateFileA("AAAAAA",
                                    GENERIC_READ | GENERIC_WRITE | DELETE,
                                    0,
                                    nullptr,
                                    CREATE_NEW,
                                    FILE_ATTRIBUTE_NORMAL,
                                    nullptr))),
      "errno=" << errno);

    // Mark file as deleted. This puts it into a "pending delete" state that
    // will persist until the handle is closed.
    FILE_DISPOSITION_INFO info{};
    info.DeleteFile = TRUE;
    CHECK_MESSAGE(SetFileInformationByHandle(
                    h.get(), FileDispositionInfo, &info, sizeof(info)),
                  "errno=" << errno);

    std::string path = "XXXXXX";
    Fd fd(bsd_mkstemps(&path[0], 0));
    CHECK_MESSAGE(fd, "errno=" << errno);
    CHECK(path == "BBBBBB");
  }

  SUBCASE("existing directory")
  {
    CHECK_MESSAGE(CreateDirectoryA("AAAAAA", nullptr), "errno=" << errno);

    std::string path = "XXXXXX";
    Fd fd(bsd_mkstemps(&path[0], 0));
    CHECK_MESSAGE(fd, "errno=" << errno);
    CHECK(path == "BBBBBB");
  }

  SUBCASE("permission denied")
  {
    auto make_ACL = [](const char* acl_string) {
      PSECURITY_DESCRIPTOR desc = nullptr;
      ConvertStringSecurityDescriptorToSecurityDescriptorA(
        acl_string, SDDL_REVISION_1, &desc, nullptr);
      return std::shared_ptr<SECURITY_DESCRIPTOR>(
        static_cast<SECURITY_DESCRIPTOR*>(desc), &LocalFree);
    };

    // Create a directory with a contrived ACL that denies creation of new files
    // and directories to the "Everybody" (WD) group.
    std::shared_ptr<SECURITY_DESCRIPTOR> desc;
    CHECK_MESSAGE((desc = make_ACL("D:(D;;DCLCRPCR;;;WD)(A;;FA;;;WD)")),
                  "errno=" << errno);

    SECURITY_ATTRIBUTES attrs{};
    attrs.nLength = sizeof(attrs);
    attrs.lpSecurityDescriptor = desc.get();
    CHECK_MESSAGE(CreateDirectoryA("my_readonly_dir", &attrs),
                  "errno=" << errno);

    // Sanity check that we cannot write to this directory. (E.g. Wine doesn't
    // appear to emulate Windows ACLs properly when run under root.)
    bool broken_acls = static_cast<bool>(ScopedHANDLE(
      CreateFileA("my_readonly_dir/.writable",
                  GENERIC_WRITE,
                  0,
                  nullptr,
                  CREATE_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
                  nullptr)));

    if (!broken_acls) {
      std::string path = "my_readonly_dir/XXXXXX";
      CHECK(!Fd(bsd_mkstemps(&path[0], 0)));
      CHECK(errno == EACCES);
    } else {
      MESSAGE("ACLs do not appear to function properly on this filesystem");
    }
  }
}

TEST_SUITE_END();
