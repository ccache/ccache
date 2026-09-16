// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#include "credentials.hpp"

#include <ccache/util/args.hpp>
#include <ccache/util/exec.hpp>
#include <ccache/util/format.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>

namespace storage::remote::detail {

namespace {

std::optional<std::string>
find_json_string(const std::string_view json, const std::string_view key)
{
  const std::string quoted_key = FMT("\"{}\"", key);
  const size_t key_pos = json.find(quoted_key);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }

  size_t pos = json.find(':', key_pos + quoted_key.size());
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  ++pos;
  while (pos < json.size()
         && std::isspace(static_cast<unsigned char>(json[pos]))) {
    ++pos;
  }
  if (pos == json.size() || json[pos] != '"') {
    return std::nullopt;
  }
  ++pos;

  std::string value;
  bool escaped = false;
  for (; pos < json.size(); ++pos) {
    const char c = json[pos];
    if (escaped) {
      switch (c) {
      case '"':
      case '\\':
      case '/':
        value.push_back(c);
        break;
      case 'b':
        value.push_back('\b');
        break;
      case 'f':
        value.push_back('\f');
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      default:
        return std::nullopt;
      }
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
    } else if (c == '"') {
      return value;
    } else {
      value.push_back(c);
    }
  }

  return std::nullopt;
}

bool
is_helper_name(const std::string_view value)
{
  return !value.empty()
         && std::all_of(value.begin(), value.end(), [](const char c) {
              return std::isalnum(static_cast<unsigned char>(c)) || c == '-'
                     || c == '_';
            });
}

} // namespace

std::optional<std::string>
extract_json_string(const std::string_view json, const std::string_view key)
{
  return find_json_string(json, key);
}

tl::expected<DockerCredential, std::string>
get_docker_credential(const std::string_view helper,
                      const std::string_view registry)
{
  if (!is_helper_name(helper)) {
    return tl::unexpected("invalid Docker credential helper name");
  }

  const util::Args args{FMT("docker-credential-{}", helper), "get"};
  const auto output = util::exec_to_string(args, FMT("{}\n", registry));
  if (!output) {
    return tl::unexpected("Docker credential helper failed");
  }

  const auto username = extract_json_string(*output, "Username");
  const auto secret = extract_json_string(*output, "Secret");
  if (!username || username->empty() || !secret || secret->empty()) {
    return tl::unexpected("Docker credential helper returned incomplete credentials");
  }
  return DockerCredential{*username, *secret};
}

} // namespace storage::remote::detail
