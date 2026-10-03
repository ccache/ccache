// Copyright (C) 2025-2026 Joel Rosdahl and other contributors
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

#include "clang.hpp"

namespace compiler {

std::vector<std::string>
split_preprocessed_output_from_clang_cuda(std::string_view output)
{
  std::vector<std::string> chunks;
  if (output.empty()) {
    return chunks;
  }

  const size_t first_newline = output.find('\n');
  const std::string_view delimiter = output.substr(0, first_newline);
  size_t position =
    first_newline == std::string_view::npos ? output.size() : first_newline + 1;

  std::string current_part(delimiter);
  current_part += '\n';

  while (position < output.size()) {
    const size_t newline = output.find('\n', position);
    const std::string_view line = output.substr(position, newline - position);
    if (line == delimiter) {
      chunks.push_back(current_part);
      current_part = delimiter;
      current_part += '\n';
    } else {
      current_part.append(line);
      current_part += '\n';
    }
    position = newline == std::string_view::npos ? output.size() : newline + 1;
  }

  chunks.push_back(current_part);

  return chunks;
}

} // namespace compiler
