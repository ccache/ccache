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

#include <tl/expected.hpp>

#include <glaze/glaze.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace cxx_modules::depfiles {
// Specialized string_view class representing logical module names
class name_view
{
private:
  std::string_view m_repr;
  bool m_dotted = false;

  friend glz::detail::from<glz::JSON, name_view>;

public:
  name_view() noexcept;
  name_view(std::string_view view) noexcept;

  operator std::string_view() const noexcept;

  [[nodiscard]] inline constexpr auto
  is_dotted() const noexcept -> bool
  {
    return m_dotted;
  }
};

static_assert(std::is_trivially_copyable_v<name_view>);

// Specialized string_view class representing module file paths
//
// NOTE: Eventually replace this with P1030 std::filesystem::path_view
// NOTE: Consider using llfio::path_view in the meantime
class path_view
{
private:
  std::string_view m_repr;

  friend glz::detail::from<glz::JSON, path_view>;

public:
  path_view() noexcept;
  path_view(std::string_view view) noexcept;

  operator std::string_view() const noexcept;
  explicit operator std::filesystem::path() const;
};

static_assert(std::is_trivially_copyable_v<path_view>);

enum class LookupMethod { ByName, IncludeAngle, IncludeQuote };

static_assert(std::is_trivially_copyable_v<LookupMethod>);

class ProvidedModuleDesc
{
public:
  std::optional<path_view> source_path;
  std::optional<path_view> compiled_module_path;
  std::optional<bool> unique_on_source_path = false;
  name_view logical_name;
  std::optional<bool> is_interface = true;
};

static_assert(std::is_trivially_copyable_v<ProvidedModuleDesc>);

class RequiredModuleDesc
{
public:
  std::optional<path_view> source_path;
  std::optional<path_view> compiled_module_path;
  std::optional<bool> unique_on_source_path = false;
  name_view logical_name;
  std::optional<LookupMethod> lookup_method = LookupMethod::ByName;
};

static_assert(std::is_trivially_copyable_v<RequiredModuleDesc>);

class DepInfo
{
public:
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

static_assert(!std::is_trivially_copyable_v<DepInfo>);

static_assert(!std::is_copy_assignable_v<DepInfo>);
static_assert(!std::is_copy_constructible_v<DepInfo>);

static_assert(std::is_move_assignable_v<DepInfo>);
static_assert(std::is_move_constructible_v<DepInfo>);

class DepFile
{
public:
  class ParseError
  {
    glz::error_ctx m_repr;

    ParseError(glz::error_ctx&&);

    friend DepFile;

  public:
    using code = glz::error_code;

    operator bool() const noexcept;
    auto operator==(code err) const noexcept -> bool;
    [[nodiscard]] auto format(std::string_view buffer) const -> std::string;
  };

  std::uint32_t version;
  std::optional<std::uint32_t> revision = 0;
  std::vector<DepInfo> rules;

  DepFile() noexcept = default;

  DepFile(DepFile const&) noexcept = delete;
  DepFile(DepFile&&) noexcept = default;

  DepFile& operator=(DepFile const&) noexcept = delete;
  DepFile& operator=(DepFile&&) noexcept = default;

  [[nodiscard]] static auto parse(std::string_view buffer) noexcept
    -> tl::expected<DepFile, ParseError>;
};

static_assert(!std::is_trivially_copyable_v<DepFile>);

static_assert(!std::is_copy_assignable_v<DepFile>);
static_assert(!std::is_copy_constructible_v<DepFile>);

static_assert(std::is_move_assignable_v<DepFile>);
static_assert(std::is_move_constructible_v<DepFile>);

} // namespace cxx_modules::depfiles
