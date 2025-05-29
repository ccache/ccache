// Copyright (C) 2023-2024 Joel Rosdahl and other contributors
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

#include <ccache/compiler/msvc.hpp>
#include <ccache/util/noncopyable.hpp>

#include <optional>
#include <string>
#include <string_view>

class Context;

namespace ccache::compiler {

class Execution;

// TODO: Move this into compiler result object
class Console : util::NonCopyable
{
  class Stdout : util::NonCopyable
  {
    std::string_view m_original;
    std::optional<std::string> m_filtered;

    Stdout() noexcept = default;
    Stdout(std::string_view stdout_text) noexcept;

    [[nodiscard]] auto filtered() noexcept -> std::string&;

    friend class Console;

  public:
    [[nodiscard]] auto original() const noexcept -> std::string_view;
    [[nodiscard]] auto filtered() const noexcept -> std::string_view;
  };

  Stdout m_stdout;
  std::string_view m_stderr;
  MsvcConsole m_msvc;

  Console() noexcept = default;

  [[nodiscard]] auto stdout_text() noexcept -> Stdout&;
  [[nodiscard]] auto msvc() noexcept -> MsvcConsole&;

  friend class Execution;

public:
  Console(std::string_view stdout_text, std::string_view stderr_text) noexcept;

  [[nodiscard]] static auto process(const Context& ctx,
                                    std::string_view stdout_text,
                                    std::string_view stderr_text = {})
    -> Console;

  [[nodiscard]] auto stdout_text() const noexcept -> const Stdout&;
  [[nodiscard]] auto stderr_text() const noexcept -> std::string_view;
  [[nodiscard]] auto msvc() const noexcept -> const MsvcConsole&;
};
} // namespace ccache::compiler
