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

#include <ccache/compiler/headersearch.hpp>

#include <doctest/doctest.h>

#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace fs = std::filesystem;

using compiler::Dirs;

TEST_SUITE_BEGIN("headersearch");

TEST_CASE("compiler::parse_header_search_output")
{
  SUBCASE("empty")
  {
    const auto output = compiler::parse_header_search_output("");
    REQUIRE(output.paths);
    CHECK(output.paths->quote_dirs.empty());
    CHECK(output.paths->angle_dirs.empty());
    CHECK(output.paths->nonexistent_dirs.empty());
    CHECK(output.remaining_stderr == "");
  }

  SUBCASE("no report")
  {
    const std::string stderr_data =
      "test.c:3:10: warning: extra tokens at end of #endif directive\n"
      " indented line outside a search list\n";
    const auto output = compiler::parse_header_search_output(stderr_data);
    REQUIRE(output.paths);
    CHECK(output.paths->quote_dirs.empty());
    CHECK(output.paths->angle_dirs.empty());
    CHECK(output.paths->nonexistent_dirs.empty());
    CHECK(output.remaining_stderr == stderr_data);
  }

  SUBCASE("GCC")
  {
    const std::string stderr_data =
      "cc1: warning: command-line option '-std=c++17' is valid for C++/ObjC++"
      " but not for C\n"
      "ignoring duplicate directory "
      "\"/usr/lib/gcc/x86_64-linux-gnu/13/include\"\n"
      "ignoring nonexistent directory \"/usr/local/include/x86_64-linux-gnu\"\n"
      "ignoring nonexistent directory \"inc1\"\n"
      "ignoring nonexistent directory \"\"\n"
      "ignoring duplicate directory \"inc2\"\n"
      "#include \"...\" search starts here:\n"
      " q\n"
      "#include <...> search starts here:\n"
      " inc2\n"
      " /usr/lib/gcc/x86_64-linux-gnu/13/include\n"
      " /usr/include\n"
      "End of search list.\n"
      "#embed <...> search starts here:\n"
      " /usr/share/embed\n"
      "End of #embed search list.\n"
      "test.c:2:10: fatal error: q.h: No such file or directory\n"
      "    2 | #include \"q.h\"\n"
      "      |          ^~~~~\n"
      "compilation terminated.\n";
    const auto output = compiler::parse_header_search_output(stderr_data);
    REQUIRE(output.paths);
    CHECK(output.paths->quote_dirs == Dirs{"q"});
    CHECK(output.paths->angle_dirs
          == Dirs{"inc2",
                  "/usr/lib/gcc/x86_64-linux-gnu/13/include",
                  "/usr/include"});
    CHECK(output.paths->nonexistent_dirs
          == Dirs{"/usr/local/include/x86_64-linux-gnu", "inc1"});
    CHECK(output.remaining_stderr
          == "cc1: warning: command-line option '-std=c++17' is valid for"
             " C++/ObjC++ but not for C\n"
             "test.c:2:10: fatal error: q.h: No such file or directory\n"
             "    2 | #include \"q.h\"\n"
             "      |          ^~~~~\n"
             "compilation terminated.\n");
  }

  SUBCASE("Clang")
  {
    const std::string stderr_data =
      "clang: warning: -lfoo: 'linker' input unused"
      " [-Wunused-command-line-argument]\n"
      "clang -cc1 version 21.1.8 based upon LLVM 21.1.8 default target"
      " x86_64-unknown-linux-gnu\n"
      "ignoring nonexistent directory \"inc1\"\n"
      "ignoring duplicate directory \"inc2\"\n"
      "  as it is a non-system directory that duplicates a system directory\n"
      "#include \"...\" search starts here:\n"
      "#include <...> search starts here:\n"
      " inc2\n"
      " headers.hmap (headermap)\n"
      " /usr/lib/clang/21/include\n"
      " /Library/Frameworks (framework directory)\n"
      "End of search list.\n"
      "test.c:1:2: warning: foo [-W#warnings]\n";
    const auto output = compiler::parse_header_search_output(stderr_data);
    REQUIRE(output.paths);
    CHECK(output.paths->quote_dirs.empty());
    CHECK(output.paths->angle_dirs
          == Dirs{"inc2", "/usr/lib/clang/21/include", "/Library/Frameworks"});
    CHECK(output.paths->nonexistent_dirs == Dirs{"inc1"});
    CHECK(output.remaining_stderr
          == "clang: warning: -lfoo: 'linker' input unused"
             " [-Wunused-command-line-argument]\n"
             "clang -cc1 version 21.1.8 based upon LLVM 21.1.8 default target"
             " x86_64-unknown-linux-gnu\n"
             "test.c:1:2: warning: foo [-W#warnings]\n");
  }

  SUBCASE("CRLF")
  {
    const std::string stderr_data =
      "ignoring nonexistent directory \"inc1\"\r\n"
      "#include \"...\" search starts here:\r\n"
      " q\r\n"
      "#include <...> search starts here:\r\n"
      " inc2\r\n"
      "End of search list.\r\n"
      "warning: something\r\n";
    const auto output = compiler::parse_header_search_output(stderr_data);
    REQUIRE(output.paths);
    CHECK(output.paths->quote_dirs == Dirs{"q"});
    CHECK(output.paths->angle_dirs == Dirs{"inc2"});
    CHECK(output.paths->nonexistent_dirs == Dirs{"inc1"});
    CHECK(output.remaining_stderr == "warning: something\r\n");
  }

  SUBCASE("Multiple reports (e.g. CUDA host and device compilation)")
  {
    const std::string stderr_data =
      "clang -cc1 version 21.1.8 based upon LLVM 21.1.8 default target"
      " x86_64-unknown-linux-gnu\n"
      "#include \"...\" search starts here:\n"
      "#include <...> search starts here:\n"
      " /usr/include\n"
      "End of search list.\n"
      "clang -cc1 version 21.1.8 based upon LLVM 21.1.8 default target"
      " x86_64-unknown-linux-gnu\n"
      "#include \"...\" search starts here:\n"
      "#include <...> search starts here:\n"
      " /usr/local/cuda/include\n"
      " /usr/include\n"
      "End of search list.\n";
    const auto output = compiler::parse_header_search_output(stderr_data);
    REQUIRE(output.paths);
    CHECK(output.paths->angle_dirs
          == Dirs{"/usr/include", "/usr/local/cuda/include", "/usr/include"});
    CHECK(output.remaining_stderr
          == "clang -cc1 version 21.1.8 based upon LLVM 21.1.8 default target"
             " x86_64-unknown-linux-gnu\n"
             "clang -cc1 version 21.1.8 based upon LLVM 21.1.8 default target"
             " x86_64-unknown-linux-gnu\n");
  }

  SUBCASE("Missing end marker")
  {
    const std::string stderr_data =
      "#include <...> search starts here:\n"
      " /usr/include\n"
      "test.c:2:10: error: foo\n"
      "    2 | int x = foo;\n";
    const auto output = compiler::parse_header_search_output(stderr_data);
    REQUIRE(output.paths);
    CHECK(output.paths->angle_dirs == Dirs{"/usr/include"});
    CHECK(output.remaining_stderr
          == "test.c:2:10: error: foo\n"
             "    2 | int x = foo;\n");
  }

  SUBCASE("Nonexistent directories without search list")
  {
    const auto output = compiler::parse_header_search_output(
      "ignoring nonexistent directory \"inc1\"\nwarning: foo\n");
    REQUIRE(output.paths);
    CHECK(output.paths->nonexistent_dirs == Dirs{"inc1"});
    CHECK(output.paths->quote_dirs.empty());
    CHECK(output.paths->angle_dirs.empty());
    CHECK(output.remaining_stderr == "warning: foo\n");
  }

  SUBCASE("Duplicate directory reason without duplicate line")
  {
    const std::string stderr_data =
      "  as it is a non-system directory that duplicates a system directory\n"
      "ignoring duplicate directory \"inc2\"\n"
      "  as it is a non-system directory that duplicates a system directory\n";
    const auto output = compiler::parse_header_search_output(stderr_data);
    CHECK(output.remaining_stderr
          == "  as it is a non-system directory that duplicates a system"
             " directory\n");
  }

  SUBCASE("No trailing newline")
  {
    CHECK(
      compiler::parse_header_search_output("End of search list.\nno newline")
        .remaining_stderr
      == "no newline");
    CHECK(compiler::parse_header_search_output("End of search list.")
            .remaining_stderr
          == "");
  }
}

