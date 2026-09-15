// Copyright (C) 2023-2026 Joel Rosdahl and other contributors
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

#include "common.hpp"

#include <ccache/context.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/util/defer.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/tokenizer.hpp>

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <limits>
#include <utility>

using IncludeDelimiter = util::Tokenizer::IncludeDelimiter;

namespace fs = util::filesystem;

namespace {

#ifdef _WIN32
// SGR and EL handling below is based on GCC's MinGW console translator.
struct ConsoleAttributes
{
  std::uint16_t add = 0;
  std::uint16_t remove = 0;
};

ConsoleAttributes
parse_sgr_for_console(std::string_view parameters)
{
  ConsoleAttributes attributes;

  size_t pos = 0;
  do {
    size_t end = parameters.find(';', pos);
    auto parameter =
      parameters.substr(pos, end == std::string_view::npos ? end : end - pos);
    int value = 0;
    if (!parameter.empty()) {
      auto result = std::from_chars(
        parameter.data(), parameter.data() + parameter.size(), value);
      if (result.ec != std::errc()
          || result.ptr != parameter.data() + parameter.size()) {
        break;
      }
    }

    switch (value) {
    case 0:
      attributes.add |= FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
      attributes.remove = static_cast<std::uint16_t>(-1);
      break;
    case 1:
      attributes.add |= FOREGROUND_INTENSITY;
      break;
    case 4:
      attributes.add |= COMMON_LVB_UNDERSCORE;
      break;
    case 5:
      attributes.add |= BACKGROUND_INTENSITY;
      break;
    case 7:
      attributes.add |= COMMON_LVB_REVERSE_VIDEO;
      break;
    case 22:
      attributes.add &= ~FOREGROUND_INTENSITY;
      attributes.remove |= FOREGROUND_INTENSITY;
      break;
    case 24:
      attributes.add &= ~COMMON_LVB_UNDERSCORE;
      attributes.remove |= COMMON_LVB_UNDERSCORE;
      break;
    case 25:
      attributes.add &= ~BACKGROUND_INTENSITY;
      attributes.remove |= BACKGROUND_INTENSITY;
      break;
    case 27:
      attributes.add &= ~COMMON_LVB_REVERSE_VIDEO;
      attributes.remove |= COMMON_LVB_REVERSE_VIDEO;
      break;
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37: {
      constexpr std::uint16_t foreground =
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
      attributes.add &= ~foreground;
      int color = value - 30;
      if (color & 1)
        attributes.add |= FOREGROUND_RED;
      if (color & 2)
        attributes.add |= FOREGROUND_GREEN;
      if (color & 4)
        attributes.add |= FOREGROUND_BLUE;
      attributes.remove |= foreground;
      break;
    }
    case 38:
    case 48:
      return attributes;
    case 39:
      attributes.add |= FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
      attributes.remove |= FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
      break;
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47: {
      constexpr std::uint16_t background =
        BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;
      attributes.add &= ~background;
      int color = value - 40;
      if (color & 1)
        attributes.add |= BACKGROUND_RED;
      if (color & 2)
        attributes.add |= BACKGROUND_GREEN;
      if (color & 4)
        attributes.add |= BACKGROUND_BLUE;
      attributes.remove |= background;
      break;
    }
    case 49:
      attributes.add &= ~(BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE);
      attributes.remove |= BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE;
      break;
    }

    if (end == std::string_view::npos)
      break;
    pos = end + 1;
  } while (pos <= parameters.size());

  return attributes;
}

std::optional<int>
parse_erase_line_mode(std::string_view parameters)
{
  if (parameters.empty())
    return 0;
  int mode;
  auto result = std::from_chars(
    parameters.data(), parameters.data() + parameters.size(), mode);
  if (result.ec != std::errc()
      || result.ptr != parameters.data() + parameters.size()) {
    return std::nullopt;
  }
  return mode;
}

std::optional<std::pair<std::uint32_t, size_t>>
decode_utf8_character(std::string_view text)
{
  if (text.empty()) {
    return std::nullopt;
  }

  const auto first_byte = static_cast<unsigned char>(text[0]);
  std::uint32_t codepoint;
  size_t length;
  if (first_byte < 0x80) {
    return std::pair{first_byte, size_t{1}};
  } else if (first_byte >= 0xc2 && first_byte <= 0xdf) {
    codepoint = first_byte & 0x1f;
    length = 2;
  } else if (first_byte >= 0xe0 && first_byte <= 0xef) {
    codepoint = first_byte & 0x0f;
    length = 3;
  } else if (first_byte >= 0xf0 && first_byte <= 0xf4) {
    codepoint = first_byte & 0x07;
    length = 4;
  } else {
    return std::nullopt;
  }

  for (size_t i = 1; i < length; ++i) {
    if (i >= text.size()) {
      return std::nullopt;
    }
    const auto current_byte = static_cast<unsigned char>(text[i]);
    if ((current_byte & 0xc0) != 0x80) {
      return std::nullopt;
    }
    codepoint = (codepoint << 6) | (current_byte & 0x3f);
  }

  if ((length == 3 && codepoint < 0x800) || (length == 4 && codepoint < 0x10000)
      || (codepoint >= 0xd800 && codepoint <= 0xdfff) || codepoint > 0x10ffff) {
    return std::nullopt;
  }
  return std::pair{codepoint, length};
}

std::optional<std::wstring>
utf8_to_utf16(std::string_view text)
{
  size_t pos = 0;
  while (pos < text.size()
         && static_cast<unsigned char>(text[pos]) < 0x80) {
    ++pos;
  }
  if (pos == text.size()) {
    return std::nullopt;
  }

  std::wstring result;
  result.reserve(text.size());
  for (size_t i = 0; i < pos; ++i) {
    result.push_back(static_cast<unsigned char>(text[i]));
  }

  while (pos < text.size()) {
    auto decoded = decode_utf8_character(text.substr(pos));
    if (!decoded) {
      return std::nullopt;
    }
    auto [codepoint, length] = *decoded;
    if (codepoint <= 0xffff) {
      result.push_back(static_cast<wchar_t>(codepoint));
    } else {
      codepoint -= 0x10000;
      result.push_back(static_cast<wchar_t>(0xd800 + (codepoint >> 10)));
      result.push_back(static_cast<wchar_t>(0xdc00 + (codepoint & 0x3ff)));
    }
    pos += length;
  }
  return result;
}
#endif

// Search for the first match of the following regular expression:
//
//   \x1b\[[\x30-\x3f]*[\x20-\x2f]*[Km]
//
// The primary reason for not using std::regex is that it's not available for
// GCC 4.8. It's also a bit bloated. The reason for not using POSIX regex
// functionality is that it's are not available in MinGW.
std::string_view
find_first_ansi_csi_seq(std::string_view string)
{
  size_t pos = 0;
  while (pos < string.length() && string[pos] != 0x1b) {
    ++pos;
  }
  if (pos + 1 >= string.length() || string[pos + 1] != '[') {
    return {};
  }
  size_t start = pos;
  pos += 2;
  while (pos < string.length() && string[pos] >= 0x30 && string[pos] <= 0x3f) {
    ++pos;
  }
  while (pos < string.length() && string[pos] >= 0x20 && string[pos] <= 0x2f) {
    ++pos;
  }
  if (pos < string.length() && (string[pos] == 'K' || string[pos] == 'm')) {
    return string.substr(start, pos + 1 - start);
  } else {
    return {};
  }
}

#ifdef _WIN32
void
write_console_data(int fd, std::string_view data)
{
  HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  DWORD mode;
  if (GetConsoleMode(handle, &mode)) {
    if (auto utf16 = utf8_to_utf16(data)) {
      size_t pos = 0;
      while (pos < utf16->size()) {
        DWORD count = static_cast<DWORD>(std::min<size_t>(
          utf16->size() - pos, std::numeric_limits<DWORD>::max()));
        DWORD written;
        if (!WriteConsoleW(
              handle, utf16->data() + pos, count, &written, nullptr)
            || written == 0) {
          throw core::Error("Failed to write Unicode text to console");
        }
        pos += written;
      }
      return;
    }
  }

  util::throw_on_error<core::Error>(
    util::write_fd(fd, data.data(), data.length()),
    FMT("Failed to write to fd {}: ", fd));
}

void
apply_sgr(HANDLE handle, std::string_view parameters)
{
  auto attributes = parse_sgr_for_console(parameters);

  CONSOLE_SCREEN_BUFFER_INFO info;
  if (attributes.remove != static_cast<WORD>(-1)
      && GetConsoleScreenBufferInfo(handle, &info)) {
    attributes.add |= info.wAttributes & ~attributes.remove;
  }
  if (attributes.add & COMMON_LVB_REVERSE_VIDEO) {
    attributes.add = static_cast<WORD>((attributes.add & 0xff00)
                                       | ((attributes.add & 0x00f0) >> 4)
                                       | ((attributes.add & 0x000f) << 4));
    attributes.add &= ~COMMON_LVB_REVERSE_VIDEO;
  }
  SetConsoleTextAttribute(handle, attributes.add);
}

void
translate_ansi_to_console(std::string_view text, int fd)
{
  HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  size_t pos = 0;
  while (pos < text.size()) {
    auto sequence = find_first_ansi_csi_seq(text.substr(pos));
    if (sequence.empty()) {
      write_console_data(fd, text.substr(pos));
      break;
    }

    size_t sequence_pos = sequence.data() - text.data();
    write_console_data(fd, text.substr(pos, sequence_pos - pos));
    auto parameters = sequence.substr(2, sequence.size() - 3);
    if (sequence.back() == 'm') {
      apply_sgr(handle, parameters);
    } else if (sequence.back() == 'K') {
      CONSOLE_SCREEN_BUFFER_INFO info;
      if (GetConsoleScreenBufferInfo(handle, &info)) {
        auto mode = parse_erase_line_mode(parameters);
        if (!mode) {
          pos = sequence_pos + sequence.size();
          continue;
        }
        COORD start = info.dwCursorPosition;
        DWORD count;
        if (*mode == 0) {
          count = info.dwSize.X - info.dwCursorPosition.X;
        } else if (*mode == 1) {
          start.X = 0;
          count = info.dwCursorPosition.X + 1;
        } else {
          start.X = 0;
          count = info.dwSize.X;
        }
        DWORD written;
        FillConsoleOutputCharacterW(handle, L' ', count, start, &written);
        FillConsoleOutputAttribute(
          handle, info.wAttributes, count, start, &written);
      }
    }
    pos = sequence_pos + sequence.size();
  }
}
#endif

} // namespace

