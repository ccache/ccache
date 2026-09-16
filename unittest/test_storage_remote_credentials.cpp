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

TEST_CASE("Docker credential keeps username and secret separate")
{
  const storage::remote::detail::DockerCredential credential{"octocat", "secret"};

  CHECK(credential.username == "octocat");
  CHECK(credential.secret == "secret");
}

TEST_CASE("parse registry bearer challenge")
{
  const auto challenge = storage::remote::detail::parse_registry_bearer_challenge(
    R"(Bearer realm="https://ghcr.io/token",service="ghcr.io")");

  REQUIRE(challenge);
  CHECK(challenge->realm == "https://ghcr.io/token");
  CHECK(challenge->service == "ghcr.io");
  CHECK_FALSE(storage::remote::detail::parse_registry_bearer_challenge("Basic realm=\"x\""));
}

TEST_SUITE_END();
