// Copyright (C) 2025 Joel Rosdahl and other contributors
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

#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>

#include <cerrno>
#include <fstream>
#include <iostream>

namespace fs = util::filesystem;

namespace util {

std::vector<std::string>
split_preprocessed_file_from_clang_cuda(const fs::path& path)
{
  std::ifstream infile(path);
  std::vector<std::string> chunks;

  if (!infile) {
    LOG("Failed to open {}: {}", path, strerror(errno));
    return chunks;
  }

  std::string delimiter;
  if (!std::getline(infile, delimiter)) {
    return chunks;
  }

  std::string current_part = delimiter + "\n";
  std::string line;

  while (std::getline(infile, line)) {
    if (line == delimiter) {
      chunks.push_back(current_part);
      current_part = delimiter + "\n";
    } else {
      current_part += line + "\n";
    }
  }

  if (!current_part.empty()) {
    chunks.push_back(current_part);
  }

  return chunks;
}

} // namespace util
