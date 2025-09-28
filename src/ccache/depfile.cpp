// Copyright (C) 2020-2025 Joel Rosdahl and other contributors
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

#include "depfile.hpp"

#include <ccache/context.hpp>
#include <ccache/core/common.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/hash.hpp>
#include <ccache/util/assertions.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/tokenizer.hpp>

#include <algorithm>

namespace fs = util::filesystem;

namespace depfile {

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
rewrite_source_paths(const Context& ctx, std::string_view content)
{
  ASSERT(!ctx.config.base_dirs().empty());

  bool rewritten = false;
  bool first = true;
  auto tokens = tokenize(content);
  for (auto& token : tokens) {
    if (first) {
      // Don't rewrite object file path.
      first = false;
      continue;
    }
    if (token.empty() || token == ":") {
      continue;
    }
    auto rel_path = core::make_relative_path(ctx, token);
    if (rel_path != token) {
      rewritten = true;
      token = util::pstr(rel_path);
    }
  }

  if (rewritten) {
    return untokenize(tokens);
  } else {
    return std::nullopt;
  }
}

// Replace absolute paths with relative paths in the provided dependency file.
tl::expected<void, std::string>
make_paths_relative_in_output_dep(const Context& ctx)
{
  if (ctx.config.base_dirs().empty()) {
    LOG_RAW("Base dir not set, skip using relative paths");
    return {}; // nothing to do
  }

  const auto& output_dep = ctx.args_info.output_dep;
  TRY_ASSIGN(auto content, util::read_file<std::string>(output_dep));
  const auto new_content = rewrite_source_paths(ctx, content);
  if (new_content) {
    TRY(util::write_file(output_dep, *new_content));
  } else {
    LOG("No paths in dependency file {} made relative", output_dep);
  }

  return {};
}

std::vector<std::string>
tokenize(std::string_view text)
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

  std::vector<std::string> tokens;
  const size_t length = text.size();

  size_t i = 0;

  while (true) {
    // Find start of next token.
    while (i < length && text[i] != '\n' && isspace(text[i])) {
      ++i;
    }

    // Detect end of entry.
    if (i == length || text[i] == '\n') {
      if (!tokens.empty() && !tokens.back().empty()) {
        tokens.emplace_back("");
      }
      if (i == length) {
        // Reached the end.
        break;
      }
      ++i;
      continue;
    }

    if (text[i] == ':') {
      tokens.emplace_back(":");
      ++i;
      continue;
    }

    if (text[i] == '\\' && i + 1 < length && text[i + 1] == '\n') {
      // Line continuation.
      i += 2;
      continue;
    }

    // Parse token.
    std::string token;
    while (i < length) {
      if (text[i] == ':' && token.length() == 1 && !isspace(token[0])
          && i + 1 < length && (text[i + 1] == '/' || text[i + 1] == '\\')) {
        // It's a Windows path, so the colon is not a separator and instead
        // added to the token.
        token += text[i];
        ++i;
        continue;
      }

      if (text[i] == ':' || isspace(text[i])
          || (text[i] == '\\' && i + 1 < length && text[i + 1] == '\n')) {
        // End of token.
        break;
      }

      if (i + 1 < length) {
        switch (text[i]) {
        case '\\':
          switch (text[i + 1]) {
          // A backspace followed by any of the below characters leaves the
          // character as is.
          case '\\':
          case '#':
          case ':':
          case ' ':
          case '\t':
            ++i;
            break;
          }
          break;
        case '$':
          if (text[i + 1] == '$') {
            // A dollar sign preceded by a dollar sign escapes the dollar sign.
            ++i;
          }
          break;
        }
      }

      token += text[i];
      ++i;
    }

    tokens.push_back(token);
  }

  return tokens;
}

std::string
untokenize(const std::vector<std::string>& tokens)
{
  std::string result;
  for (const auto& token : tokens) {
    if (token.empty()) {
      result += '\n';
    } else if (token == ":") {
      result += ':';
    } else {
      if (!result.empty() && result.back() != '\n') {
        result += " \\\n ";
      }
      result += escape_filename(token);
    }
  }
  if (!result.empty() && result.back() != '\n') {
    result += '\n';
  }
  return result;
}

} // namespace depfile
