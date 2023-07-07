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
format_base16(nonstd::span<const uint8_t> data)
{
  static const char digits[] = "0123456789abcdef";
  std::string result;
  result.reserve(2 * data.size());
  for (uint8_t b : data) {
    result += digits[b >> 4];
    result += digits[b & 0xF];
  }
  return result;
}

std::string
format_base32hex(nonstd::span<const uint8_t> data)
{
  static const char digits[] = "0123456789abcdefghijklmnopqrstuv";
  std::string result;
  result.reserve(data.size() * 8 / 5 + 1);
  uint8_t i = 0;
  uint16_t bits = 0;
  for (uint8_t b : data) {
    bits <<= 8;
    bits |= b;
    i += 8;
    while (i >= 5) {
      result += digits[(bits >> (i - 5)) & 0x1f];
      i -= 5;
    }
  }
  if (i > 0) {
    DEBUG_ASSERT(i < 5);
    result += digits[(bits << (5 - i)) & 0x1f];
  }
  return result;
}

std::string
format_digest(nonstd::span<const uint8_t> data)
{
  const size_t base16_bytes = 2;
  ASSERT(data.size() >= base16_bytes);
  return util::format_base16({data.data(), base16_bytes})
         + util::format_base32hex(
           {data.data() + base16_bytes, data.size() - base16_bytes});
}

std::string
format_human_readable_diff(int64_t diff, SizeUnitPrefixType prefix_type)
{
  const char* sign = diff == 0 ? "" : (diff > 0 ? "+" : "-");
  return FMT(
    "{}{}", sign, format_human_readable_size(std::abs(diff), prefix_type));
}

std::string
format_human_readable_size(uint64_t size, SizeUnitPrefixType prefix_type)
{
  const double factor = prefix_type == SizeUnitPrefixType::binary ? 1024 : 1000;
  const char* infix = prefix_type == SizeUnitPrefixType::binary ? "i" : "";
  if (size >= factor * factor * factor) {
    return FMT("{:.1f} G{}B", size / (factor * factor * factor), infix);
  } else if (size >= factor * factor) {
    return FMT("{:.1f} M{}B", size / (factor * factor), infix);
  } else if (size >= factor) {
    const char* k = prefix_type == SizeUnitPrefixType::binary ? "K" : "k";
    return FMT("{:.1f} {}{}B", size / factor, k, infix);
  } else if (size == 1) {
    return "1 byte";
  } else {
    return FMT("{} bytes", size);
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

nonstd::expected<std::pair<uint64_t, SizeUnitPrefixType>, std::string>
parse_size(const std::string& value)
{
  errno = 0;

  char* p;
  double result = strtod(value.c_str(), &p);
  if (errno != 0 || result < 0 || p == value.c_str() || value.empty()) {
    return nonstd::make_unexpected(FMT("invalid size: \"{}\"", value));
  }

  while (isspace(*p)) {
    ++p;
  }

  SizeUnitPrefixType prefix_type;
  if (*p != '\0') {
    prefix_type = *(p + 1) == 'i' ? SizeUnitPrefixType::binary
                                  : SizeUnitPrefixType::decimal;
    unsigned multiplier =
      prefix_type == SizeUnitPrefixType::binary ? 1024 : 1000;
    switch (*p) {
    case 'T':
      result *= multiplier;
      [[fallthrough]];
    case 'G':
      result *= multiplier;
      [[fallthrough]];
    case 'M':
      result *= multiplier;
      [[fallthrough]];
    case 'K':
    case 'k':
      result *= multiplier;
      break;
    default:
      return nonstd::make_unexpected(FMT("invalid size: \"{}\"", value));
    }
  } else {
    result *= 1024 * 1024 * 1024;
    prefix_type = SizeUnitPrefixType::binary;
  }

  return std::make_pair(static_cast<uint64_t>(result), prefix_type);
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
split_once(const char* string, const char split_char)
{
  return split_once(std::string_view(string), split_char);
}

std::pair<std::string, std::optional<std::string>>
split_once(std::string&& string, const char split_char)
{
  const auto [left, right] = split_once(std::string_view(string), split_char);
  if (right) {
    return std::make_pair(std::string(left), std::string(*right));
  } else {
    return std::make_pair(std::string(left), std::nullopt);
  }
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
