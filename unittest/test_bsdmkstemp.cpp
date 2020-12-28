#include "../src/Fd.hpp"
#include "TestUtil.hpp"

#include "third_party/doctest.h"
#include "third_party/win32/mktemp.h"

#include <algorithm>
#include <memory>
#include <ostream>
#include <sddl.h>
#include <utility>

using TestUtil::TestContext;

namespace {

class ScopedHANDLE
{
public:
  ScopedHANDLE() = default;

  explicit ScopedHANDLE(HANDLE h) : h_(h)
  {
  }

  ScopedHANDLE(ScopedHANDLE&& other) : ScopedHANDLE(other.release())
  {
  }

  ~ScopedHANDLE()
  {
    if (h_ != INVALID_HANDLE_VALUE) {
      CloseHandle(h_);
    }
  }

  ScopedHANDLE&
  operator=(ScopedHANDLE rhs)
  {
    std::swap(h_, rhs.h_);
    return *this;
  }

  explicit operator bool() const
  {
    return h_ != INVALID_HANDLE_VALUE;
  }

  HANDLE
  get() const
  {
    return h_;
  }

  HANDLE
  release()
  {
    HANDLE h = h_;
    h_ = INVALID_HANDLE_VALUE;
    return h;
  }

private:
  HANDLE h_ = INVALID_HANDLE_VALUE;
};

} // namespace

TEST_SUITE_BEGIN("thirdparty");

TEST_CASE("thirdparty::bsd_mkstemp")
{
  TestContext test_context;

  static int rand_iter;
  rand_iter = 0;

  bsd_mkstemp_set_random_source([](void* buf, size_t nbytes) {
    std::fill_n(
      static_cast<uint16_t*>(buf), nbytes / sizeof(uint16_t), rand_iter);
    rand_iter++;
  });

  struct Cleanup
  {
    ~Cleanup()
    {
      bsd_mkstemp_set_random_source(nullptr);
    }
  } cleanup;

  SUBCASE("successful")
  {
    std::string path("XXXXXX");
    CHECK_MESSAGE(Fd(bsd_mkstemp(&path[0])), "errno=" << errno);
    CHECK(path == "AAAAAA");
  }

  SUBCASE("existing_file")
  {
    CHECK_MESSAGE(ScopedHANDLE(CreateFileA("AAAAAA",
                                           GENERIC_READ | GENERIC_WRITE,
                                           0,
                                           nullptr,
                                           CREATE_NEW,
                                           FILE_ATTRIBUTE_NORMAL,
                                           nullptr)),
                  "errno=" << errno);

    std::string path("XXXXXX");
    CHECK_MESSAGE(Fd(bsd_mkstemp(&path[0])), "errno=" << errno);
    CHECK(path == "BBBBBB");
  }

  SUBCASE("existing_file_pending_delete")
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

    std::string path("XXXXXX");
    CHECK_MESSAGE(Fd(bsd_mkstemp(&path[0])), "errno=" << errno);
    CHECK(path == "BBBBBB");
  }

  SUBCASE("existing_dir")
  {
    CHECK_MESSAGE(CreateDirectoryA("AAAAAA", nullptr), "errno=" << errno);

    std::string path("XXXXXX");
    CHECK_MESSAGE(Fd(bsd_mkstemp(&path[0])), "errno=" << errno);
    CHECK(path == "BBBBBB");
  }

  SUBCASE("permission denied")
  {
    auto makeACL = [](const char* aclString) {
      PSECURITY_DESCRIPTOR desc = nullptr;
      ConvertStringSecurityDescriptorToSecurityDescriptorA(
        aclString, SDDL_REVISION_1, &desc, nullptr);
      return std::shared_ptr<SECURITY_DESCRIPTOR>(
        static_cast<SECURITY_DESCRIPTOR*>(desc), &LocalFree);
    };

    // Create a directory with a contrived ACL that denies creation of new
    // files and directories to the "Everybody" (WD) group.
    std::shared_ptr<SECURITY_DESCRIPTOR> desc;
    CHECK_MESSAGE((desc = makeACL("D:(D;;DCLCRPCR;;;WD)(A;;FA;;;WD)")),
                  "errno=" << errno);

    SECURITY_ATTRIBUTES attrs{};
    attrs.nLength = sizeof(attrs);
    attrs.lpSecurityDescriptor = desc.get();
    CHECK_MESSAGE(CreateDirectoryA("my_readonly_dir", &attrs),
                  "errno=" << errno);

    // Sanity check that we cannot write to this directory. (E.g. Wine
    // doesn't appear to emulate Windows ACLs properly when run under root.)
    bool broken_acls = static_cast<bool>(ScopedHANDLE(
      CreateFileA("my_readonly_dir/.writable",
                  GENERIC_WRITE,
                  0,
                  nullptr,
                  CREATE_ALWAYS,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_DELETE_ON_CLOSE,
                  nullptr)));

    if (!broken_acls) {
      std::string path("my_readonly_dir/XXXXXX");
      CHECK(!Fd(bsd_mkstemp(&path[0])));
      CHECK(errno == EACCES);
    } else {
      MESSAGE("ACLs do not appear to function properly on this filesystem");
    }
  }
}

TEST_SUITE_END();
