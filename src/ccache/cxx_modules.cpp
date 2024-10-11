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

#include "cxx_modules.hpp"

#include <tl/expected.hpp>

#include <glaze/glaze.hpp>

#include <string_view>

// NOTE: clang-format toggled off for some sections below because the current
// clang-format project settings do not handle them well. These toggles could be
// removed after bringing the settings more up-to-date.

// clang-format off
template<>
struct glz::meta<cxx_modules::depfiles::LookupMethod>
{
  using enum cxx_modules::depfiles::LookupMethod;
  static constexpr auto value = glz::enumerate(
    "by-name", ByName,
    "include-angle", IncludeAngle,
    "include-quote", IncludeQuote
  );
};
// clang-format on

// clang-format off
template<>
struct glz::meta<cxx_modules::depfiles::ProvidedModuleDesc>
{
  using T = cxx_modules::depfiles::ProvidedModuleDesc;
  static constexpr auto value = glz::object(
    "source-path", &T::source_path,
    "compiled-module-path", &T::compiled_module_path,
    "unique-on-source-path", &T::unique_on_source_path,
    "logical-name", &T::logical_name,
    "is-interface", &T::is_interface
  );
};
// clang-format on

// clang-format off
template<>
struct glz::meta<cxx_modules::depfiles::RequiredModuleDesc>
{
  using T = cxx_modules::depfiles::RequiredModuleDesc;
  static constexpr auto value = glz::object(
    "source-path", &T::source_path,
    "compiled-module-path", &T::compiled_module_path,
    "unique-on-source-path", &T::unique_on_source_path,
    "logical-name", &T::logical_name,
    "lookup-method", &T::lookup_method
  );
};
// clang-format on

// clang-format off
template<>
struct glz::meta<cxx_modules::depfiles::DepInfo>
{
  using T = cxx_modules::depfiles::DepInfo;
  static constexpr auto value = glz::object(
    "work-directory", &T::work_directory,
    "primary-output", &T::primary_output,
    "outputs", &T::outputs,
    "provides", &T::provides,
    "requires", &T::requires_
  );
};
// clang-format on

// clang-format off
template<>
struct glz::meta<cxx_modules::depfiles::DepFile>
{
  using T = cxx_modules::depfiles::DepFile;
  static constexpr auto value = glz::object(
    "version", &T::version,
    "revision", &T::revision,
    "rules", &T::rules
  );
};
// clang-format on

// clang-format off
namespace glz::detail {
template<>
struct from<JSON, cxx_modules::depfiles::path_view>
{
  template<auto Opts>
  static void
  op(cxx_modules::depfiles::path_view& value, auto&&... args)
  {
    read<JSON>::op<Opts>(value.m_repr, args...);
  }
};
// clang-format on

// clang-format off
template<>
struct from<JSON, cxx_modules::depfiles::name_view>
{
  template<auto Opts>
  static void
  op(cxx_modules::depfiles::name_view& value, auto&&... args)
  {
    read<JSON>::op<Opts>(value.m_repr, args...);
  }
};
// clang-format on
} // namespace glz::detail

namespace cxx_modules::depfiles {
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

DepFile::ParseError::ParseError(glz::error_ctx&& ctx) : m_repr(ctx)
{
}

DepFile::ParseError::operator bool() const noexcept
{
  return m_repr.operator bool();
}

auto
DepFile::ParseError::operator==(
  DepFile::ParseError::code const err) const noexcept -> bool
{
  return m_repr.operator==(err);
}

auto
DepFile::ParseError::format(std::string_view buffer) const -> std::string
{
  return glz::format_error(m_repr, buffer);
}

auto
DepFile::parse(std::string_view buffer) noexcept
  -> tl::expected<DepFile, DepFile::ParseError>
{
  static constexpr glz::opts ParserOpts = glz::opts{
    .format = glz::JSON,
    .error_on_missing_keys = true,
    .validate_trailing_whitespace = false,
    .raw_string = true,
  };
  DepFile dep_file{};
  if (auto&& err = glz::read<ParserOpts, DepFile>(dep_file, buffer)) {
    return tl::unexpected(
      DepFile::ParseError(std::forward<decltype(err)>(err)));
  }
  return dep_file;
}
} // namespace cxx_modules::depfiles
