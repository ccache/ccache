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

#include <ccache/cxx_modules/common.hpp>

namespace cxx_modules {

name_view::name_view() noexcept = default;

name_view::name_view(std::string_view view) noexcept
  : m_repr(view),
    m_dotted(m_repr.contains('.'))
{
}

name_view::operator std::string_view() const noexcept
{
  return m_repr;
}

path_view::path_view() noexcept = default;

path_view::path_view(std::string_view view) noexcept : m_repr(view)
{
}

path_view::operator std::string_view() const noexcept
{
  return m_repr;
}

path_view::operator std::filesystem::path() const
{
  return std::filesystem::path(m_repr);
}

} // namespace cxx_modules
