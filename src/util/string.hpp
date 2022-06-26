// Copyright (C) 2021-2022 Joel Rosdahl and other contributors
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

#include <sys/stat.h> // for mode_t

#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace util {

// --- Interface ---

// Return true if `suffix` is a suffix of `string`.
bool ends_with(std::string_view string, std::string_view suffix);

// Join stringified elements of `container` delimited by `delimiter` into a
// string. There must exist an `std::string to_string(T::value_type)` function.
template<typename T>
std::string join(const T& container, const std::string_view delimiter);

// Join stringified elements between input iterators `begin` and `end` delimited
// by `delimiter` into a string. There must exist an `std::string
// to_string(T::value_type)` function.
template<typename T>
std::string
join(const T& begin, const T& end, const std::string_view delimiter);

// Parse a string into a double.
//
// Returns an error string if `value` cannot be parsed as a double.
nonstd::expected<double, std::string> parse_double(const std::string& value);

// Parse a string into a signed integer.
//
// Returns an error string if `value` cannot be parsed as an int64_t or if the
// value falls out of the range [`min_value`, `max_value`]. `min_value` and
// `max_value` default to min and max values of int64_t. `description` is
// included in the error message for range violations.
nonstd::expected<int64_t, std::string>
parse_signed(const std::string& value,
             std::optional<int64_t> min_value = std::nullopt,
             std::optional<int64_t> max_value = std::nullopt,
             std::string_view description = "integer");

// Parse `value` (an octal integer).
nonstd::expected<mode_t, std::string> parse_umask(const std::string& value);

// Parse a string into an unsigned integer.
//
// Returns an error string if `value` cannot be parsed as an uint64_t with base
// `base`, or if the value falls out of the range [`min_value`, `max_value`].
// `min_value` and `max_value` default to min and max values of uint64_t.
// `description` is included in the error message for range violations.
nonstd::expected<uint64_t, std::string>
parse_unsigned(const std::string& value,
               std::optional<uint64_t> min_value = std::nullopt,
               std::optional<uint64_t> max_value = std::nullopt,
               std::string_view description = "integer",
               int base = 10);

// Percent-decode[1] `string`.
//
// [1]: https://en.wikipedia.org/wiki/Percent-encoding
nonstd::expected<std::string, std::string>
percent_decode(std::string_view string);

// Replace the all occurrences of `from` to `to` in `string`.
std::string replace_all(std::string_view string,
                        std::string_view from,
                        std::string_view to);

// Replace the first occurrence of `from` to `to` in `string`.
std::string replace_first(std::string_view string,
                          std::string_view from,
                          std::string_view to);

// Split `string` into two parts using `split_char` as the delimiter. The second
// part will be `nullopt` if there is no `split_char` in `string.`
std::pair<std::string_view, std::optional<std::string_view>>
split_once(std::string_view string, char split_char);

// Return true if `prefix` is a prefix of `string`.
bool starts_with(const char* string, std::string_view prefix);

// Return true if `prefix` is a prefix of `string`.
bool starts_with(std::string_view string, std::string_view prefix);

// Strip whitespace from left and right side of a string.
[[nodiscard]] std::string strip_whitespace(std::string_view string);

// Convert `value` to a string. This function is used when joining
// `std::string`s with `util::join`.
template<typename T> std::string to_string(const T& value);

// --- Inline implementations ---

inline bool
ends_with(const std::string_view string, const std::string_view suffix)
{
  return string.length() >= suffix.length()
         && string.substr(string.length() - suffix.length()) == suffix;
}

template<typename T>
inline std::string
join(const T& container, const std::string_view delimiter)
{
  return join(container.begin(), container.end(), delimiter);
}

template<typename T>
inline std::string
join(const T& begin, const T& end, const std::string_view delimiter)
{
  std::string result;
  for (auto it = begin; it != end; ++it) {
    if (it != begin) {
      result.append(delimiter.data(), delimiter.length());
    }
    result += to_string(*it);
  }
  return result;
}

inline bool
starts_with(const char* const string, const std::string_view prefix)
{
  // Optimized version of starts_with(string_view, string_view): avoid computing
  // the length of the string argument.
  return std::strncmp(string, prefix.data(), prefix.length()) == 0;
}

inline bool
starts_with(const std::string_view string, const std::string_view prefix)
{
  return string.substr(0, prefix.size()) == prefix;
}

template<typename T>
inline std::string
to_string(const T& t)
{
  using std::to_string;
  return to_string(std::forward<T>(t));
}

template<>
inline std::string
to_string(const std::string& string)
{
  return std::string(string);
}

template<>
inline std::string
to_string(const std::string_view& sv)
{
  return std::string(sv);
}

} // namespace util
