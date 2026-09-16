// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#pragma once

#include <tl/expected.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace storage::remote::detail {

struct DockerCredential
{
  std::string username;
  std::string secret;
};

struct RegistryBearerChallenge
{
  std::string realm;
  std::string service;
};

std::optional<std::string> extract_json_string(std::string_view json,
                                               std::string_view key);

tl::expected<DockerCredential, std::string>
get_docker_credential(std::string_view helper, std::string_view registry);

std::optional<RegistryBearerChallenge>
parse_registry_bearer_challenge(std::string_view value);

} // namespace storage::remote::detail
