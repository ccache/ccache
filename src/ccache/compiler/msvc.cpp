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
#include <ccache/util/assertions.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>

namespace fs = util::filesystem;

namespace {

// Only some of the strings in the JSON file are paths: the value of "Source",
// the elements of the "Includes" array, and the "Header" and "BMI" values in
// the "ImportedModules" and "ImportedHeaderUnits" arrays.
bool
key_holds_paths(std::string_view key)
{
  return key == "Source" || key == "Includes" || key == "Header"
         || key == "BMI";
}

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

std::optional<std::string>
rewrite_paths_in_source_dependencies(const Context& ctx,
                                     std::string_view file_content)
{
  ASSERT(!ctx.config.base_dirs().empty());

  // This is a scanner tuned to what MSVC writes rather than a JSON parser. The
  // schema is flat, the only escape MSVC produces is the doubled backslash of
  // a Windows path, and a string followed by a colon is a key.
  std::string result;
  result.reserve(file_content.size());
  std::string last_key;
  bool rewritten = false;
  size_t pos = 0;

  while (pos < file_content.size()) {
    const size_t start = file_content.find('"', pos);
    if (start == std::string_view::npos) {
      break;
    }
    size_t end = start + 1;
    while (end < file_content.size() && file_content[end] != '"') {
      end += (file_content[end] == '\\') ? 2 : 1;
    }
    if (end >= file_content.size()) {
      break;
    }

    result.append(file_content.substr(pos, start + 1 - pos));
    std::string value(file_content.substr(start + 1, end - start - 1));

    const size_t next = file_content.find_first_not_of(" \t\r\n", end + 1);
    if (next != std::string_view::npos && file_content[next] == ':') {
      last_key = value;
    } else if (key_holds_paths(last_key)) {
      // JSON doubles every backslash, so unescape before this is a real path.
      const fs::path path(util::replace_all(value, "\\\\", "\\"));
      if (path.is_absolute()) {
        const fs::path relative_path = core::make_relative_path(ctx, path);
        if (relative_path != path) {
          // Escape it again, so what is written is JSON as MSVC writes it.
          value =
            util::replace_all(util::pstr(relative_path).str(), "\\", "\\\\");
          rewritten = true;
        }
      }
    }

    result.append(value);
    result.append("\"");
    pos = end + 1;
  }

  if (!rewritten) {
    return std::nullopt;
  }
  result.append(file_content.substr(pos));
  return result;
}

// Replace absolute paths with relative paths in the file that
// /sourceDependencies produced.
tl::expected<void, std::string>
make_paths_relative_in_source_dependencies(const Context& ctx)
{
  if (ctx.config.base_dirs().empty()) {
    LOG("Base dir not set, skip using relative paths");
    return {}; // nothing to do
  }

  const auto& output_sd = ctx.args_info.output_sd;
  TRY_ASSIGN(auto content, util::read_file<std::string>(output_sd));
  const auto new_content = rewrite_paths_in_source_dependencies(ctx, content);
  if (new_content) {
    TRY(util::write_file(output_sd, *new_content));
  } else {
    LOG("No paths in source dependencies file {} made relative", output_sd);
  }

  return {};
}

} // namespace compiler
