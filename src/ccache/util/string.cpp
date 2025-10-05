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

#include "string.hpp"

#include <ccache/util/assertions.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/time.hpp>

#include <algorithm>
#include <cctype>

namespace fs = util::filesystem;

namespace {

#ifdef _WIN32
const char k_path_delimiter[] = ";";
#else
const char k_path_delimiter[] = ":";
#endif

template<typename T>
std::vector<T>
split_into(std::string_view string,
           const char* separators,
           util::Tokenizer::Mode mode,
           util::Tokenizer::IncludeDelimiter include_delimiter)

{
  std::vector<T> result;
  for (const auto token :
       util::Tokenizer(string, separators, mode, include_delimiter)) {
    result.emplace_back(token);
  }
  return result;
}

} // namespace

namespace util {

std::string
format_argv_as_win32_command_string(const char* const* argv,
                                    bool escape_backslashes)
{
  if (!argv || !argv[0]) {
    return {};
  }

  std::string result;
  if (getenv("_CCACHE_TEST") && argv[0] && util::ends_with(argv[0], ".sh")) {
    result += "sh.exe ";
  }

  for (size_t i = 0; argv[i]; ++i) {
    const char* arg = argv[i];
    int bs = 0;
    result += '"';
    for (size_t j = 0; arg[j]; ++j) {
      switch (arg[j]) {
      case '\\':
        if (!escape_backslashes) {
          ++bs;
          break;
        }
        [[fallthrough]];

      case '"':
        bs = (bs << 1) + 1;
        [[fallthrough]];

      default:
        while (bs > 0) {
          result += '\\';
          --bs;
        }
        result += arg[j];
      }
    }
    bs <<= 1;
    while (bs > 0) {
      result += '\\';
      --bs;
    }
    result += "\" ";
  }

  result.resize(result.length() - 1);
  return result;
}

std::string
format_argv_for_logging(const char* const* argv)
{
  std::string result;
  for (size_t i = 0; argv[i]; ++i) {
    if (i != 0) {
      result += ' ';
    }
    std::string arg = replace_all(argv[i], "\\", "\\\\");
    arg = replace_all(arg, "\"", "\\\"");
    if (arg.empty() || arg.find(' ') != std::string::npos) {
      arg = FMT("\"{}\"", arg);
    }
    result += arg;
  }
  return result;
}

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
  return format_base16({data.data(), base16_bytes})
         + format_base32hex(
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
  const double dsize = static_cast<double>(size);
  const char* infix = prefix_type == SizeUnitPrefixType::binary ? "i" : "";
  if (dsize >= factor * factor * factor) {
    return FMT("{:.1f} G{}B", dsize / (factor * factor * factor), infix);
  } else if (dsize >= factor * factor) {
    return FMT("{:.1f} M{}B", dsize / (factor * factor), infix);
  } else if (dsize >= factor) {
    const char* k = prefix_type == SizeUnitPrefixType::binary ? "K" : "k";
    return FMT("{:.1f} {}{}B", dsize / factor, k, infix);
  } else if (size == 1) {
    return "1 byte";
  } else {
    return FMT("{} bytes", size);
  }
}

std::string
format_iso8601_timestamp(const TimePoint& time, TimeZone time_zone)
{
  const auto tm =
    (time_zone == TimeZone::local ? util::localtime : util::gmtime)(time);
  if (tm) {
    char timestamp[100];
    (void)strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%S", &*tm);
    return timestamp;
  } else {
    return std::to_string(util::sec(time));
  }
}

std::string
join_path_list(const std::vector<std::filesystem::path>& path_list)
{
  return join(path_list, k_path_delimiter);
}

tl::expected<double, std::string>
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
    return tl::unexpected(FMT("invalid floating point: \"{}\"", value));
  } else {
    return result;
  }
}

tl::expected<uint64_t, std::string>
parse_duration(std::string_view duration)
{
  uint64_t factor = 0;
  char last_ch = duration.empty() ? '\0' : duration[duration.length() - 1];

  switch (last_ch) {
  case 'd':
    factor = 24 * 60 * 60;
    break;
  case 's':
    factor = 1;
    break;
  default:
    return tl::unexpected(FMT(
      "invalid suffix (supported: d (day) and s (second)): \"{}\"", duration));
  }

  auto value = parse_unsigned(duration.substr(0, duration.length() - 1));
  if (!value) {
    return value;
  }
  return factor * *value;
}

