// Copyright (C) 2021-2023 Joel Rosdahl and other contributors
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

#include <assertions.hpp>
#include <fmtmacros.hpp>

#include <algorithm>
#include <cctype>
#include <iostream>

namespace util {

std::string
format_human_readable_diff(int64_t diff)
{
  const char* sign = diff == 0 ? "" : (diff > 0 ? "+" : "-");
  return FMT("{}{}", sign, format_human_readable_size(std::abs(diff)));
}

std::string
format_human_readable_size(uint64_t size)
{
  if (size >= 1000 * 1000 * 1000) {
    return FMT("{:.1f} GB", size / ((double)(1000 * 1000 * 1000)));
  } else if (size >= 1000 * 1000) {
    return FMT("{:.1f} MB", size / ((double)(1000 * 1000)));
  } else if (size >= 1000) {
    return FMT("{:.1f} kB", size / 1000.0);
  } else if (size == 1) {
    return "1 byte";
  } else {
    return FMT("{} bytes", size);
  }
}

std::string
format_parsable_size_with_suffix(uint64_t size)
{
  if (size >= 1000 * 1000 * 1000) {
    return FMT("{:.1f}G", size / ((double)(1000 * 1000 * 1000)));
  } else if (size >= 1000 * 1000) {
    return FMT("{:.1f}M", size / ((double)(1000 * 1000)));
  } else {
    return FMT("{}", size);
  }
}

nonstd::expected<double, std::string>
parse_double(const std::string& value)
{
  size_t end;
  double result;
  bool failed = false;
  try {
    result = std::stod(value, &end);
  } catch (const std::exception&) {
    failed = true;
  }

  if (failed || end != value.size()) {
    return nonstd::make_unexpected(
      FMT("invalid floating point: \"{}\"", value));
  } else {
    return result;
  }
}

nonstd::expected<int64_t, std::string>
parse_signed(std::string_view value,
             const std::optional<int64_t> min_value,
             const std::optional<int64_t> max_value,
             const std::string_view description)
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
parse_umask(std::string_view value)
{
  return util::parse_unsigned(value, 0, 0777, "umask", 8);
}

nonstd::expected<uint64_t, std::string>
parse_unsigned(std::string_view value,
               const std::optional<uint64_t> min_value,
               const std::optional<uint64_t> max_value,
               const std::string_view description,
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
percent_decode(std::string_view string)
{
  const auto from_hex = [](const char digit) {
    return static_cast<uint8_t>(
      std::isdigit(digit) ? digit - '0' : std::tolower(digit) - 'a' + 10);
  };

  std::string result;
  result.reserve(string.size());
  size_t i = 0;
  while (i < string.size()) {
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
    ++i;
  }

  return result;
}

std::string
replace_all(const std::string_view string,
            const std::string_view from,
            const std::string_view to)
{
  if (from.empty()) {
    return std::string(string);
  }

  std::string result;
  size_t left = 0;
  size_t right = 0;
  while (left < string.size()) {
    right = string.find(from, left);
    if (right == std::string_view::npos) {
      result.append(string.data() + left);
      break;
    }
    result.append(string.data() + left, right - left);
    result.append(to.data(), to.size());
    left = right + from.size();
  }
  return result;
}

std::string
replace_first(const std::string_view string,
              const std::string_view from,
              const std::string_view to)
{
  if (from.empty()) {
    return std::string(string);
  }

  std::string result;
  const auto pos = string.find(from);
  if (pos != std::string_view::npos) {
    result.append(string.data(), pos);
    result.append(to.data(), to.length());
    result.append(string.data() + pos + from.size());
  } else {
    result = std::string(string);
  }
  return result;
}

std::pair<std::string_view, std::optional<std::string_view>>
split_once(const std::string_view string, const char split_char)
{
  const size_t sep_pos = string.find(split_char);
  if (sep_pos == std::string_view::npos) {
    return std::make_pair(string, std::nullopt);
  } else {
    return std::make_pair(string.substr(0, sep_pos),
                          string.substr(sep_pos + 1));
  }
}

std::string
strip_whitespace(const std::string_view string)
{
  const auto is_space = [](const int ch) { return std::isspace(ch); };
  const auto start = std::find_if_not(string.begin(), string.end(), is_space);
  const auto end =
    std::find_if_not(string.rbegin(), string.rend(), is_space).base();
  return start < end ? std::string(start, end) : std::string();
}

} // namespace util
