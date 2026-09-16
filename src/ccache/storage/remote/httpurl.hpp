// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#pragma once

#include <cxxurl/url.hpp>

#include <string>

namespace storage::remote::detail {

inline Url
http_base_url(const Url& url)
{
  Url base;
  base.scheme(url.scheme());
  base.host(url.host(), url.ip_version());
  if (!url.port().empty()) {
    base.port(url.port());
  }
  return base;
}

inline std::string
http_url_path(const Url& url)
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
