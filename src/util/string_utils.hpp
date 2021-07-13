// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#pragma once

#include <third_party/nonstd/expected.hpp>
#include <third_party/nonstd/optional.hpp>
#include <third_party/nonstd/string_view.hpp>

#include <sys/stat.h> // for mode_t

#include <cstring>
#include <string>
#include <utility>

namespace util {

// Return true if `suffix` is a suffix of `string`.
inline bool
ends_with(const nonstd::string_view string, const nonstd::string_view suffix)
{
  return string.ends_with(suffix);
}

// Parse `value` (an octal integer).
nonstd::expected<mode_t, std::string> parse_umask(const std::string& value);

// Percent-decode[1] `string`.
//
// [1]: https://en.wikipedia.org/wiki/Percent-encoding
nonstd::expected<std::string, std::string>
percent_decode(nonstd::string_view string);

// Split `string` into two parts using `split_char` as the delimiter. The second
// part will be `nullopt` if there is no `split_char` in `string.`
std::pair<nonstd::string_view, nonstd::optional<nonstd::string_view>>
split_once(nonstd::string_view string, char split_char);

// Return true if `prefix` is a prefix of `string`.
inline bool
starts_with(const char* string, const nonstd::string_view prefix)
{
  // Optimized version of starts_with(string_view, string_view): avoid computing
  // the length of the string argument.
  return std::strncmp(string, prefix.data(), prefix.length()) == 0;
}

// Return true if `prefix` is a prefix of `string`.
inline bool
starts_with(const nonstd::string_view string, const nonstd::string_view prefix)
{
  return string.starts_with(prefix);
}

// Strip whitespace from left and right side of a string.
[[nodiscard]] std::string strip_whitespace(nonstd::string_view string);

} // namespace util
