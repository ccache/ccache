// Copyright (C) 2020-2023 Joel Rosdahl and other contributors
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

#include <Util.hpp>
#include <core/exceptions.hpp>
#include <util/Tokenizer.hpp>
#include <util/assertions.hpp>
#include <util/file.hpp>
#include <util/logging.hpp>
#include <util/path.hpp>
#include <util/string.hpp>

#include <algorithm>

static inline bool
is_blank(const std::string& s)
{
  return std::all_of(s.begin(), s.end(), [](char c) { return isspace(c); });
}

namespace Depfile {

std::string
escape_filename(std::string_view filename)
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

std::optional<std::string>
rewrite_source_paths(const Context& ctx, std::string_view file_content)
{
  ASSERT(!ctx.config.base_dir().empty());

  // Fast path for the common case:
  if (file_content.find(ctx.config.base_dir()) == std::string::npos) {
    return std::nullopt;
  }

  std::string adjusted_file_content;
  adjusted_file_content.reserve(file_content.size());

  bool content_rewritten = false;
  bool seen_target_token = false;

  using util::Tokenizer;
  for (const auto line : Tokenizer(file_content,
                                   "\n",
                                   Tokenizer::Mode::include_empty,
                                   Tokenizer::IncludeDelimiter::yes)) {
    const auto tokens = util::split_into_views(line, " \t");
    for (size_t i = 0; i < tokens.size(); ++i) {
      DEBUG_ASSERT(!line.empty()); // line.empty() -> no tokens
      DEBUG_ASSERT(!tokens[i].empty());

      if (i > 0 || line[0] == ' ' || line[0] == '\t') {
        adjusted_file_content.push_back(' ');
      }

      const auto& token = tokens[i];
      bool token_rewritten = false;
      if (seen_target_token && util::is_absolute_path(token)) {
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

      if (tokens[i].back() == ':') {
        seen_target_token = true;
      }
    }
  }

  if (content_rewritten) {
    return adjusted_file_content;
  } else {
    return std::nullopt;
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

  const std::string& output_dep = ctx.args_info.output_dep;
  const auto file_content = util::read_file<std::string>(output_dep);
  if (!file_content) {
    LOG("Failed to read dependency file {}: {}",
        output_dep,
        file_content.error());
    return;
  }
  const auto new_content = rewrite_source_paths(ctx, *file_content);
  if (new_content) {
    util::write_file(output_dep, *new_content);
  } else {
    LOG("No paths in dependency file {} made relative", output_dep);
  }
}

std::vector<std::string>
tokenize(std::string_view file_content)
{
  // A dependency file uses Makefile syntax. This is not perfect parser but
  // should be enough for parsing a regular dependency file.
  //
  // Note that this is pretty complex because of Windows paths that can be
  // identical to a target-colon-prerequisite without spaces (e.g. cat:/meow vs.
  // c:/meow).
  //
  // Here are tests on Windows on how GNU Make 4.3 handles different scenarios:
  //
  //   cat:/meow   -> sees "cat" and "/meow"
  //   cat:\meow   -> sees "cat" and "\meow"
  //   cat:\ meow  -> sees "cat" and " meow"
  //   cat:c:/meow -> sees "cat" and "c:/meow"
  //   cat:c:\meow -> sees "cat" and "c:\meow"
  //   cat:c:      -> target pattern contains no '%'.  Stop.
  //   cat:c:\     -> target pattern contains no '%'.  Stop.
  //   cat:c:/     -> sees "cat" and "c:/"
  //   cat:c:meow  -> target pattern contains no '%'.  Stop.
  //   c:c:/meow   -> sees "c" and "c:/meow"
  //   c:c:\meow   -> sees "c" and "c:\meow"
  //   c:z:\meow   -> sees "c" and "z:\meow"
  //   c:cd:\meow  -> target pattern contains no '%'.  Stop.
  //
  // Thus, if there is a colon and the previous token is one character long and
  // the following character is a slash (forward or backward), then it is
  // interpreted as a Windows path.

  std::vector<std::string> result;
  const size_t length = file_content.size();
  std::string token;
  size_t p = 0;

  while (p < length) {
    char c = file_content[p];

    if (c == ':' && p + 1 < length && !is_blank(token) && token.length() == 1) {
      const char next = file_content[p + 1];
      if (next == '/' || next == '\\') {
        // It's a Windows path, so the colon is not a separator and instead
        // added to the token.
        token.push_back(c);
        ++p;
        continue;
      }
    }

    // Each token is separated by whitespace or a colon.
    if (isspace(c) || c == ':') {
      // Chomp all spaces before next character.
      while (p < length && isspace(file_content[p])) {
        ++p;
      }
      if (!is_blank(token)) {
        // If there were spaces between a token and the colon, add the colon the
        // token to make sure it is seen as a target and not as a dependency.
        if (p < length) {
          const char next = file_content[p];
          if (next == ':') {
            token.push_back(next);
            ++p;
            // Chomp all spaces before next character.
            while (p < length && isspace(file_content[p])) {
              ++p;
            }
          }
        }
        result.push_back(token);
      }
      token.clear();
      continue;
    }

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
        // discard the backslash.
        case '\n':
          ++p;
          continue;
        }
      }
      break;
    case '$':
      if (p + 1 < length) {
        const char next = file_content[p + 1];
        if (next == '$') {
          // A dollar sign preceded by a dollar sign escapes the dollar sign.
          c = next;
          ++p;
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
