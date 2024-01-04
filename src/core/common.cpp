// Copyright (C) 2023 Joel Rosdahl and other contributors
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

#include "common.hpp"

#include <Context.hpp>
#include <core/exceptions.hpp>
#include <util/Finalizer.hpp>
#include <util/Tokenizer.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>
#include <util/fmtmacros.hpp>
#include <util/path.hpp>

using IncludeDelimiter = util::Tokenizer::IncludeDelimiter;

namespace fs = util::filesystem;

namespace {

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

std::string
rewrite_stderr_to_absolute_paths(std::string_view text)
{
  const std::string_view in_file_included_from = "In file included from ";

  std::string result;
  using util::Tokenizer;
  for (auto line : Tokenizer(text,
                             "\n",
                             Tokenizer::Mode::include_empty,
                             Tokenizer::IncludeDelimiter::yes)) {
    // Rewrite <path> to <absolute path> in the following two cases, where X may
    // be optional ANSI CSI sequences:
    //
    // In file included from X<path>X:1:
    // X<path>X:1:2: ...

    if (util::starts_with(line, in_file_included_from)) {
      result += in_file_included_from;
      line = line.substr(in_file_included_from.length());
    }
    while (!line.empty() && line[0] == 0x1b) {
      auto csi_seq = find_first_ansi_csi_seq(line);
      result.append(csi_seq.data(), csi_seq.length());
      line = line.substr(csi_seq.length());
    }
    size_t path_end = line.find(':');
    if (path_end == std::string_view::npos) {
      result.append(line.data(), line.length());
    } else {
      std::string path(line.substr(0, path_end));
      if (util::DirEntry(path)) {
        result += util::real_path(path);
        auto tail = line.substr(path_end);
        result.append(tail.data(), tail.length());
      } else {
        result.append(line.data(), line.length());
      }
    }
  }
  return result;
}

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

void
send_to_console(const Context& ctx, std::string_view text, int fd)
{
  std::string_view text_to_send = text;
  std::string modified_text;

#ifdef _WIN32
  // stdout/stderr are normally opened in text mode, which would convert
  // newlines a second time since we treat output as binary data. Make sure to
  // switch to binary mode.
  int oldmode = _setmode(fd, _O_BINARY);
  util::Finalizer binary_mode_restorer([=] { _setmode(fd, oldmode); });
#endif

  if (ctx.args_info.strip_diagnostics_colors) {
    modified_text = strip_ansi_csi_seqs(text);
    text_to_send = modified_text;
  }

  if (ctx.config.absolute_paths_in_stderr()) {
    modified_text = rewrite_stderr_to_absolute_paths(text_to_send);
    text_to_send = modified_text;
  }

  util::throw_on_error<core::Error>(
    util::write_fd(fd, text_to_send.data(), text_to_send.length()),
    FMT("Failed to write to fd {}: ", fd));
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

} // namespace core
