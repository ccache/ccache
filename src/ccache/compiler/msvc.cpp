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
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>

namespace fs = std::filesystem;

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

std::optional<MsvcPathMap>
parse_msvc_path_map(std::string_view value)
{
  const auto eq_pos = value.find('=');
  if (eq_pos == std::string_view::npos || eq_pos == 0) {
    return std::nullopt;
  }
  const auto old_path = value.substr(0, eq_pos);
  if (old_path.find('/') != std::string_view::npos
      || !old_path.ends_with('\\')) {
    return std::nullopt;
  }
  return MsvcPathMap{fs::path(old_path), std::string(value.substr(eq_pos + 1))};
}

std::optional<std::string>
apply_msvc_path_maps(const fs::path& path, const std::vector<MsvcPathMap>& maps)
{
  for (const auto& [old_path, new_path] : maps) {
    if (!util::path_starts_with(path, old_path)) {
      continue;
    }
    auto it = path.begin();
    for (const auto& component : old_path) {
      if (!component.empty()) { // An empty component stems from the trailing \.
        ++it;
      }
    }
    fs::path rest;
    for (; it != path.end(); ++it) {
      rest /= *it;
    }
    return new_path + util::pstr(rest).str();
  }
  return std::nullopt;
}

} // namespace compiler
