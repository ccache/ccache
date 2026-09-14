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
#include <vector>

namespace storage::remote {

class GhaStorage : public RemoteStorage
{
public:
  std::unique_ptr<Backend> create_backend(
    const Url& url,
    const std::vector<Backend::Attribute>& attributes) const override;
};

} // namespace storage::remote
