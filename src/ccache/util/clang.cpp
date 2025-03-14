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
    LOG("cant open file {}", mixed_preprocessed_path);
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