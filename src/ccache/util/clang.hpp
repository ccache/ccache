#pragma once

#include <string>
#include <vector>

namespace util {

std::vector<std::string>
split_preprocess_file_in_clang_cuda(const std::string& mixed_preprocessed_path);

} // namespace util
