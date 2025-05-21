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

#include <optional>

// This header declares an internal API for zero-copy parsing of
// p1689 (currently r5) dynamic dependency information JSON files.
//
// The glaze JSON parsing library:
// https://github.com/stephenberry/glaze
//
// The p1689 specification:
// https://www.open-std.org/jtc1/sc22/wg21/docs/papers/2022/p1689r5.html

namespace cxx_modules::deps::p1689 {

enum class LookupMethod : std::uint8_t { ByName, IncludeAngle, IncludeQuote };

struct ProvidedModuleDesc
{
  std::optional<path_view> source_path;
  std::optional<path_view> compiled_module_path;
  std::optional<bool> unique_on_source_path = false;
  name_view logical_name;
  std::optional<bool> is_interface = true;
};

struct RequiredModuleDesc
{
  std::optional<path_view> source_path;
  std::optional<path_view> compiled_module_path;
  std::optional<bool> unique_on_source_path = false;
  name_view logical_name;
  std::optional<LookupMethod> lookup_method = LookupMethod::ByName;

  [[nodiscard]] auto
  is_system() const noexcept -> bool
  {
    return lookup_method == LookupMethod::IncludeAngle;
  }
};

struct DepInfo
{
  std::optional<path_view> work_directory;
  std::optional<path_view> primary_output;
  std::optional<std::vector<path_view>> outputs;
  std::optional<std::vector<ProvidedModuleDesc>> provides;
  std::optional<std::vector<RequiredModuleDesc>> requires_;

  DepInfo() noexcept = default;

  DepInfo(DepInfo const&) noexcept = delete;
  DepInfo(DepInfo&&) noexcept = default;

  DepInfo& operator=(DepInfo const&) noexcept = delete;
  DepInfo& operator=(DepInfo&&) noexcept = default;
};

struct DepFile : util::NonCopyable
{
  static constexpr std::string_view kind = "p1689";

  std::uint32_t version;
  std::optional<std::uint32_t> revision = 0;
  std::vector<DepInfo> rules;

  DepFile() noexcept = default;

  DepFile(DepFile const&) noexcept = delete;
  DepFile(DepFile&&) noexcept = default;

  DepFile& operator=(DepFile const&) noexcept = delete;
  DepFile& operator=(DepFile&&) noexcept = default;
};

} // namespace cxx_modules::deps::p1689

// NOTE: clang-format toggled off for some sections below because the current
// clang-format project settings do not handle them well. These toggles could be
// removed after bringing the settings more up-to-date.

// clang-format off
template<>
struct glz::meta<cxx_modules::deps::p1689::LookupMethod>
{
  using enum cxx_modules::deps::p1689::LookupMethod;
  static constexpr auto value = glz::enumerate(
    "by-name", ByName,
    "include-angle", IncludeAngle,
    "include-quote", IncludeQuote
  );
};
// clang-format on

// clang-format off
template<>
struct glz::meta<cxx_modules::deps::p1689::ProvidedModuleDesc>
{
  using T = cxx_modules::deps::p1689::ProvidedModuleDesc;
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
struct glz::meta<cxx_modules::deps::p1689::RequiredModuleDesc>
{
  using T = cxx_modules::deps::p1689::RequiredModuleDesc;
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
struct glz::meta<cxx_modules::deps::p1689::DepInfo>
{
  using T = cxx_modules::deps::p1689::DepInfo;
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
struct glz::meta<cxx_modules::deps::p1689::DepFile>
{
  using T = cxx_modules::deps::p1689::DepFile;
  static constexpr auto value = glz::object(
    "version", &T::version,
    "revision", &T::revision,
    "rules", &T::rules
  );
};
// clang-format on

namespace cxx_modules::deps::p1689 {

static_assert(TriviallyCopyable<LookupMethod>);

static_assert(TriviallyCopyable<ProvidedModuleDesc>);

static_assert(TriviallyCopyable<RequiredModuleDesc>);

static_assert(NonCopyable<DepInfo>);
static_assert(Movable<DepInfo>);

static_assert(NonCopyable<DepFile>);
static_assert(Movable<DepFile>);

} // namespace cxx_modules::deps::p1689
