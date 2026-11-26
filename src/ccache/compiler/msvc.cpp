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
#include <ccache/util/filesystem.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>

#include <algorithm>

namespace fs = util::filesystem;

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
  const auto [old_path, new_path] = util::split_once_into_views(value, '=');
  if (!new_path || !(old_path.ends_with('\\') || old_path.ends_with('/'))
      || std::ranges::any_of(old_path, [](char c) {
           return static_cast<unsigned char>(c) >= 0x80;
         })) {
    return std::nullopt;
  }
  return MsvcPathMap{std::string(old_path), std::string(*new_path)};
}

std::optional<std::string>
apply_msvc_path_map(const MsvcPathMap& map, std::string_view path)
{
  const auto dir = util::replace_all(path, "/", "\\") + '\\';
  const auto old_path = util::replace_all(map.old_path, "/", "\\");
  if (!util::to_lowercase(dir).starts_with(util::to_lowercase(old_path))) {
    return std::nullopt;
  }
  auto rest = dir.substr(old_path.size());
  if (!rest.empty()) {
    rest.pop_back(); // The appended backslash.
  }
  return map.new_path + rest;
}

std::optional<MsvcPathMap>
select_msvc_root_map(const std::vector<std::string>& maps,
                     const std::vector<fs::path>& base_dirs)
{
  if (base_dirs.size() != 1 || maps.size() != 2) {
    return std::nullopt;
  }
  auto root = util::replace_all(util::pstr(base_dirs.front()).str(), "/", "\\");
  while (root.ends_with('\\')) {
    root.pop_back();
  }
  root += '\\';
  const auto forward_root = util::replace_all(root, "\\", "/");

  const auto first = parse_msvc_path_map(maps[0]);
  const auto second = parse_msvc_path_map(maps[1]);
  if (!first || !second || first->new_path != second->new_path) {
    return std::nullopt;
  }
  if (!(first->old_path == root && second->old_path == forward_root)
      && !(first->old_path == forward_root && second->old_path == root)) {
    return std::nullopt;
  }
  return MsvcPathMap{root, first->new_path};
}

fs::path
msvc_object_dir(const fs::path& cwd, const fs::path& output_obj)
{
  return (cwd / output_obj).parent_path().lexically_normal();
}

bool
msvc_output_names_root(std::string_view root, std::string_view output)
{
  auto name = util::to_lowercase(root);
  if (name.ends_with('\\')) {
    name.pop_back();
  }
  return util::replace_all(util::to_lowercase(output), "/", "\\").find(name)
         != std::string::npos;
}

} // namespace compiler
