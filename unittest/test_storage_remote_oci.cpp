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
#include <ccache/storage/remote/httptransport.hpp>
#include <ccache/storage/remote/ocistorage.hpp>
#include <ccache/storage/storage.hpp>
#include <ccache/util/environment.hpp>
#include <ccache/util/file.hpp>

#include <cxxurl/url.hpp>
#include <doctest/doctest.h>

#ifndef _WIN32
#  include <sys/stat.h>
#endif

TEST_SUITE_BEGIN("storage::remote::OciStorage");

namespace {

Hash::Digest
test_digest()
{
  return {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
          0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
          0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13};
}

} // namespace

TEST_CASE("parse oci storage URL")
{
  const auto config = storage::remote::detail::parse_oci_storage_config(
    Url("oci://ghcr.io:5443/OWNER/NAME/ccache/prod"),
    {{"credential-helper", "pass", "pass"},
     {"insecure", "true", "true"},
     {"debug", "true", "true"}});

  CHECK(config.registry == "ghcr.io:5443");
  CHECK(config.repository == "OWNER/NAME");
  CHECK(config.prefix == "prod");
  CHECK(config.credential.empty());
  CHECK(config.credential_helper == "pass");
  CHECK(config.insecure);
  CHECK(config.debug);
}

TEST_CASE("create HTTPS OCI storage backend")
{
  storage::remote::OciStorage storage;
  CHECK_NOTHROW(storage.create_backend(
    Url("oci://registry.example.invalid/ns/cache"), {}, {{}}));
}

TEST_CASE("preserve HTTPS redirect transport")
{
  const Url redirect_url("https://blob.example.invalid:5443/cache/blob");
  const Url redirect_base = storage::remote::detail::http_base_url(redirect_url);

  CHECK(redirect_base.scheme() == "https");
  CHECK(redirect_base.host() == "blob.example.invalid");
  CHECK(redirect_base.port() == "5443");
}

TEST_CASE("reject OCI token configuration")
{
  CHECK_THROWS_AS(storage::remote::detail::parse_oci_storage_config(
                    Url("oci://registry.example.invalid/ns/cache"),
                    {{"token", "secret-token", "secret-token"}}),
                  core::Fatal);
  CHECK(storage::get_redacted_url_str_for_logging(
          Url("oci://user:secret@registry.example.invalid/ns/cache"))
        == "oci://********@registry.example.invalid/ns/cache");
}

TEST_CASE("parse OCI credential from a private file")
{
  TestUtil::TestContext test_context;
  REQUIRE(util::write_file("oci-credential", "secret-from-file\n"));
#ifndef _WIN32
  REQUIRE(chmod("oci-credential", S_IRUSR | S_IWUSR) == 0);
#endif
  util::setenv("CCACHE_TEST_OCI_CREDENTIAL_FILE", "oci-credential");

  const auto config = storage::remote::detail::parse_oci_storage_config(
    Url("oci://registry.example.invalid/ns/cache"),
    {{"credential-file-env",
      "CCACHE_TEST_OCI_CREDENTIAL_FILE",
      "CCACHE_TEST_OCI_CREDENTIAL_FILE"}});

  CHECK(config.credential == "secret-from-file");
}

TEST_CASE("parse structured OCI credential from a private file")
{
  TestUtil::TestContext test_context;
  REQUIRE(util::write_file(
    "oci-credential", R"({"Username":"octocat","Secret":"secret-from-file"})"));
#ifndef _WIN32
  REQUIRE(chmod("oci-credential", S_IRUSR | S_IWUSR) == 0);
#endif

  const auto config = storage::remote::detail::parse_oci_storage_config(
    Url("oci://registry.example.invalid/ns/cache"),
    {{"credential-file", "oci-credential", "oci-credential"}});

  CHECK(config.credential_username == "octocat");
  CHECK(config.credential == "secret-from-file");
}

#ifndef _WIN32
TEST_CASE("reject OCI credential file readable by other users")
{
  TestUtil::TestContext test_context;
  REQUIRE(util::write_file("oci-credential", "secret-from-file\n"));
  REQUIRE(chmod("oci-credential", S_IRUSR | S_IWUSR | S_IRGRP) == 0);

  CHECK_THROWS_AS(storage::remote::detail::parse_oci_storage_config(
                    Url("oci://registry.example.invalid/ns/cache"),
                    {{"credential-file", "oci-credential", "oci-credential"}}),
                  core::Fatal);
}
#endif

