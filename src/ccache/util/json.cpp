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

#include "json.hpp"

#include <ccache/util/format.hpp>
#include <ccache/util/string.hpp>

namespace {

struct ParseState
{
  std::string_view doc;
  size_t pos;
};

void
skip_whitespace(ParseState& state)
{
  while (state.pos < state.doc.size() && util::is_space(state.doc[state.pos])) {
    ++state.pos;
  }
}

tl::expected<std::string, std::string>
parse_string(ParseState& state)
{
  if (state.pos >= state.doc.size() || state.doc[state.pos] != '"') {
    return tl::unexpected("Expected string");
  }
  ++state.pos; // Skip opening '"'

  std::string result;
  while (state.pos < state.doc.size()) {
    char ch = state.doc[state.pos];

    if (ch == '"') {
      ++state.pos; // Skip closing '"'
      return result;
    }

    if (ch == '\\') {
      ++state.pos;
      if (state.pos >= state.doc.size()) {
        return tl::unexpected("Unexpected end of string");
      }

      char escaped = state.doc[state.pos];
      switch (escaped) {
      case '"':
      case '\\':
      case '/':
        result += escaped;
        break;
      case 'b':
        result += '\b';
        break;
      case 'f':
        result += '\f';
        break;
      case 'n':
        result += '\n';
        break;
      case 'r':
        result += '\r';
        break;
      case 't':
        result += '\t';
        break;
      case 'u':
        return tl::unexpected("\\uXXXX escape sequences are not supported");
      default:
        return tl::unexpected(FMT("Unknown escape sequence: \\{}", escaped));
      }
      ++state.pos;
    } else {
      result += ch;
      ++state.pos;
    }
  }

  return tl::unexpected("Unterminated string");
}

void
skip_primitive(ParseState& state)
{
  // Skip numbers, true, false, null
  while (state.pos < state.doc.size()) {
    char ch = state.doc[state.pos];
    if (util::is_space(ch) || ch == ',' || ch == '}' || ch == ']') {
      break;
    }
    ++state.pos;
  }
}

tl::expected<void, std::string>
skip_array(ParseState& state)
{
  if (state.pos >= state.doc.size() || state.doc[state.pos] != '[') {
    return tl::unexpected("Expected array");
  }
  ++state.pos; // Skip '['

  int depth = 1;
  while (state.pos < state.doc.size() && depth > 0) {
    char ch = state.doc[state.pos];
    if (ch == '"') {
      auto str_result = parse_string(state); // Parse and discard
      if (!str_result) {
        return tl::unexpected(str_result.error());
      }
    } else if (ch == '[') {
      ++depth;
      ++state.pos;
    } else if (ch == ']') {
      --depth;
      ++state.pos;
    } else {
      ++state.pos;
    }
  }

  if (depth != 0) {
    return tl::unexpected("Unterminated array");
  }
  return {};
}

tl::expected<void, std::string>
skip_object(ParseState& state)
{
  if (state.pos >= state.doc.size() || state.doc[state.pos] != '{') {
    return tl::unexpected("Expected object");
  }
  ++state.pos; // Skip '{'

  int depth = 1;
  while (state.pos < state.doc.size() && depth > 0) {
    char ch = state.doc[state.pos];
    if (ch == '"') {
      auto str_result = parse_string(state); // Parse and discard
      if (!str_result) {
        return tl::unexpected(str_result.error());
      }
    } else if (ch == '{') {
      ++depth;
      ++state.pos;
    } else if (ch == '}') {
      --depth;
      ++state.pos;
    } else {
      ++state.pos;
    }
  }

  if (depth != 0) {
    return tl::unexpected("Unterminated object");
  }
  return {};
}

tl::expected<void, std::string>
skip_value(ParseState& state)
{
  if (state.pos >= state.doc.size()) {
    return tl::unexpected("Unexpected end of document");
  }

  char ch = state.doc[state.pos];

  if (ch == '"') {
    auto str_result = parse_string(state); // Parse and discard
    if (!str_result) {
      return tl::unexpected(str_result.error());
    }
  } else if (ch == '{') {
    auto obj_result = skip_object(state);
    if (!obj_result) {
      return tl::unexpected(obj_result.error());
    }
  } else if (ch == '[') {
    auto arr_result = skip_array(state);
    if (!arr_result) {
      return tl::unexpected(arr_result.error());
    }
  } else if (ch == 't' || ch == 'f' || ch == 'n' || ch == '-'
             || util::is_digit(ch)) {
    skip_primitive(state);
  } else {
    return tl::unexpected(FMT("Unexpected character: '{}'", ch));
  }
  return {};
}

tl::expected<void, std::string>
navigate_to_key(ParseState& state, std::string_view key)
{
  if (state.pos >= state.doc.size() || state.doc[state.pos] != '{') {
    return tl::unexpected("Expected object");
  }
  ++state.pos; // Skip '{'

  while (true) {
    skip_whitespace(state);

    if (state.pos >= state.doc.size()) {
      return tl::unexpected(FMT("Key '{}' not found", key));
    }

    if (state.doc[state.pos] == '}') {
      return tl::unexpected(FMT("Key '{}' not found", key));
    }

    if (state.doc[state.pos] != '"') {
      return tl::unexpected("Expected string key");
    }
    auto current_key_result = parse_string(state);
    if (!current_key_result) {
      return tl::unexpected(current_key_result.error());
    }

    skip_whitespace(state);
    if (state.pos >= state.doc.size() || state.doc[state.pos] != ':') {
      return tl::unexpected("Expected ':' after key");
    }
    ++state.pos; // Skip ':'

    skip_whitespace(state);

    if (*current_key_result == key) {
      return {}; // Found the key, state.pos is now at the value
    }

    auto skip_result = skip_value(state);
    if (!skip_result) {
      return tl::unexpected(skip_result.error());
    }

    skip_whitespace(state);
    if (state.pos < state.doc.size() && state.doc[state.pos] == ',') {
      ++state.pos; // Skip comma
    }
  }
}

tl::expected<std::vector<std::string>, std::string>
parse_string_array(ParseState& state)
{
  if (state.pos >= state.doc.size() || state.doc[state.pos] != '[') {
    return tl::unexpected("Expected array");
  }
  ++state.pos; // Skip '['

  std::vector<std::string> result;

  while (true) {
    skip_whitespace(state);

    if (state.pos >= state.doc.size()) {
      return tl::unexpected("Unterminated array");
    }

    if (state.doc[state.pos] == ']') {
      ++state.pos; // Skip ']'
      return result;
    }

    if (state.doc[state.pos] != '"') {
      return tl::unexpected("Expected string in array");
    }

    auto str_result = parse_string(state);
    if (!str_result) {
      return tl::unexpected(str_result.error());
    }
    result.push_back(*str_result);

    skip_whitespace(state);

    if (state.pos >= state.doc.size()) {
      return tl::unexpected("Unterminated array");
    }

    if (state.doc[state.pos] == ',') {
      ++state.pos; // Skip comma
    } else if (state.doc[state.pos] != ']') {
      return tl::unexpected("Expected ',' or ']' in array");
    }
  }
}

} // namespace

