// Copyright (C) 2020-2024 Joel Rosdahl and other contributors
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

#include <ccache/cxx_modules/json.hpp>

namespace cxx_modules::json {

ParseError::ParseError(glz::error_ctx ctx) : m_repr(ctx)
{
}

ParseError::operator bool() const noexcept
{
  return m_repr.operator bool();
}

auto
ParseError::operator==(ParseError::code const err) const noexcept -> bool
{
  return m_repr.operator==(err);
}

auto
ParseError::format(std::string_view borrowed) const -> std::string
{
  return glz::format_error(m_repr, borrowed);
}

} // namespace cxx_modules::json
