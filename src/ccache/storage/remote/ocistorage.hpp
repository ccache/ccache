// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#pragma once

#include <ccache/storage/remote/remotestorage.hpp>

#include <cxxurl/url.hpp>

#include <memory>
#include <string>
#include <vector>

namespace storage::remote {

namespace detail {

struct OciStorageConfig
{
  std::string registry;
  std::string repository;
  std::string prefix;
  std::string credential;
  std::string credential_helper;
  bool insecure = false;
  bool debug = false;
  std::chrono::milliseconds connect_timeout = k_default_connect_timeout;
  std::chrono::milliseconds operation_timeout = k_default_operation_timeout;
};

OciStorageConfig
parse_oci_storage_config(
  const Url& url,
  const std::vector<RemoteStorage::Backend::Attribute>& attributes);

std::string make_oci_storage_key(const Hash::Digest& key,
                                 const std::string& prefix);

std::string make_oci_manifest_tag(const std::string& key);

std::string make_oci_blob_digest(std::span<const uint8_t> value);

std::string make_oci_blob_path(const std::string& repository,
                               const std::string& digest);

std::string make_oci_manifest_path(const std::string& repository,
                                   const std::string& tag_or_digest);

} // namespace detail

class OciStorage : public RemoteStorage
{
public:
  std::unique_ptr<Backend> create_backend(
    const Url& url,
    const std::vector<Backend::Attribute>& attributes,
    const BackendContext& context) const override;
};

} // namespace storage::remote
