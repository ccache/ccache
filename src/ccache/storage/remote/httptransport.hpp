// Copyright (C) 2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.

#pragma once

#include "httpurl.hpp"

#include <ccache/storage/remote/remotestorage.hpp>
#include <ccache/util/wincompat.hpp>

#include <httplib.h>

namespace storage::remote::detail {

inline RemoteStorage::Backend::Failure
http_failure_from_httplib_error(httplib::Error error)
{
  return error == httplib::Error::ConnectionTimeout
           ? RemoteStorage::Backend::Failure::timeout
           : RemoteStorage::Backend::Failure::error;
}

} // namespace storage::remote::detail