TEST_CASE("compiler::find_shadow_paths")
{
#ifdef _WIN32
  const fs::path cwd = "C:/cwd";
  const fs::path abs = "C:/abs";
#else
  const fs::path cwd = "/cwd";
  const fs::path abs = "/abs";
#endif

  std::set<fs::path> existing;
  std::set<fs::path> existing_files;
  auto stat = [&](const fs::path& path) {
    return existing_files.contains(path) ? compiler::PathKind::file
           : existing.contains(path)     ? compiler::PathKind::directory
                                         : compiler::PathKind::missing;
  };
  std::map<fs::path, fs::path> links;
  auto canonical = [&](const fs::path& path) {
    const auto it = links.find(path);
    return it == links.end() ? path : it->second;
  };
  auto find = [&](const compiler::HeaderSearchPaths& paths, const Dirs& files) {
    std::vector<compiler::IncludedFile> included_files;
    for (const auto& file : files) {
      included_files.push_back({file, {}});
    }
    return compiler::find_shadow_paths(
             paths, cwd, included_files, {}, stat, canonical)
      .paths;
  };

  compiler::HeaderSearchPaths paths;

  SUBCASE("include file in later directory")
  {
    paths.angle_dirs = {"inc1", "inc2"};
    existing = {cwd / "inc1", cwd / "inc2", cwd / "inc2/hello.h"};
    CHECK(find(paths, {"inc2/hello.h"}) == Dirs{"inc1/hello.h"});
  }

  SUBCASE("include file in first directory")
  {
    paths.angle_dirs = {"inc1", "inc2"};
    existing = {cwd / "inc1", cwd / "inc2", cwd / "inc1/hello.h"};
    CHECK(find(paths, {"inc1/hello.h"}).empty());
  }

  SUBCASE("include file outside search directories")
  {
    paths.angle_dirs = {"inc1", "inc2"};
    existing = {cwd / "inc1", cwd / "inc2", cwd / "hello.h"};
    CHECK(find(paths, {"hello.h"}).empty());
  }

  SUBCASE("quote directories are searched first")
  {
    paths.quote_dirs = {"q"};
    paths.angle_dirs = {"inc1", "inc2"};
    existing = {cwd / "q", cwd / "inc1", cwd / "inc2", cwd / "inc2/hello.h"};
    CHECK(find(paths, {"inc2/hello.h"}) == Dirs{"inc1/hello.h", "q/hello.h"});
  }

  SUBCASE("absolute directories")
  {
    paths.angle_dirs = {abs / "inc1", abs / "inc2"};
    existing = {abs / "inc1", abs / "inc2", abs / "inc2/hello.h"};
    CHECK(find(paths, {abs / "inc2/hello.h"}) == Dirs{abs / "inc1/hello.h"});
  }

  SUBCASE("absolute include file in relative directory")
  {
    paths.angle_dirs = {"inc1", "inc2"};
    existing = {cwd / "inc1", cwd / "inc2", cwd / "inc2/hello.h"};
    CHECK(find(paths, {cwd / "inc2/hello.h"}) == Dirs{"inc1/hello.h"});
  }

  SUBCASE("existing file is not a shadow path")
  {
    paths.angle_dirs = {"inc1", "inc2"};
    existing = {
      cwd / "inc1", cwd / "inc2", cwd / "inc1/hello.h", cwd / "inc2/hello.h"};
    CHECK(find(paths, {"inc2/hello.h"}).empty());
  }

  SUBCASE("first missing parent directory")
  {
    paths.angle_dirs = {"inc1", "sys"};
    existing = {cwd / "inc1",
                cwd / "sys",
                cwd / "sys/bits",
                cwd / "sys/bits/a.h",
                cwd / "sys/bits/b.h"};
    CHECK(find(paths, {"sys/bits/a.h", "sys/bits/b.h"}) == Dirs{"inc1/bits"});

    existing.insert(cwd / "inc1/bits");
    CHECK(find(paths, {"sys/bits/a.h", "sys/bits/b.h"})
          == Dirs{"inc1/bits/a.h", "inc1/bits/b.h"});
  }

  SUBCASE("nested directories")
  {
    paths.angle_dirs = {"a", "a/b"};
    existing = {cwd / "a", cwd / "a/b", cwd / "a/b/c.h"};
    CHECK(find(paths, {"a/b/c.h"}) == Dirs{"a/c.h"});
  }

  SUBCASE("duplicate directories")
  {
    paths.angle_dirs = {"inc1", "inc2", "inc1", "inc2"};
    existing = {cwd / "inc1", cwd / "inc2", cwd / "inc2/hello.h"};
    CHECK(find(paths, {"inc2/hello.h"}) == Dirs{"inc1/hello.h"});
  }

  SUBCASE("unnormalized directories")
  {
    paths.angle_dirs = {"./inc1/", abs / "lib/../include"};
    existing = {cwd / "inc1", abs / "include", abs / "include/stdio.h"};
    CHECK(find(paths, {abs / "include/stdio.h"}) == Dirs{"inc1/stdio.h"});
  }

  SUBCASE("parent-relative include")
  {
    paths.angle_dirs = {"x/a", "b"};
    existing = {cwd / "x", cwd / "x/a", cwd / "b", cwd / "foo.h"};
    CHECK(find(paths, {"b/../foo.h"}) == Dirs{"x/foo.h"});
  }

  SUBCASE("precompiled header")
  {
    paths.angle_dirs = {"inc1", "inc2"};
    existing = {cwd / "inc1", cwd / "inc2", cwd / "inc2/foo.h.gch"};
    CHECK(find(paths, {"inc2/foo.h.gch"})
          == Dirs{"inc1/foo.h", "inc1/foo.h.gch"});
  }

  SUBCASE("directory of including file")
  {
    paths.angle_dirs = {"other", "include"};
    existing = {cwd / "src",
                cwd / "other",
                cwd / "include",
                cwd / "include/foo.h",
                cwd / "other/bar.h"};
    const std::vector<compiler::IncludedFile> included_files = {
      {"include/foo.h", {"src", "."}},
      {"other/bar.h",   {"include"} },
    };
    CHECK(compiler::find_shadow_paths(
            paths, cwd, included_files, {}, stat, canonical)
            .paths
          == Dirs{"foo.h", "include/bar.h", "other/foo.h", "src/foo.h"});
  }

  SUBCASE("__has_include probes")
  {
    paths.quote_dirs = {"q"};
    paths.angle_dirs = {"inc1", "inc2"};
    existing = {cwd / "src", cwd / "q", cwd / "inc1", cwd / "inc2"};
    existing_files = {cwd / "inc2/found.h"};
    const std::vector<compiler::HasIncludeProbe> probes = {
      {"src/main.c", "missing.h",   true },
      {"src/main.c", "found.h",     false},
      {"inc1/x.h",   "sub/other.h", false},
    };
    const auto result =
      compiler::find_shadow_paths(paths, cwd, {}, probes, stat, canonical);
    CHECK(result.paths
          == Dirs{"inc1/found.h",
                  "inc1/missing.h",
                  "inc1/sub",
                  "inc2/missing.h",
                  "inc2/sub",
                  "q/missing.h",
                  "src/missing.h"});
    CHECK(result.probed_files == Dirs{"inc2/found.h"});
  }

  SUBCASE("nonexistent directories")
  {
    paths.nonexistent_dirs = {"missing", abs / "gone", "file_not_dir"};
    existing = {cwd / "file_not_dir"};
    const auto result = find(paths, {});
    CHECK(std::set<fs::path>(result.begin(), result.end())
          == std::set<fs::path>{abs / "gone", "missing"});
  }

  SUBCASE("include file printed with symlinks resolved")
  {
    paths.angle_dirs = {"inc1", abs / "link/include"};
    links = {
      {abs / "link/include", abs / "real/include"}
    };
    existing = {cwd / "inc1", abs / "link/include", abs / "real/include/foo.h"};
    CHECK(find(paths, {abs / "real/include/foo.h"}) == Dirs{"inc1/foo.h"});
  }

  SUBCASE("same directory via different symlinks")
  {
    paths.angle_dirs = {abs / "link/include", abs / "real/include"};
    links = {
      {abs / "link/include", abs / "real/include"}
    };
    existing = {
      abs / "link/include", abs / "real/include", abs / "real/include/foo.h"};
    CHECK(find(paths, {abs / "real/include/foo.h"}).empty());
  }
}

