// Copyright (C) 2025 Joel Rosdahl and other contributors
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

#include "clang.hpp"

#include <ccache/util/logging.hpp>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace util {

std::vector<std::string>
split_preprocess_file_in_clang_cuda(const std::string& mixed_preprocessed_path)
{
  std::ifstream infile(mixed_preprocessed_path);
  std::vector<std::string> split_preprocess_file_list;

  if (!infile) {
    LOG("Can't open file {}", mixed_preprocessed_path);
    return split_preprocess_file_list;
  }

  std::string delimiter;
  if (!std::getline(infile, delimiter)) {
    return split_preprocess_file_list;
  }

  std::string currentPart = delimiter + "\n";
  std::string line;

  while (std::getline(infile, line)) {
    if (line == delimiter) {
      split_preprocess_file_list.push_back(currentPart);
      currentPart = delimiter + "\n";
    } else {
      currentPart += line + "\n";
    }
  }

  if (!currentPart.empty()) {
    split_preprocess_file_list.push_back(currentPart);
  }

  return split_preprocess_file_list;
}

} // namespace util
