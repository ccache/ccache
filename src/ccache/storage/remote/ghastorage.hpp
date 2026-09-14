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

struct GhaStorageConfig
{
  std::string results_url;
  std::string token;
  std::string prefix;
  bool debug = false;
  std::chrono::milliseconds connect_timeout = k_default_connect_timeout;
  std::chrono::milliseconds operation_timeout = k_default_operation_timeout;
};

GhaStorageConfig
parse_gha_storage_config(
  const Url& url,
  const std::vector<RemoteStorage::Backend::Attribute>& attributes);

std::string make_gha_storage_key(const Hash::Digest& key,
                                 const std::string& prefix);

std::optional<std::string>
extract_gha_archive_location(std::string_view response_body);

} // namespace detail

class GhaStorage : public RemoteStorage
{
public:
  std::unique_ptr<Backend> create_backend(
    const Url& url,
    const std::vector<Backend::Attribute>& attributes) const override;
};

} // namespace storage::remote
