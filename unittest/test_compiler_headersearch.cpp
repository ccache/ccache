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

#include <string>

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

TEST_SUITE_END();
