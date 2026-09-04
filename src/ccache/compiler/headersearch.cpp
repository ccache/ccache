// Copyright (C) 2026 Joel Rosdahl and other contributors
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

#include "headersearch.hpp"

#include <cstdint>

namespace compiler {

namespace {

// See gcc/incpath.cc in GCC and lib/Lex/InitHeaderSearch.cpp in Clang.
const std::string_view k_quote_marker = "#include \"...\" search starts here:";
const std::string_view k_angle_marker = "#include <...> search starts here:";
const std::string_view k_end_marker = "End of search list.";
const std::string_view k_nonexistent_prefix =
  "ignoring nonexistent directory \"";
const std::string_view k_duplicate_prefix = "ignoring duplicate directory \"";
// Printed on the line after "ignoring duplicate directory" when a user
// directory duplicates a built-in system directory.
const std::string_view k_duplicate_reason =
  "  as it is a non-system directory that duplicates a system directory";

enum class Section : uint8_t { outside, quote, angle };

std::string_view
strip_line_ending(std::string_view line)
{
  while (line.ends_with('\n') || line.ends_with('\r')) {
    line.remove_suffix(1);
  }
  return line;
}

} // namespace

HeaderSearchOutput
parse_header_search_output(std::string_view stderr_data)
{
  HeaderSearchOutput output;
  output.remaining_stderr.reserve(stderr_data.size());

  Section section = Section::outside;
  bool previous_was_duplicate = false;

  size_t pos = 0;
  while (pos < stderr_data.size()) {
    const size_t newline = stderr_data.find('\n', pos);
    const size_t line_end =
      newline == std::string_view::npos ? stderr_data.size() : newline + 1;
    const std::string_view raw_line = stderr_data.substr(pos, line_end - pos);
    const std::string_view line = strip_line_ending(raw_line);
    pos = line_end;

    const bool follows_duplicate = previous_was_duplicate;
    previous_was_duplicate = line.starts_with(k_duplicate_prefix);

    bool report_line = true;
    if (line == k_quote_marker) {
      section = Section::quote;
    } else if (line == k_angle_marker) {
      section = Section::angle;
    } else if (line == k_end_marker) {
      section = Section::outside;
    } else if (section == Section::outside || !line.starts_with(' ')) {
      // Directories in the search list are indented; anything else means that
      // the list has ended (also if the end marker is missing).
      section = Section::outside;
      report_line =
        (line.starts_with(k_nonexistent_prefix) && line.ends_with('"'))
        || previous_was_duplicate
        || (follows_duplicate && line == k_duplicate_reason);
    }

    if (!report_line) {
      output.remaining_stderr.append(raw_line);
    }
  }

  return output;
}

} // namespace compiler
