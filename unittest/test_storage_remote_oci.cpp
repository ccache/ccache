// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#include "testutil.hpp"

#include <ccache/storage/remote/ocistorage.hpp>
#include <ccache/storage/storage.hpp>
#include <ccache/util/environment.hpp>

#include <cxxurl/url.hpp>
#include <doctest/doctest.h>

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
    Url("oci://ghcr.io/OWNER/NAME/ccache/prod"),
    {{"token", "secret-token", "secret-token"}, {"debug", "true", "true"}});

  CHECK(config.registry == "ghcr.io");
  CHECK(config.repository == "OWNER/NAME");
  CHECK(config.prefix == "prod");
  CHECK(config.token == "secret-token");
  CHECK(config.debug);
}

TEST_CASE("parse oci token from environment")
{
  TestUtil::TestContext test_context;
  util::setenv("CCACHE_TEST_OCI_TOKEN", "secret-from-env");

  const auto config = storage::remote::detail::parse_oci_storage_config(
    Url("oci://registry.example.invalid/ns/cache"),
    {{"token-env", "CCACHE_TEST_OCI_TOKEN", "CCACHE_TEST_OCI_TOKEN"}});

  CHECK(config.token == "secret-from-env");
  CHECK(storage::get_redacted_url_str_for_logging(
          Url("oci://user:secret@registry.example.invalid/ns/cache"))
        == "oci://********@registry.example.invalid/ns/cache");
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
