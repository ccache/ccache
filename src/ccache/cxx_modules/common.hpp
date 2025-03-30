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

#pragma once

#include <glaze/core/opts.hpp>
#include <glaze/core/read.hpp>
#include <glaze/glaze.hpp>

#include <filesystem>
#include <string_view>
#include <type_traits>

namespace cxx_modules {

template<class T> concept TriviallyCopyable = requires
{
  requires std::is_trivially_copyable_v<T>;
};

template<class T> concept NonCopyable = requires
{
  requires !TriviallyCopyable<T>;
  requires !std::is_copy_assignable_v<T>;
  requires !std::is_copy_constructible_v<T>;
};

template<class T> concept Movable = requires
{
  requires std::is_move_assignable_v<T>;
  requires std::is_move_constructible_v<T>;
};

template<class T> concept FromJSON = glz::read_supported<glz::JSON, T>;

// Specialized string_view class representing logical module names
class name_view
{
private:
  std::string_view m_repr;
  bool m_dotted = false;

  friend glz::from<glz::JSON, name_view>;

public:
  name_view() noexcept;
  name_view(std::string_view view) noexcept;

  operator std::string_view() const noexcept;

  [[nodiscard]] constexpr auto
  is_dotted() const noexcept -> bool
  {
    return m_dotted;
  }
};
static_assert(TriviallyCopyable<name_view>);

// Specialized string_view class representing module file paths
class path_view
{
private:
  std::string_view m_repr;

  friend glz::from<glz::JSON, path_view>;

public:
  path_view() noexcept;
  path_view(std::string_view view) noexcept;

  operator std::string_view() const noexcept;
  explicit operator std::filesystem::path() const;
};

} // namespace cxx_modules

// clang-format off
namespace glz {
  template<>
  struct from<JSON, cxx_modules::path_view>
  {
    template<auto Opts>
    static void
    op(cxx_modules::path_view& value, auto&&... args)
    {
      parse<JSON>::op<Opts>(value.m_repr, args...);
    }
  };
// clang-format on

// clang-format off
  template<>
  struct from<JSON, cxx_modules::name_view>
  {
    template<auto Opts>
    static void
    op(cxx_modules::name_view& value, auto&&... args)
    {
      parse<JSON>::op<Opts>(value.m_repr, args...);
    }
  };
// clang-format on
} // namespace glz

namespace cxx_modules {

static_assert(TriviallyCopyable<name_view>);
static_assert(FromJSON<name_view>);

static_assert(TriviallyCopyable<path_view>);
static_assert(FromJSON<path_view>);

} // namespace cxx_modules
