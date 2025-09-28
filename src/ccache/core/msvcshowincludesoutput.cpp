// Copyright (C) 2022-2024 Joel Rosdahl and other contributors
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

#include "msvcshowincludesoutput.hpp"

#include <ccache/context.hpp>
#include <ccache/util/string.hpp>

namespace core::MsvcShowIncludesOutput {

std::vector<std::string_view>
get_includes(std::string_view file_content, std::string_view prefix)
{
  // /showIncludes output is written to stdout together with other messages.
  // Every line of it is "<prefix> <spaces> <file>" where the prefix is "Note:
  // including file:" in English but can be localized.

  std::vector<std::string_view> result;
  // This will split at each \r or \n, but that simply means there will be empty
  // "lines".
  for (std::string_view line : util::split_into_views(file_content, "\r\n")) {
    if (util::starts_with(line, prefix)) {
      size_t pos = prefix.size();
      while (pos < line.size() && isspace(line[pos])) {
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
strip_includes(const Context& ctx, util::Bytes&& stdout_data)
{
  using util::Tokenizer;
  using Mode = Tokenizer::Mode;
  using IncludeDelimiter = Tokenizer::IncludeDelimiter;

  if (stdout_data.empty() || !ctx.auto_depend_mode
      || ctx.config.compiler_type() != CompilerType::msvc) {
    return std::move(stdout_data);
  }

  util::Bytes new_stdout_data;
  for (const auto line : Tokenizer(util::to_string_view(stdout_data),
                                   "\n",
                                   Mode::include_empty,
                                   IncludeDelimiter::yes)) {
    if (!util::starts_with(line, ctx.config.msvc_dep_prefix())) {
      new_stdout_data.insert(new_stdout_data.end(), line.data(), line.size());
    }
  }
  return new_stdout_data;
}

} // namespace core::MsvcShowIncludesOutput
