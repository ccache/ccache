// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#pragma once

#include "system.hpp"

#include "third_party/fmt/core.h"
#include "third_party/nonstd/string_view.hpp"

// Specialization of fmt::formatter for nonstd::string_view.
namespace fmt {

template<> struct formatter<nonstd::string_view>
{
  template<typename ParseContext>
  constexpr auto
  parse(ParseContext& ctx) const -> decltype(ctx.begin())
  {
    return ctx.begin();
  }

  template<typename FormatContext>
  auto
  format(const nonstd::string_view& sv, FormatContext& ctx)
    -> decltype(ctx.out())
  {
    return format_to(ctx.out(), "{}", fmt::string_view(sv.data(), sv.size()));
  }
};

} // namespace fmt
