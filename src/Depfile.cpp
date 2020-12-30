// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "Depfile.hpp"

#include "Context.hpp"
#include "Hash.hpp"
#include "core/Logging.hpp"
#include "core/assertions.hpp"

static inline bool
is_blank(const std::string& s)
{
  return std::all_of(s.begin(), s.end(), [](char c) { return isspace(c); });
}

namespace Depfile {

std::string
escape_filename(nonstd::string_view filename)
{
  std::string result;
  result.reserve(filename.size());
  for (const char c : filename) {
    switch (c) {
    case '\\':
    case '#':
    case ':':
    case ' ':
    case '\t':
      result.push_back('\\');
      break;
    case '$':
      result.push_back('$');
      break;
    }
    result.push_back(c);
  }
  return result;
}

nonstd::optional<std::string>
rewrite_paths(const Context& ctx, const std::string& file_content)
{
  ASSERT(!ctx.config.base_dir().empty());
  ASSERT(ctx.has_absolute_include_headers);

  // Fast path for the common case:
  if (file_content.find(ctx.config.base_dir()) == std::string::npos) {
    return nonstd::nullopt;
  }

  std::string adjusted_file_content;
  adjusted_file_content.reserve(file_content.size());

  bool content_rewritten = false;
  for (const auto& line : Util::split_into_views(file_content, "\n")) {
    const auto tokens = Util::split_into_views(line, " \t");
    for (size_t i = 0; i < tokens.size(); ++i) {
      DEBUG_ASSERT(line.length() > 0); // line.empty() -> no tokens
      if (i > 0 || line[0] == ' ' || line[0] == '\t') {
        adjusted_file_content.push_back(' ');
      }

      const auto& token = tokens[i];
      bool token_rewritten = false;
      if (Util::is_absolute_path(token)) {
        const auto new_path = Util::make_relative_path(ctx, token);
        if (new_path != token) {
          adjusted_file_content.append(new_path);
          token_rewritten = true;
        }
      }
      if (token_rewritten) {
        content_rewritten = true;
      } else {
        adjusted_file_content.append(token.begin(), token.end());
      }
    }
    adjusted_file_content.push_back('\n');
  }

  if (content_rewritten) {
    return adjusted_file_content;
  } else {
    return nonstd::nullopt;
  }
}

// Replace absolute paths with relative paths in the provided dependency file.
void
make_paths_relative_in_output_dep(const Context& ctx)
{
  if (ctx.config.base_dir().empty()) {
    LOG_RAW("Base dir not set, skip using relative paths");
    return; // nothing to do
  }
  if (!ctx.has_absolute_include_headers) {
    LOG_RAW(
      "No absolute path for included files found, skip using relative paths");
    return; // nothing to do
  }

  const std::string& output_dep = ctx.args_info.output_dep;
  std::string file_content;
  try {
    file_content = Util::read_file(output_dep);
  } catch (const Error& e) {
    LOG("Cannot open dependency file {}: {}", output_dep, e.what());
    return;
  }
  const auto new_content = rewrite_paths(ctx, file_content);
  if (new_content) {
    Util::write_file(output_dep, *new_content);
  } else {
    LOG("No paths in dependency file {} made relative", output_dep);
  }
}

std::vector<std::string>
tokenize(nonstd::string_view file_content)
{
  // A dependency file uses Makefile syntax. This is not perfect parser but
  // should be enough for parsing a regular dependency file.

  std::vector<std::string> result;
  const size_t length = file_content.size();
  std::string token;
  size_t p = 0;

  while (p < length) {
    // Each token is separated by whitespace.
    if (isspace(file_content[p])) {
      while (p < length && isspace(file_content[p])) {
        ++p;
      }
      if (!is_blank(token)) {
        result.push_back(token);
      }
      token.clear();
      continue;
    }

    char c = file_content[p];
    switch (c) {
    case '\\':
      if (p + 1 < length) {
        const char next = file_content[p + 1];
        switch (next) {
        // A backspace followed by any of the below characters leaves the
        // character as is.
        case '\\':
        case '#':
        case ':':
        case ' ':
        case '\t':
          c = next;
          ++p;
          break;
        // Backslash followed by newline is interpreted like a space, so simply
        // the backslash.
        case '\n':
          ++p;
          continue;
        }
      }
      break;
    case '$':
      if (p + 1 < length) {
        const char next = file_content[p + 1];
        switch (next) {
        // A dollar sign preceded by a dollar sign escapes the dollar sign.
        case '$':
          c = next;
          ++p;
          break;
        }
      }
      break;
    }

    token.push_back(c);
    ++p;
  }

  if (!is_blank(token)) {
    result.push_back(token);
  }

  return result;
}

} // namespace Depfile
