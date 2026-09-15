// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#pragma once

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace storage::remote::detail {

inline std::optional<std::string>
getenv_string(const char* name)
{
  const char* value = std::getenv(name);
  return value && *value ? std::optional<std::string>(value) : std::nullopt;
}

inline bool
parse_bool(const std::string_view value)
{
  return value == "1" || value == "true" || value == "yes" || value == "on";
}

inline std::string
strip_slashes(std::string value)
{
  while (!value.empty() && value.front() == '/') {
    value.erase(value.begin());
  }
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

} // namespace storage::remote::detail
