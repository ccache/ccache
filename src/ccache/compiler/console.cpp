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

#include <ccache/compiler/console.hpp>
#include <ccache/context.hpp>
#include <ccache/core/common.hpp>
#include <ccache/util/assertions.hpp>
#include <ccache/util/conversion.hpp>
#include <ccache/util/tokenizer.hpp>

namespace ccache::compiler {

Console::Stdout::Stdout(std::string_view stdout_text) noexcept
  : m_original(stdout_text)
{
}

[[nodiscard]] auto
Console::Stdout::original() const noexcept -> std::string_view
{
  return m_original;
}

[[nodiscard]] auto
Console::Stdout::filtered() const noexcept -> std::string_view
{
  if (m_filtered.has_value()) {
    return *m_filtered;
  } else {
    return original();
  }
}

[[nodiscard]] auto
Console::Stdout::filtered() noexcept -> std::string&
{
  // Initialize `m_filtered_text` on write.
  if (!m_filtered.has_value()) {
    m_filtered.emplace(std::string{});
  }
  return *m_filtered;
}

Console::Console(std::string_view stdout_text,
                 std::string_view stderr_text) noexcept
  : m_stdout(stdout_text),
    m_stderr(stderr_text)
{
}

[[nodiscard]] auto
Console::stdout_text() const noexcept -> const Console::Stdout&
{
  return m_stdout;
}

[[nodiscard]] auto
Console::stdout_text() noexcept -> Console::Stdout&
{
  return m_stdout;
}

[[nodiscard]] auto
Console::stderr_text() const noexcept -> std::string_view
{
  return m_stderr;
}

[[nodiscard]] auto
Console::msvc() const noexcept -> const MsvcConsole&
{
  return m_msvc;
}

[[nodiscard]] auto
Console::msvc() noexcept -> MsvcConsole&
{
  return m_msvc;
}

namespace {
[[nodiscard]] auto
line_view(const std::variant<std::string_view, std::string>& text) noexcept
  -> std::string_view
{
  if (const auto* pval = std::get_if<std::string_view>(&text)) {
    return *pval;
  }
  if (const auto* pval = std::get_if<std::string>(&text)) {
    return *pval;
  }
  ASSERT(false);
}
} // namespace

// Process console output from compiler.
//
// This processing encompasses parsing /showIncludes and /sourceDependencies for
// MSVC and potentially rewriting absolute paths into relative paths if the
// `base_dir` option is set in the configuration.
[[nodiscard]] auto
Console::process(const Context& ctx,
                 std::string_view stdout_text,
                 std::string_view stderr_text) -> Console
{
  using util::Tokenizer;

  auto console = Console(stdout_text, stderr_text);

  const auto text = console.stdout_text().original();

  auto lines = Tokenizer(text,
                         "\n",
                         Tokenizer::Mode::include_empty,
                         Tokenizer::IncludeDelimiter::yes);

  for (auto it = lines.begin(); it != lines.end(); ++it) {
    const auto next = *it;

    std::variant<std::string_view, std::string> edit = next;

    // Rewrite absolute paths if `base_dir` is configured.
    if (!ctx.config.base_dir().empty()) {
      const auto line = line_view(edit);
      if (ctx.config.compiler() == Compiler::type::msvc) {
        // Ninja uses the lines with 'Note: including file: ' to determine the
        // used headers. Headers within basedir need to be changed into relative
        // paths because otherwise Ninja will use the abs path to original
        // header to check if a file needs to be recompiled.
        if (util::starts_with(line, ctx.config.msvc_dep_prefix())) {
          std::string orig(line);
          std::string abs;
          abs = util::replace_first(orig, ctx.config.msvc_dep_prefix(), "");
          abs = util::strip_whitespace(abs);
          fs::path rel(core::make_relative_path(ctx, abs));
          edit = util::replace_first(orig, abs, util::pstr(rel).str());
        }
        // The MSVC /FC option causes paths in diagnostics messages to become
        // absolute. Those within basedir need to be changed into relative
        // paths.
        else {
          size_t len = core::get_diagnostics_path_length(line);
          if (len != 0) {
            std::string_view abs = line.substr(0, len);
            fs::path rel = core::make_relative_path(ctx, abs);
            edit = util::replace_all(line, abs, util::pstr(rel).str());
          }
        }
      }
    }

    auto line = line_view(edit);

    if (util::starts_with(line, "__________")) {
      core::send_to_console(ctx, line, STDOUT_FILENO);
    }

    // TODO: move logic to separate function that returns an optional line.
    if (ctx.config.compiler() == Compiler::type::msvc) {
      // TODO:
      // Add a new ctx option that lets us detect if /showIncludes is specified
      // manually and also check for that here.
      if (ctx.auto_depend_mode) {
        // Entry for /showIncludes
        if (util::starts_with(line, ctx.config.msvc_dep_prefix())) {
          // Find the start offset of the (indented) include entry.
          size_t offset = ctx.config.msvc_dep_prefix().size();
          while (offset < line.size() && isspace(line[offset])) {
            ++offset;
          }

          // Find the length of the include entry.
          size_t length = line.size() - 1;
          while (line[length] == '\r' || line[length] == '\n') {
            length -= 1;
          }

          // Get the include entry as a substring of the line.
          std::string_view include = line.substr(offset, length - offset + 1);
          if (!include.empty()) {
            console.msvc().show_includes().push_back(include);
          }

          continue;
        }
      }
    }

    console.stdout_text().filtered() += line;
  }

  return console;
}

} // namespace ccache::compiler
