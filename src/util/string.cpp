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

#include "string.hpp"

#include <fmtmacros.hpp>

#include <cctype>

namespace util {

nonstd::expected<int64_t, std::string>
parse_signed(const std::string& value,
             const nonstd::optional<int64_t> min_value,
             const nonstd::optional<int64_t> max_value,
             const nonstd::string_view description)
{
  const std::string stripped_value = strip_whitespace(value);

  size_t end = 0;
  long long result = 0;
  bool failed = false;
  try {
    // Note: sizeof(long long) is guaranteed to be >= sizeof(int64_t)
    result = std::stoll(stripped_value, &end, 10);
  } catch (std::exception&) {
    failed = true;
  }
  if (failed || end != stripped_value.size()) {
    return nonstd::make_unexpected(
      FMT("invalid integer: \"{}\"", stripped_value));
  }

  const int64_t min = min_value ? *min_value : INT64_MIN;
  const int64_t max = max_value ? *max_value : INT64_MAX;
  if (result < min || result > max) {
    return nonstd::make_unexpected(
      FMT("{} must be between {} and {}", description, min, max));
  } else {
    return result;
  }
}

nonstd::expected<mode_t, std::string>
parse_umask(const std::string& value)
{
  return util::parse_unsigned(value, 0, 0777, "umask", 8);
}

nonstd::expected<uint64_t, std::string>
parse_unsigned(const std::string& value,
               const nonstd::optional<uint64_t> min_value,
               const nonstd::optional<uint64_t> max_value,
               const nonstd::string_view description,
               const int base)
{
  const std::string stripped_value = strip_whitespace(value);

  size_t end = 0;
  unsigned long long result = 0;
  bool failed = false;
  if (starts_with(stripped_value, "-")) {
    failed = true;
  } else {
    try {
      // Note: sizeof(unsigned long long) is guaranteed to be >=
      // sizeof(uint64_t)
      result = std::stoull(stripped_value, &end, base);
    } catch (std::exception&) {
      failed = true;
    }
  }
  if (failed || end != stripped_value.size()) {
    const auto base_info = base == 8 ? "octal " : "";
    return nonstd::make_unexpected(
      FMT("invalid unsigned {}integer: \"{}\"", base_info, stripped_value));
  }

  const uint64_t min = min_value ? *min_value : 0;
  const uint64_t max = max_value ? *max_value : UINT64_MAX;
  if (result < min || result > max) {
    return nonstd::make_unexpected(
      FMT("{} must be between {} and {}", description, min, max));
  } else {
    return result;
  }
}

nonstd::expected<std::string, std::string>
percent_decode(nonstd::string_view string)
{
  const auto from_hex = [](const char digit) {
    return static_cast<uint8_t>(
      std::isdigit(digit) ? digit - '0' : std::tolower(digit) - 'a' + 10);
  };

  std::string result;
  result.reserve(string.size());
  for (size_t i = 0; i < string.size(); ++i) {
    if (string[i] != '%') {
      result += string[i];
    } else if (i + 2 >= string.size() || !std::isxdigit(string[i + 1])
               || !std::isxdigit(string[i + 2])) {
      return nonstd::make_unexpected(
        FMT("invalid percent-encoded string at position {}: {}", i, string));
    } else {
      const char ch = static_cast<char>(from_hex(string[i + 1]) << 4
                                        | from_hex(string[i + 2]));
      result += ch;
      i += 2;
    }
  }

  return result;
}

std::pair<nonstd::string_view, nonstd::optional<nonstd::string_view>>
split_once(const nonstd::string_view string, const char split_char)
{
  const size_t sep_pos = string.find(split_char);
  if (sep_pos == nonstd::string_view::npos) {
    return std::make_pair(string, nonstd::nullopt);
  } else {
    return std::make_pair(string.substr(0, sep_pos),
                          string.substr(sep_pos + 1));
  }
}

std::string
strip_whitespace(const nonstd::string_view string)
{
  const auto is_space = [](const int ch) { return std::isspace(ch); };
  const auto start = std::find_if_not(string.begin(), string.end(), is_space);
  const auto end =
    std::find_if_not(string.rbegin(), string.rend(), is_space).base();
  return start < end ? std::string(start, end) : std::string();
}

} // namespace util
