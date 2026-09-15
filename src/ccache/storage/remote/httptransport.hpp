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
#include <ccache/util/wincompat.hpp>

#include <cxxurl/url.hpp>
#include <httplib.h>

#include <string>

namespace storage::remote::detail {

inline RemoteStorage::Backend::Failure
http_failure_from_httplib_error(httplib::Error error)
{
  return error == httplib::Error::ConnectionTimeout
           ? RemoteStorage::Backend::Failure::timeout
           : RemoteStorage::Backend::Failure::error;
}

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