namespace util {

SimpleJsonParser::SimpleJsonParser(std::string_view document)
  : m_document(document)
{
}

tl::expected<std::vector<std::string>, std::string>
SimpleJsonParser::get_string_array(std::string_view filter) const
{
  if (filter.empty() || filter[0] != '.') {
    return tl::unexpected("Invalid filter: must start with '.'");
  }

  // Parse filter path, e.g. ".Data.Includes" -> ["Data", "Includes"].
  auto path = split_into_views(filter.substr(1), ".");
  if (path.empty()) {
    return tl::unexpected("Empty filter path");
  }

  ParseState state{m_document, 0};
  skip_whitespace(state);

  if (state.pos >= state.doc.size() || state.doc[state.pos] != '{') {
    return tl::unexpected("Expected object at root");
  }

  // Navigate through nested objects.
  for (size_t i = 0; i < path.size() - 1; ++i) {
    auto nav_result = navigate_to_key(state, path[i]);
    if (!nav_result) {
      return tl::unexpected(nav_result.error());
    }
    skip_whitespace(state);
    if (state.pos >= state.doc.size() || state.doc[state.pos] != '{') {
      return tl::unexpected(FMT("Expected object for key '{}'", path[i]));
    }
  }

  // Navigate to the final key which should contain an array.
  auto nav_result = navigate_to_key(state, path.back());
  if (!nav_result) {
    return tl::unexpected(nav_result.error());
  }
  skip_whitespace(state);

  if (state.pos >= state.doc.size() || state.doc[state.pos] != '[') {
    return tl::unexpected(FMT("Expected array for key '{}'", path.back()));
  }

  return parse_string_array(state);
}

} // namespace util
