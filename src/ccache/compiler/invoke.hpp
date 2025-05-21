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

#include <ccache/compiler/console.hpp>
#include <ccache/util/bytes.hpp>
#include <ccache/util/noncopyable.hpp>

#include <tl/expected.hpp>

class Args;
class Context;

namespace core {
enum class Statistic;
}

namespace ccache::compiler {

class Execution;

// A compiler/proprocessor invocation.
class Invocation : util::NonCopyable
{
  Context& m_ctx; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  Args& m_args;   // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  bool m_capture_stdout;

public:
  Invocation(Context& ctx, Args& args, bool capture_stdout = true) noexcept;

  // Execute the compiler/preprocessor, invocation with logic to retry without
  // requesting colored diagnostics messages if that fails.
  [[nodiscard]] auto execute() const
    -> tl::expected<Execution, core::Statistic>;
};

// An execution result of a compiler/preprocessor invocation.
class Execution : util::NonCopyable
{
  int m_exit_status;
  util::Bytes m_stdout_data;
  util::Bytes m_stderr_data;
  Console m_console;

  Execution() noexcept = default;

  friend auto Invocation::execute() const
    -> tl::expected<Execution, core::Statistic>;

public:
  [[nodiscard]] auto exit_status() const noexcept -> int;

  [[nodiscard]] auto stdout_data() const noexcept -> const util::Bytes&;

  [[nodiscard]] auto stderr_data() const noexcept -> const util::Bytes&;
  [[nodiscard]] auto stderr_data() noexcept -> util::Bytes&;

  [[nodiscard]] auto console() const noexcept -> const Console&;
};

} // namespace ccache::compiler
