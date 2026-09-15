// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#include <ccache/storage/remote/credentials.hpp>

#include <doctest/doctest.h>

TEST_SUITE_BEGIN("storage::remote::credentials");

TEST_CASE("extract JSON string")
{
  const auto value = storage::remote::detail::extract_json_string(
    R"({"Secret":"one\ntwo"})", "Secret");

  REQUIRE(value);
  CHECK(*value == "one\ntwo");
}

TEST_CASE("reject missing or malformed JSON string")
{
  CHECK_FALSE(storage::remote::detail::extract_json_string("{}", "Secret"));
  CHECK_FALSE(
    storage::remote::detail::extract_json_string(R"({"Secret":42})", "Secret"));
  CHECK_FALSE(storage::remote::detail::extract_json_string(
    R"({"Secret":"unterminated})", "Secret"));
}

TEST_SUITE_END();