namespace core {

void
ensure_dir_exists(const fs::path& dir)
{
  if (auto result = fs::create_directories(dir); !result) {
    throw core::Fatal(
      FMT("Failed to create directory {}: {}", dir, result.error().message()));
  }
}

fs::path
make_relative_path(const Context& ctx,
                   const std::filesystem::path& path,
                   const std::filesystem::path& dir1,
                   const std::optional<std::filesystem::path>& dir2)
{
  DEBUG_ASSERT(dir1.is_absolute());
  DEBUG_ASSERT(!dir2 || dir2->is_absolute());
  if (!ctx.config.base_dirs().empty() && path.is_absolute()
      && util::path_starts_with(path, ctx.config.base_dirs())) {
    return util::make_relative_path(dir1, dir2.value_or(dir1), path);
  } else {
    return path;
  }
}

fs::path
make_relative_path(const Context& ctx, const fs::path& path)
{
  return make_relative_path(ctx, path, ctx.actual_cwd, ctx.apparent_cwd);
}

inline bool
parse_inlined_from_msg(std::string_view& line, std::string& result)
{
  // Reference for GCC: <https://github.com/gcc-mirror/gcc/blob/
  // c7507e395f096240ffa8fa5dfcbfcfd8c5e23bb8/gcc/langhooks.cc#L450-L467>
  static const std::string_view inlined_from_msg = "    inlined from ";
  static const std::string_view inlined_from_msg_separator = " at ";

  if (!line.starts_with(inlined_from_msg)) {
    return false;
  }

  size_t signature_end = line.find(inlined_from_msg_separator);
  if (signature_end == std::string_view::npos) {
    return false;
  }

  signature_end += inlined_from_msg_separator.size();
  result.append(line.data(), signature_end);
  line = line.substr(signature_end);

  return true;
}

inline bool
parse_in_file_included_from_msg(std::string_view& line, std::string& result)
{
  // Line prefixes from GCC plus extra space at the end. Reference:
  // <https://gcc.gnu.org/git?p=gcc.git;a=blob;f=gcc/diagnostic-format-text.cc;
  // h=856d25e8482cd0bff39bd8076e6e529e184362cc;hb=HEAD#l676>
  static const std::string_view in_file_included_from_msgs[] = {
    "                 from ",
    "In file included from ",
    "        included from ",
    "In module imported at ", // longer message first to match in full
    "In module ",
    "of module ",
    "imported at ",
  };

  for (const auto& in_file_included_from : in_file_included_from_msgs) {
    if (line.starts_with(in_file_included_from)) {
      result += in_file_included_from;
      line = line.substr(in_file_included_from.length());
      return true;
    }
  }

  return false;
}

std::string
rewrite_stderr_to_absolute_paths(std::string_view text)
{
  std::string result;
  using util::Tokenizer;
  for (auto line : Tokenizer(text,
                             "\n",
                             Tokenizer::Mode::include_empty,
                             Tokenizer::IncludeDelimiter::yes)) {
    if (!parse_inlined_from_msg(line, result)) {
      parse_in_file_included_from_msg(line, result);
    }
    while (!line.empty() && line[0] == 0x1b) {
      auto csi_seq = find_first_ansi_csi_seq(line);
      result.append(csi_seq.data(), csi_seq.length());
      line = line.substr(csi_seq.length());
    }
    size_t path_end = get_diagnostics_path_length(line);
    if (path_end == 0) {
      result.append(line.data(), line.length());
    } else {
      fs::path path(line.substr(0, path_end));
      result += util::pstr(fs::canonical(path).value_or(path));
      auto tail = line.substr(path_end);
      result.append(tail.data(), tail.length());
    }
  }
  return result;
}

void
send_to_console(const Context& ctx, std::string_view text, int fd)
{
  std::string_view text_to_send = text;
  std::string modified_text;

#ifdef _WIN32
  DWORD console_mode;
  HANDLE handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  bool translate_colors =
    ctx.args_info.translate_diagnostics_colors
    && GetConsoleMode(handle, &console_mode)
#  ifdef ENABLE_VIRTUAL_TERMINAL_PROCESSING
    && !(console_mode & ENABLE_VIRTUAL_TERMINAL_PROCESSING)
#  endif
    ;
#endif

#ifdef _WIN32
  // stdout/stderr are normally opened in text mode, which would convert
  // newlines a second time since we treat output as binary data. Make sure to
  // switch to binary mode.
  int oldmode = _setmode(fd, _O_BINARY);
  DEFER(_setmode(fd, oldmode));
#endif

  if (ctx.args_info.strip_diagnostics_colors
#ifdef _WIN32
      && !translate_colors
#endif
  ) {
    modified_text = strip_ansi_csi_seqs(text);
    text_to_send = modified_text;
  }

  if (ctx.config.absolute_paths_in_stderr()) {
    modified_text = rewrite_stderr_to_absolute_paths(text_to_send);
    text_to_send = modified_text;
  }

#ifdef _WIN32
  if (translate_colors) {
    translate_ansi_to_console(text_to_send, fd);
    return;
  }
#endif

#ifdef _WIN32
  write_console_data(fd, text_to_send);
#else
  util::throw_on_error<core::Error>(
    util::write_fd(fd, text_to_send.data(), text_to_send.length()),
    FMT("Failed to write to fd {}: ", fd));
#endif
}

std::string
strip_ansi_csi_seqs(std::string_view string)
{
  size_t pos = 0;
  std::string result;

  while (true) {
    auto seq_span = find_first_ansi_csi_seq(string.substr(pos));
    auto data_start = string.data() + pos;
    auto data_length =
      seq_span.empty() ? string.length() - pos : seq_span.data() - data_start;
    result.append(data_start, data_length);
    if (seq_span.empty()) {
      // Reached tail.
      break;
    }
    pos += data_length + seq_span.length();
  }

  return result;
}

std::size_t
get_diagnostics_path_length(std::string_view line)
{
  std::size_t path_end = 0;

#ifdef _WIN32
  // Check if the path starts with a drive letter.
  if (line.size() >= 3 && line[1] == ':' && (line[2] == '\\' || line[2] == '/')
      && ((line[0] >= 'A' && line[0] <= 'Z')
          || (line[0] >= 'a' && line[0] <= 'z'))) {
    path_end = line.find(':', 3);
    if (path_end == std::string_view::npos) {
      // Treat the drive letter as "path".
      path_end = 1;
    }
  } else {
    path_end = line.find(':');
  }
#else
  path_end = line.find(':');
#endif

  if (path_end == std::string_view::npos || path_end == 0) {
    return 0;
  }

  line = line.substr(0, path_end);
  // There could be an extra space before ':'.
  // https://developercommunity.visualstudio.com/t/10729549
  if (line.back() == ' ') {
    line.remove_suffix(1);
    path_end -= 1;
  }

  // MSVC: Strip "(line[,column])" component.
  if (!line.empty() && line.back() == ')') {
    do {
      line.remove_suffix(1);
    } while (!line.empty() && line.back() != '('
             && (util::is_digit(line.back()) || line.back() == ','));

    if (!line.empty() && line.back() == '(') {
      path_end = line.size() - 1;
    }
  }

  return path_end;
}

} // namespace core
