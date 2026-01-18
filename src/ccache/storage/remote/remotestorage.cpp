// Copyright (C) 2021-2026 Joel Rosdahl and other contributors
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

#include "remotestorage.hpp"

#include <ccache/util/expected.hpp>
#include <ccache/util/string.hpp>

namespace storage::remote {

std::chrono::milliseconds
RemoteStorage::Backend::parse_timeout_attribute(const std::string& value)
{
  return std::chrono::milliseconds(util::value_or_throw<Failed>(
    util::parse_unsigned(value, 1, 60 * 1000, "timeout")));
}

} // namespace storage::remote
