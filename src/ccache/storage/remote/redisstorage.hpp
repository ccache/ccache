// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITHOUT
// ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.
//
// You should have received a copy of the GNU General Public License along with
// this program; if not, write to the Free Software Foundation, Inc., 51
// Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA

#pragma once

#include <ccache/storage/remote/remotestorage.hpp>

#include <cxxurl/url.hpp>

#include <memory>
#include <vector>

namespace storage::remote {

class RedisStorage : public RemoteStorage
{
public:
  std::unique_ptr<Backend> create_backend(
    const Url& url,
    const std::vector<Backend::Attribute>& attributes) const override;
};

} // namespace storage::remote