TEST_CASE("compiler::find_has_include_operands")
{
  using Operands = std::vector<compiler::HasIncludeOperand>;

  CHECK(compiler::find_has_include_operands("").literals.empty());
  CHECK(!compiler::find_has_include_operands("").macro_operand);

  auto operands = compiler::find_has_include_operands(
    "#if __has_include(<foo/bar.h>)\n#if __has_include ( \"baz.h\" )\n"
    "#if __has_include_next(<stdint.h>)\n#if __has_include \\\n(<a.h>)\n");
  CHECK(operands.literals
        == Operands{
          {"foo/bar.h", false},
          {"baz.h",     true },
          {"stdint.h",  false},
          {"a.h",       false}
  });
  CHECK(!operands.macro_operand);

  operands = compiler::find_has_include_operands(
    "#ifdef __has_include\n#if defined(__has_include)\n"
    "#if x__has_include(<a.h>)\n#if __has_includes(<a.h>)\n"
    "#if __has_include(<a.h\n>)\n");
  CHECK(operands.literals.empty());
  CHECK(!operands.macro_operand);

  operands = compiler::find_has_include_operands(
    "#if __has_include(<a.h>) && __has_include(HEADER)\n");
  CHECK(operands.literals
        == Operands{
          {"a.h", false}
  });
  CHECK(operands.macro_operand);
  CHECK(compiler::find_has_include_operands("#if __has_include_next(HEADER)")
          .macro_operand);
}

TEST_SUITE_END();
