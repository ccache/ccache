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

#include <ccache/cxx_modules/common.hpp>
#include <ccache/util/noncopyable.hpp>

// This header declares an internal API for zero-copy parsing of
// MSVC /sourceDependencies JSON data.
//
// MSVC /sourceDependencies documentation:
// https://learn.microsoft.com/en-us/cpp/build/reference/sourcedependencies

namespace cxx_modules::deps::msvc {

namespace source_dependencies {

struct ImportedModule
{
  name_view name;
  path_view bmi;
};

struct ImportedHeaderUnit
{
  path_view header;
  path_view bmi;
};

struct Data : util::NonCopyable
{
  path_view source;
  name_view provided_module;
  std::vector<path_view> includes;
  std::vector<ImportedModule> imported_modules;
  std::vector<ImportedHeaderUnit> imported_header_units;
};

} // namespace source_dependencies

struct SourceDependencies : util::NonCopyable
{
  std::string_view version;
  source_dependencies::Data data;

  SourceDependencies() noexcept = default;

  SourceDependencies(SourceDependencies const&) noexcept = delete;
  SourceDependencies(SourceDependencies&&) noexcept = default;

  SourceDependencies& operator=(SourceDependencies const&) noexcept = delete;
  SourceDependencies& operator=(SourceDependencies&&) noexcept = default;
};

} // namespace cxx_modules::deps::msvc

// NOTE: clang-format toggled off for some sections below because the current
// clang-format project settings do not handle them well. These toggles could be
// removed after bringing the settings more up-to-date.

// clang-format off
template<>
struct glz::meta<cxx_modules::deps::msvc::source_dependencies::ImportedModule>
{
  using T = cxx_modules::deps::msvc::source_dependencies::ImportedModule;
  static constexpr auto value = glz::object(
    "Name", &T::name,
    "BMI", &T::bmi
  );
};
// clang-format on

// clang-format off
template<>
struct glz::meta<cxx_modules::deps::msvc::source_dependencies::ImportedHeaderUnit>
{
  using T = cxx_modules::deps::msvc::source_dependencies::ImportedHeaderUnit;
  static constexpr auto value = glz::object(
    "Header", &T::header,
    "BMI", &T::bmi
  );
};
// clang-format on

// clang-format off
template<>
struct glz::meta<cxx_modules::deps::msvc::source_dependencies::Data>
{
  using T = cxx_modules::deps::msvc::source_dependencies::Data;
  static constexpr auto value = glz::object(
    "Source", &T::source,
    "ProvidedModule", &T::provided_module,
    "Includes", &T::includes,
    "ImportedModules", &T::imported_modules,
    "ImportedHeaderUnits", &T::imported_header_units
  );
};
// clang-format on

// clang-format off
template<>
struct glz::meta<cxx_modules::deps::msvc::SourceDependencies>
{
  using T = cxx_modules::deps::msvc::SourceDependencies;
  static constexpr auto value = glz::object(
    "Version", &T::version,
    "Data", &T::data
  );
};
// clang-format on

namespace cxx_modules::deps::msvc {

static_assert(TriviallyCopyable<source_dependencies::ImportedModule>);

static_assert(TriviallyCopyable<source_dependencies::ImportedHeaderUnit>);

static_assert(NonCopyable<source_dependencies::Data>);
static_assert(Movable<source_dependencies::Data>);

static_assert(NonCopyable<SourceDependencies>);
static_assert(Movable<SourceDependencies>);

} // namespace cxx_modules::deps::msvc
