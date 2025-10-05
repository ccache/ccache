// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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

#pragma once

#include <ccache/util/conversion.hpp>
#include <ccache/util/time.hpp>
#include <ccache/util/tokenizer.hpp>

#include <nonstd/span.hpp>
#include <tl/expected.hpp>

#include <sys/stat.h> // for mode_t

#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace util {

// --- Interface ---

enum class SizeUnitPrefixType { binary, decimal };
enum class TimeZone { local, utc };

// Return true if `suffix` is a suffix of `string`.
bool ends_with(std::string_view string, std::string_view suffix);

// Recreate a Windows command line string based on `argv`. If `prefix` is
// non-empty, add it as the first argument. If `escape_backslashes` is true,
// emit an additional backslash for each backslash that is not preceding '"' and
// is not at the end of `argv[i]` either.
std::string
format_argv_as_win32_command_string(const char* const* argv,
                                    bool escape_backslashes = false);

// Format `argv` as a simple string for logging purposes. That is, the result is
// not intended to be easily machine parsable. `argv` must be terminated by a
// nullptr.
std::string format_argv_for_logging(const char* const* argv);

// Format a hexadecimal string representing `data`. The returned string will be
// `2 * data.size()` long.
std::string format_base16(nonstd::span<const uint8_t> data);

// Format a lowercase base32hex string representing `data`. No padding
// characters will be added.
std::string format_base32hex(nonstd::span<const uint8_t> data);

// Format a hash digest representing `data`.
//
// The first two bytes are encoded as four lowercase base16 digits to maintain
// compatibility with the cleanup algorithm in older ccache versions and to
// allow for up to four uniform cache levels. The rest are encoded as lowercase
// base32hex digits without padding characters.
std::string format_digest(nonstd::span<const uint8_t> data);

// Format `diff` as a human-readable string.
std::string format_human_readable_diff(int64_t diff,
                                       SizeUnitPrefixType prefix_type);

// Format `size` as a human-readable string.
std::string format_human_readable_size(uint64_t size,
                                       SizeUnitPrefixType prefix_type);

// Format `time` as a human-readable ISO8601 timestamp string.
std::string format_iso8601_timestamp(const TimePoint& time,
                                     TimeZone time_zone = TimeZone::local);

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

// Join paths into a string with system-dependent delimiter.
std::string join_path_list(const std::vector<std::filesystem::path>& path_list);

// Parse a string into a double.
//
// Returns an error string if `value` cannot be parsed as a double.
tl::expected<double, std::string> parse_double(const std::string& value);

// Parse `duration`, an unsigned integer with d (days) or s (seconds) suffix,
// into seconds.
tl::expected<uint64_t, std::string> parse_duration(std::string_view duration);

// Parse a string into a signed integer.
//
// Returns an error string if `value` cannot be parsed as an int64_t or if the
// value falls out of the range [`min_value`, `max_value`]. `min_value` and
// `max_value` default to min and max values of int64_t. `description` is
// included in the error message for range violations.
tl::expected<int64_t, std::string>
parse_signed(std::string_view value,
             std::optional<int64_t> min_value = std::nullopt,
             std::optional<int64_t> max_value = std::nullopt,
             std::string_view description = "integer");

// Parse a "size value", i.e. a string that can end in k, M, G, T (10-based
// suffixes) or Ki, Mi, Gi, Ti (2-based suffixes). For backward compatibility, K
// is also recognized as a synonym of k.
tl::expected<std::pair<uint64_t, SizeUnitPrefixType>, std::string>
parse_size(const std::string& value);

// Parse `value` (an octal integer).
tl::expected<mode_t, std::string> parse_umask(std::string_view value);

// Parse a string into an unsigned integer.
//
// Returns an error string if `value` cannot be parsed as an uint64_t with base
// `base`, or if the value falls out of the range [`min_value`, `max_value`].
// `min_value` and `max_value` default to min and max values of uint64_t.
// `description` is included in the error message for range violations.
tl::expected<uint64_t, std::string>
parse_unsigned(std::string_view value,
               std::optional<uint64_t> min_value = std::nullopt,
               std::optional<uint64_t> max_value = std::nullopt,
               std::string_view description = "integer",
               int base = 10);

// Percent-decode[1] `string`.
//
// [1]: https://en.wikipedia.org/wiki/Percent-encoding
tl::expected<std::string, std::string> percent_decode(std::string_view string);

// Replace the all occurrences of `from` to `to` in `string`.
std::string replace_all(std::string_view string,
                        std::string_view from,
                        std::string_view to);

// Replace the first occurrence of `from` to `to` in `string`.
std::string replace_first(std::string_view string,
                          std::string_view from,
                          std::string_view to);

// Split `string` into tokens at any of the characters in `separators`.
// `separators` must neither be the empty string nor a nullptr.
std::vector<std::string>
split_into_strings(std::string_view string,
                   const char* separators,
                   Tokenizer::Mode mode = Tokenizer::Mode::skip_empty,
                   Tokenizer::IncludeDelimiter include_delimiter =
                     Tokenizer::IncludeDelimiter::no);

// Split `string` into tokens at any of the characters in `separators`. These
// tokens are views into `string`. `separators` must neither be the empty string
// nor a nullptr.
std::vector<std::string_view>
split_into_views(std::string_view string,
                 const char* separators,
                 Tokenizer::Mode mode = Tokenizer::Mode::skip_empty,
                 Tokenizer::IncludeDelimiter include_delimiter =
                   Tokenizer::IncludeDelimiter::no);

// Split `string` into two parts using `split_char` as the delimiter. The second
// part will be `nullopt` if there is no `split_char` in `string.`
std::pair<std::string, std::optional<std::string>>
split_once(std::string_view string, char split_char);

// Like `split_once` but splits into `std::string_view`.
std::pair<std::string_view, std::optional<std::string_view>>
split_once_into_views(std::string_view string, char split_char);

// Split `string` into two parts where the split point is before a potential
// absolute path. The second part will be `nullopt` if no absolute path
// candidate was found.
std::pair<std::string_view, std::optional<std::string_view>>
split_option_with_concat_path(std::string_view string);

// Split a list of paths (such as the content of $PATH on Unix platforms or
// %PATH% on Windows platforms) into paths.
std::vector<std::filesystem::path> split_path_list(std::string_view path_list);

// Return true if `prefix` is a prefix of `string`.
bool starts_with(const char* string, std::string_view prefix);

// Return true if `prefix` is a prefix of `string`.
bool starts_with(std::string_view string, std::string_view prefix);

// Strip whitespace from left and right side of a string.
[[nodiscard]] std::string strip_whitespace(std::string_view string);

// Convert a string to lowercase.
[[nodiscard]] std::string to_lowercase(std::string_view string);

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

} // namespace util
