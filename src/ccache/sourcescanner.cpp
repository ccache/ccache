// Copyright (C) 2025 Joel Rosdahl and other contributors
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

#include "sourcescanner.hpp"

#include <cctype>
#include <cstring>

namespace sourcescanner {

static const char*
skip_to_next_line(const char* p, const char* end)
{
  while (p < end && *p != '\n') {
    ++p;
  }
  return p < end ? p + 1 : p;
}

static const char*
skip_horizontal_whitespace(const char* p, const char* end)
{
  while (p < end && (*p == ' ' || *p == '\t')) {
    ++p;
  }
  return p;
}

static const char*
skip_line_continuation(const char* p, const char* end)
{
  if (p + 1 < end && *p == '\\' && (p[1] == '\n' || p[1] == '\r')) {
    p += 2;
    if (p < end && p[-1] == '\r' && *p == '\n') {
      ++p;
    }
  }
  return p;
}

static const char*
skip_whitespace_and_continuations(const char* p, const char* end)
{
  while (p < end) {
    const char* prev = p;
    p = skip_horizontal_whitespace(p, end);
    p = skip_line_continuation(p, end);
    if (p == prev) {
      break;
    }
  }
  return p;
}

std::vector<EmbedDirective>
scan_for_embed_directives(std::string_view source)
{
  std::vector<EmbedDirective> result;

  const char* p = source.data();
  const char* const end = p + source.size();

  while (p < end) {
    // Look for '#' at the start of a line
    if (*p != '#') {
      p = skip_to_next_line(p, end);
      continue;
    }

    ++p;
    p = skip_whitespace_and_continuations(p, end);

    // Check for "embed" keyword
    if (p + 5 > end || std::strncmp(p, "embed", 5) != 0) {
      p = skip_to_next_line(p, end);
      continue;
    }

    // Ensure "embed" is not part of a longer identifier (e.g. #embedded).
    // Required because we're doing text matching, not tokenization.
    const char* after_embed = p + 5;
    if (after_embed < end
        && (std::isalnum(static_cast<unsigned char>(*after_embed))
            || *after_embed == '_')) {
      p = skip_to_next_line(p, end);
      continue;
    }

    p = skip_whitespace_and_continuations(after_embed, end);
    if (p >= end) {
      break;
    }

    // C23 6.10.2: #embed has two forms like #include:
    //   #embed "q-char-sequence"  (quoted)
    //   #embed <h-char-sequence>  (system)
    // See: https://en.cppreference.com/w/c/preprocessor/embed
    char open_delim = *p;
    char close_delim;
    bool is_system;

    if (open_delim == '"') {
      close_delim = '"';
      is_system = false;
    } else if (open_delim == '<') {
      close_delim = '>';
      is_system = true;
    } else {
      p = skip_to_next_line(p, end);
      continue;
    }

    ++p;
    const char* path_start = p;

    // Extract path until closing delimiter or newline.
    while (p < end && *p != close_delim && *p != '\n') {
      ++p;
    }

    if (p < end && *p == close_delim) {
      std::string path(path_start, p - path_start);
      if (!path.empty()) {
        result.push_back({std::move(path), is_system});
      }
    }

    p = skip_to_next_line(p, end);
  }

  return result;
}

} // namespace sourcescanner
