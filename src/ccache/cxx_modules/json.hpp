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

#include <ccache/core/statistic.hpp>
#include <ccache/cxx_modules/common.hpp>
#include <ccache/util/bytes.hpp>
#include <ccache/util/logging.hpp>

#include <tl/expected.hpp>

#include <glaze/glaze.hpp>

namespace cxx_modules::json {

static constexpr glz::opts ParseOpts = glz::opts{
  .format = glz::JSON,
  .error_on_unknown_keys = false,
  .error_on_missing_keys = true,
  .raw_string = true, // TODO: potentially unescape if path not found
  // .partial_read = true, // TODO: enable when testing format versions
};

class ParseError;

template<class T, glz::opts Opts = ParseOpts>
auto parse(std::string_view borrowed, T& dest) noexcept
  -> tl::expected<void, json::ParseError>;

class ParseError
{
  static_assert(TriviallyCopyable<glz::error_ctx>);

  glz::error_ctx m_repr;

  ParseError(glz::error_ctx);

  template<class T, glz::opts Opts>
  friend auto parse(std::string_view borrowed, T& dest) noexcept
    -> tl::expected<void, json::ParseError>;

public:
  using code = glz::error_code;

  operator bool() const noexcept;

  auto operator==(code err) const noexcept -> bool;

  [[nodiscard]] auto format(std::string_view borrowed) const -> std::string;
};
static_assert(TriviallyCopyable<ParseError>);

template<class T, glz::opts Opts>
auto
parse(std::string_view borrowed, T& dest) noexcept
  -> tl::expected<void, json::ParseError>
{
  if (auto&& err = glz::read<Opts, T>(dest, borrowed)) {
    return tl::unexpected(json::ParseError(std::forward<decltype(err)>(err)));
  }
  return {};
}

template<class T, glz::opts Opts = ParseOpts>
auto
parse(std::string_view borrowed) noexcept -> tl::expected<T, json::ParseError>
{
  T dest{};
  if (auto result = parse<T, Opts>(borrowed, dest)) {
    return dest;
  } else {
    return tl::unexpected(std::move(result.error()));
  }
}

} // namespace cxx_modules::json
