// Copyright (C) 2021 Joel Rosdahl and other contributors
//
// See doc/AUTHORS.adoc for a complete list of contributors.
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

#include "SecondaryStorage.hpp"

#include <util/expected.hpp>
#include <util/string.hpp>

namespace storage {
namespace secondary {

bool
SecondaryStorage::Backend::is_framework_attribute(const std::string& name)
{
  return name == "read-only";
}

std::chrono::milliseconds
SecondaryStorage::Backend::parse_timeout_attribute(const std::string& value)
{
  return std::chrono::milliseconds(util::value_or_throw<Failed>(
    util::parse_unsigned(value, 1, 60 * 1000, "timeout")));
}

} // namespace secondary
} // namespace storage
