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
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>

#include <cxxurl/url.hpp>
#include <httplib.h>

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
parse_bool(std::string_view value)
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

inline RemoteStorage::Backend::Failure
failure_from_httplib_error(httplib::Error error)
{
  return error == httplib::Error::ConnectionTimeout
           ? RemoteStorage::Backend::Failure::timeout
           : RemoteStorage::Backend::Failure::error;
}

inline void
log_diagnostic(const std::string& code, const std::string& message)
{
  LOG("{}: {}", code, message);
}

inline Url
partial_url(const Url& url)
{
  Url partial;
  partial.scheme(url.scheme());
  partial.host(url.host(), url.ip_version());
  if (!url.port().empty()) {
    partial.port(url.port());
  }
  return partial;
}

inline std::string
url_path(const Url& url)
{
  auto path = url.path();
  if (path.empty()) {
    return "/";
  }
  if (path.back() != '/') {
    path += '/';
  }
  return path;
}

} // namespace storage::remote::detail
