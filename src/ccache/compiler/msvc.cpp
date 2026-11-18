// Copyright (C) 2022-2025 Joel Rosdahl and other contributors
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

#include "msvc.hpp"

#include <ccache/context.hpp>
#include <ccache/core/common.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>

namespace {
constexpr std::string_view k_cwd_placeholder = "%CCACHE_CWD%";
} // namespace

namespace compiler {

std::vector<std::string_view>
get_includes_from_msvc_show_includes(std::string_view file_content,
                                     std::string_view prefix)
{
  // /showIncludes output is written to stdout together with other messages.
  // Every line of it is "<prefix> <spaces> <file>" where the prefix is "Note:
  // including file:" in English but can be localized.

  std::vector<std::string_view> result;
  // This will split at each \r or \n, but that simply means there will be empty
  // "lines".
  for (std::string_view line : util::split_into_views(file_content, "\r\n")) {
    if (line.starts_with(prefix)) {
      size_t pos = prefix.size();
      while (pos < line.size() && util::is_space(line[pos])) {
        ++pos;
      }
      std::string_view include = line.substr(pos);
      if (!include.empty()) {
        result.push_back(include);
      }
    }
  }
  return result;
}

util::Bytes
strip_includes_from_msvc_show_includes(const Context& ctx,
                                       util::Bytes&& stdout_data)
{
  using util::Tokenizer;
  using Mode = Tokenizer::Mode;
  using IncludeDelimiter = Tokenizer::IncludeDelimiter;

  const bool strip_auto_includes =
    ctx.auto_depend_mode
    && (ctx.config.compiler_type() == CompilerType::msvc
        || ctx.config.compiler_type() == CompilerType::nvcc);

  if (stdout_data.empty() || !strip_auto_includes) {
    return std::move(stdout_data);
  }

  util::Bytes new_stdout_data;
  for (const auto line : Tokenizer(util::to_string_view(stdout_data),
                                   "\n",
                                   Mode::include_empty,
                                   IncludeDelimiter::yes)) {
    if (!line.starts_with(ctx.config.msvc_dep_prefix())) {
      new_stdout_data.insert(new_stdout_data.end(), line.data(), line.size());
    }
  }
  return new_stdout_data;
}

std::string
rewrite_paths_in_source_dependencies(const Context& ctx,
                                     std::string_view file_content)
{
  if (ctx.config.base_dirs().empty()) {
    return std::string(file_content);
  }

  std::string result;
  size_t pos = 0;

  while (true) {
    // Find a double-quote '"'.
    const size_t start = file_content.find('"', pos);
    if (start == std::string_view::npos) {
      break;
    }

    // Find the closing double-quote '"'.
    const size_t end = file_content.find('"', start + 1);
    if (end == std::string_view::npos) {
      break;
    }

    // Absolute path (or a key).
    result.append(file_content.substr(pos, start + 1 - pos));
    std::string value(file_content.substr(start + 1, end - start - 1));
    pos = end + 1;

    const util::filesystem::path path(util::replace_all(value, "\\\\", "\\"));
    const util::filesystem::path relative_path =
      core::make_relative_path(ctx, path);

    // Only an absolute path below base_dir comes back changed.
    if (relative_path != path) {
      const std::string placeholder_path =
        util::pstr(util::filesystem::path(k_cwd_placeholder) / relative_path);
      value = util::replace_all(placeholder_path, "\\", "\\\\");
    }

    result.append(value);
    result.append("\"");
  }

  result.append(file_content.substr(pos));
  return result;
}

std::string
restore_paths_in_source_dependencies(const Context& ctx,
                                     std::string_view file_content)
{
  const std::string cwd = util::pstr(ctx.actual_cwd);
  const std::string escaped_cwd = util::replace_all(cwd, "\\", "\\\\");
  return util::replace_all(file_content, k_cwd_placeholder, escaped_cwd);
}

} // namespace compiler