TEST_CASE("reject OCI credential helper and file together")
{
  TestUtil::TestContext test_context;
  REQUIRE(util::write_file("oci-credential", "secret-from-file\n"));
#ifndef _WIN32
  REQUIRE(chmod("oci-credential", S_IRUSR | S_IWUSR) == 0);
#endif

  CHECK_THROWS_AS(storage::remote::detail::parse_oci_storage_config(
                    Url("oci://registry.example.invalid/ns/cache"),
                    {{"credential-helper", "pass", "pass"},
                     {"credential-file", "oci-credential", "oci-credential"}}),
                  core::Fatal);
}

#ifndef _WIN32
TEST_CASE("parse OCI systemd credential")
{
  TestUtil::TestContext test_context;
  REQUIRE(util::write_file("oci-credential", "secret-from-systemd\n"));
  REQUIRE(chmod("oci-credential", S_IRUSR | S_IWUSR) == 0);
  util::setenv("CREDENTIALS_DIRECTORY", ".");

  const auto config = storage::remote::detail::parse_oci_storage_config(
    Url("oci://registry.example.invalid/ns/cache"),
    {{"systemd-credential", "oci-credential", "oci-credential"}});

  CHECK(config.credential == "secret-from-systemd");
}

TEST_CASE("reject OCI systemd credential path")
{
  TestUtil::TestContext test_context;
  util::setenv("CREDENTIALS_DIRECTORY", ".");

  CHECK_THROWS_AS(storage::remote::detail::parse_oci_storage_config(
                    Url("oci://registry.example.invalid/ns/cache"),
                    {{"systemd-credential", "../credential", "../credential"}}),
                  core::Fatal);
}
#endif

TEST_CASE("select OCI debug logging from runtime environment")
{
  TestUtil::TestContext test_context;
  util::setenv("ACTIONS_STEP_DEBUG", "true");

  const auto config = storage::remote::detail::parse_oci_storage_config(
    Url("oci://registry.example.invalid/ns/cache"), {});

  CHECK(config.debug);
}

TEST_CASE("redact OCI numeric registry host for logging")
{
  CHECK(storage::get_redacted_url_str_for_logging(
          Url("oci://198.51.100.7/ns/cache"))
        == "oci://<redacted-host>/ns/cache");
  const auto ipv6_url = storage::get_redacted_url_str_for_logging(
    Url("oci://[2001:db8::7]/ns/cache"));
  CHECK(ipv6_url.find("2001:db8::7") == std::string::npos);
  CHECK(ipv6_url.find("<redacted-host>") != std::string::npos);
  CHECK(storage::get_redacted_url_str_for_logging(
          Url("oci://ghcr.io/ns/cache"))
        == "oci://ghcr.io/ns/cache");
}

TEST_CASE("make oci key from full digest")
{
  const auto key =
    storage::remote::detail::make_oci_storage_key(test_digest(), "");

  CHECK(key == "000102030405060708090a0b0c0d0e0f10111213");
  CHECK(key.size() == 40);
}

TEST_CASE("make oci prefixed key and API path")
{
  const auto key =
    storage::remote::detail::make_oci_storage_key(test_digest(), "team/cache");

  CHECK(key == "team/cache/000102030405060708090a0b0c0d0e0f10111213");
  CHECK(storage::remote::detail::make_oci_manifest_tag(key)
        == "ccache-9293f71edbcaee23409f4c45fb7ed020e15e6d10ae02e51276f215c4a2c0cb13-"
           "000102030405060708090a0b0c0d0e0f10111213");
  CHECK(storage::remote::detail::make_oci_manifest_path("owner/name", "ccache-tag")
        == "/v2/owner/name/manifests/ccache-tag");
  CHECK(storage::remote::detail::make_oci_blob_path(
          "owner/name", "sha256:abcd")
        == "/v2/owner/name/blobs/sha256:abcd");
}

TEST_CASE("make OCI SHA-256 blob digest")
{
  constexpr std::array<uint8_t, 3> value = {'a', 'b', 'c'};
  CHECK(storage::remote::detail::make_oci_blob_digest(value)
        == "sha256:ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_SUITE_END();