tl::expected<int64_t, std::string>
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
    return tl::unexpected(FMT("invalid integer: \"{}\"", stripped_value));
  }

  const int64_t min = min_value ? *min_value : INT64_MIN;
  const int64_t max = max_value ? *max_value : INT64_MAX;
  if (result < min || result > max) {
    return tl::unexpected(
      FMT("{} must be between {} and {}", description, min, max));
  } else {
    return result;
  }
}

tl::expected<std::pair<uint64_t, SizeUnitPrefixType>, std::string>
parse_size(const std::string& value)
{
  errno = 0;

  char* p;
  double result = strtod(value.c_str(), &p);
  if (errno != 0 || result < 0 || p == value.c_str() || value.empty()) {
    return tl::unexpected(FMT("invalid size: \"{}\"", value));
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
      return tl::unexpected(FMT("invalid size: \"{}\"", value));
    }
  } else {
    result *= 1024 * 1024 * 1024;
    prefix_type = SizeUnitPrefixType::binary;
  }

  return std::make_pair(static_cast<uint64_t>(result), prefix_type);
}

tl::expected<mode_t, std::string>
parse_umask(std::string_view value)
{
  TRY_ASSIGN(auto mode, parse_unsigned(value, 0, 0777, "umask", 8));
  return static_cast<mode_t>(mode);
}

tl::expected<uint64_t, std::string>
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
    return tl::unexpected(
      FMT("invalid unsigned {}integer: \"{}\"", base_info, stripped_value));
  }

  const uint64_t min = min_value ? *min_value : 0;
  const uint64_t max = max_value ? *max_value : UINT64_MAX;
  if (result < min || result > max) {
    return tl::unexpected(
      FMT("{} must be between {} and {}", description, min, max));
  } else {
    return result;
  }
}

tl::expected<std::string, std::string>
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
      return tl::unexpected(
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
  while (left < string.size()) {
    size_t right = string.find(from, left);
    if (right == std::string_view::npos) {
      result.append(string.data() + left, string.size() - left);
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
    result.append(string.substr(0, pos));
    result.append(to);
    result.append(string.substr(pos + from.size()));
  } else {
    result = std::string(string);
  }
  return result;
}

std::vector<std::string>
split_into_strings(std::string_view string,
                   const char* separators,
                   Tokenizer::Mode mode,
                   Tokenizer::IncludeDelimiter include_delimiter)
{
  return split_into<std::string>(string, separators, mode, include_delimiter);
}

std::vector<std::string_view>
split_into_views(std::string_view string,
                 const char* separators,
                 Tokenizer::Mode mode,
                 Tokenizer::IncludeDelimiter include_delimiter)
{
  return split_into<std::string_view>(
    string, separators, mode, include_delimiter);
}

std::pair<std::string, std::optional<std::string>>
split_once(std::string_view string, char split_char)
{
  auto [left, right] = split_once_into_views(string, split_char);
  return std::pair<std::string, std::optional<std::string>>{left, right};
}

std::pair<std::string_view, std::optional<std::string_view>>
split_once_into_views(std::string_view string, char split_char)
{
  const size_t sep_pos = string.find(split_char);
  if (sep_pos == std::string_view::npos) {
    return std::make_pair(string, std::nullopt);
  } else {
    return std::make_pair(string.substr(0, sep_pos),
                          string.substr(sep_pos + 1));
  }
}

std::pair<std::string_view, std::optional<std::string_view>>
split_option_with_concat_path(std::string_view string)
{
#ifdef _WIN32
  const char delim[] = "/\\";
#else
  const char delim[] = "/";
#endif
  size_t split_pos = string.find_first_of(delim);
  if (split_pos == std::string_view::npos) {
    return std::make_pair(string, std::nullopt);
  }

#ifdef _WIN32
  // -I/C:/foo and -I/c/foo will already be handled by delim_pos correctly
  // resulting in -I and /C:/foo or /c/foo respectively. -IC:/foo will not as
  // we would get -IC: and /foo.
  if (split_pos >= 2 && string[split_pos - 1] == ':') {
    split_pos -= 2;
  }
#endif

  return std::make_pair(string.substr(0, split_pos), string.substr(split_pos));
}

std::vector<fs::path>
split_path_list(std::string_view path_list)
{
  auto strings = split_into_views(path_list, k_path_delimiter);
  std::vector<fs::path> paths;
  std::copy(strings.cbegin(), strings.cend(), std::back_inserter(paths));
  return paths;
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

std::string
to_lowercase(std::string_view string)
{
  std::string result;
  result.resize(string.length());
  std::transform(string.begin(), string.end(), result.begin(), tolower);
  return result;
}

} // namespace util
