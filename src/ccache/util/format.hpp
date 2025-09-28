// Copyright (C) 2019-2024 Joel Rosdahl and other contributors
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

#include <fmt/core.h>
#include <fmt/format.h>

#include <filesystem>
#include <string_view>
#include <system_error>

// Convenience macro for calling `fmt::format` with `FMT_STRING` around the
// format string literal.
#define FMT(format_, ...) fmt::format(FMT_STRING(format_), __VA_ARGS__)

// Convenience macro for calling `fmt::print` with `FMT_STRING` around the
// format string literal.
#define PRINT(stream_, format_, ...)                                           \
  fmt::print(stream_, FMT_STRING(format_), __VA_ARGS__)

// Convenience macro for calling `fmt::print` with a message that is not a
// format string.
#define PRINT_RAW(stream_, message_) fmt::print(stream_, "{}", message_)

template<>
struct fmt::formatter<std::filesystem::path> : fmt::formatter<std::string_view>
{
  template<class T>
  auto
  format(const std::filesystem::path& path, T& ctx) const
  {
    return fmt::formatter<std::string_view>::format(path.string(), ctx);
  }
};

template<>
struct fmt::formatter<std::error_code> : fmt::formatter<std::string_view>
{
  template<class T>
  auto
  format(const std::error_code& code, T& ctx) const
  {
    return fmt::formatter<std::string_view>::format(code.message(), ctx);
  }
};
