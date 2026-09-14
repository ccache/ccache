// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#include "testutil.hpp"

#include <ccache/core/exceptions.hpp>
#include <ccache/storage/remote/ghastorage.hpp>
#include <ccache/storage/storage.hpp>
#include <ccache/util/environment.hpp>

#include <cxxurl/url.hpp>
#include <doctest/doctest.h>

#include <string>

TEST_SUITE_BEGIN("storage::remote::GhaStorage");

namespace {

Hash::Digest
test_digest()
{
  return {0x13, 0x12, 0x11, 0x10, 0x0f, 0x0e, 0x0d,
          0x0c, 0x0b, 0x0a, 0x09, 0x08, 0x07, 0x06,
          0x05, 0x04, 0x03, 0x02, 0x01, 0x00};
}

} // namespace

TEST_CASE("parse gha storage URL")
{
  const auto config = storage::remote::detail::parse_gha_storage_config(
    Url("gha://project/cache"),
    {{"url", "https://cache.example.invalid/results/", "ignored"},
     {"token", "secret-token", "secret-token"},
     {"debug", "true", "true"}});

  CHECK(config.results_url == "https://cache.example.invalid/results/");
  CHECK(config.prefix == "project/cache");
  CHECK(config.token == "secret-token");
  CHECK(config.debug);
}

TEST_CASE("parse gha token and URL from environment")
{
  TestUtil::TestContext test_context;
  util::setenv("CCACHE_TEST_GHA_URL", "https://cache.example.invalid/runtime/");
  util::setenv("CCACHE_TEST_GHA_TOKEN", "secret-from-env");

  const auto config = storage::remote::detail::parse_gha_storage_config(
    Url("gha://"),
    {{"url-env", "CCACHE_TEST_GHA_URL", "CCACHE_TEST_GHA_URL"},
     {"token-env", "CCACHE_TEST_GHA_TOKEN", "CCACHE_TEST_GHA_TOKEN"},
     {"prefix", "manual", "manual"}});

  CHECK(config.results_url == "https://cache.example.invalid/runtime/");
  CHECK(config.prefix == "manual");
  CHECK(config.token == "secret-from-env");
  CHECK(storage::get_redacted_url_str_for_logging(
          Url("https://user:secret@cache.example.invalid/runtime/"))
        == "https://********@cache.example.invalid/runtime/");
}

TEST_CASE("reject gha storage without runtime configuration")
{
  TestUtil::TestContext test_context;
  util::unsetenv("ACTIONS_RESULTS_URL");
  util::unsetenv("ACTIONS_CACHE_URL");
  util::unsetenv("ACTIONS_RUNTIME_TOKEN");
  util::unsetenv("ACTIONS_ID_TOKEN_REQUEST_TOKEN");

  CHECK_THROWS_WITH_AS(
    storage::remote::detail::parse_gha_storage_config(Url("gha://"), {}),
    doctest::Contains("CCACHE-GHA-0001"),
    core::Fatal);
}

TEST_CASE("make gha key from full digest")
{
  const auto key =
    storage::remote::detail::make_gha_storage_key(test_digest(), "");

  CHECK(key == "131211100f0e0d0c0b0a09080706050403020100");
  CHECK(key.size() == 40);
}

TEST_CASE("make gha prefixed key")
{
  CHECK(storage::remote::detail::make_gha_storage_key(test_digest(), "linux/x64")
        == "linux/x64/131211100f0e0d0c0b0a09080706050403020100");
}

TEST_CASE("extract gha archive location")
{
  const auto archive_location =
    storage::remote::detail::extract_gha_archive_location(
      R"({"cacheKey":"key","archiveLocation":"https://cache.example.invalid/a/b?sig=one%2Ftwo"})");

  REQUIRE(archive_location);
  CHECK(*archive_location
        == "https://cache.example.invalid/a/b?sig=one%2Ftwo");
}

TEST_CASE("extract gha archive location with escaped slashes")
{
  const auto archive_location =
    storage::remote::detail::extract_gha_archive_location(
      R"({
        "archiveLocation" : "https:\/\/cache.example.invalid\/entry?sig=x"
      })");

  REQUIRE(archive_location);
  CHECK(*archive_location == "https://cache.example.invalid/entry?sig=x");
}

TEST_CASE("reject gha lookup response without archive location")
{
  CHECK_FALSE(storage::remote::detail::extract_gha_archive_location(
    R"({"cacheKey":"key"})"));
  CHECK_FALSE(storage::remote::detail::extract_gha_archive_location(
    R"({"archiveLocation":true})"));
}

TEST_CASE("make gha archive path preserves signed query")
{
  CHECK(storage::remote::detail::make_gha_archive_path(
          "https://cache.example.invalid/a/b?sig=one%2Ftwo&empty#fragment")
        == "/a/b?sig=one%2Ftwo&empty");
  CHECK(storage::remote::detail::make_gha_archive_path(
          "https://cache.example.invalid?sig=one%2Ftwo")
        == "?sig=one%2Ftwo");
  CHECK(storage::remote::detail::make_gha_archive_path(
          "https://cache.example.invalid")
        == "/");
}

TEST_SUITE_END();
