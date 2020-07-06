// Copyright (C) 2010-2020 Joel Rosdahl and other contributors
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

#include "../src/ccache.hpp"
#include "../src/hash.hpp"
#include "../src/Context.hpp"
#include "TestUtil.hpp"

#include "third_party/catch.hpp"

#include <vector>
#include <string>

using TestUtil::TestContext;

TEST_CASE("ignore_options")
{
  // This test case verifies that only valid ignore_options items are processed
  // and that the hash produced by ccache is not affected by any item in
  // ignore_options.
  TestContext test_context;

  Context exp_ctx;
  Context act_ctx;

  struct hash* exp_hash = hash_init();
  struct hash* act_hash = hash_init();

  // Without the actual file present, this test will cause an exception to be
  // thrown for reasons not having to do with the functionality under test.
  exp_ctx.args_info.input_file = "foo.c";
  act_ctx.args_info.input_file = "foo.c";
  Util::write_file("foo.c", "int main() { return 0; }");

  std::string ignore_options = "-fmessage-length=* -ignored-option -not*valid"
    "-also*not*valid* -has-value1=2* -has-value2=30";

  exp_ctx.set_ignore_options(Util::split_into_strings(ignore_options, " "));
  act_ctx.set_ignore_options(Util::split_into_strings(ignore_options, " "));

  Args exp_cc = Args::from_string(
      "cc -c foo.c -Wall -Werror -has-value1=40 -has-value2=50 -g");
  Args act_cc = Args::from_string(
    "cc -c foo.c -fmessage-length=20 -Wall -Werror -ignored-option"
    " -has-value1=40 -has-value2=50 -g");
  std::vector<std::string>exp_ignore_options = Util::split_into_strings(
      "-fmessage-length=* -ignored-option -has-value1=2* -has-value2=30", " ");
  Args exp_cpp = exp_cc;
  Args act_cpp = act_cc;

  // These run in direct mode so we don't try executing a compiler for the
  // test. We also discard the digest because it comes from the manifest, which
  // is irrelevant here.
  calculate_result_name(exp_ctx, exp_cc, exp_cpp, exp_hash, true);
  calculate_result_name(act_ctx, act_cc, act_cpp, act_hash, true);

  CHECK(exp_ignore_options == act_ctx.ignore_options());
  CHECK(exp_ctx.hashed_args == act_ctx.hashed_args);
}
