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

#pragma once

#include <third_party/nonstd/expected.hpp>

#include <filesystem>

namespace util::filesystem {

using directory_iterator = std::filesystem::directory_iterator;
using path = std::filesystem::path;

#define DEFINE_FS_WRAPPER(name_, fnspec_)                                      \
  template<typename... Args>                                                   \
  nonstd::expected<decltype(std::filesystem::name_ fnspec_), std::error_code>  \
  name_(Args&&... args)                                                        \
  {                                                                            \
    std::error_code ec;                                                        \
    if constexpr (std::is_same<decltype(std::filesystem::name_ fnspec_),       \
                               void>::value) {                                 \
      std::filesystem::name_(std::forward<Args>(args)..., ec);                 \
      if (ec) {                                                                \
        return nonstd::make_unexpected(ec);                                    \
      }                                                                        \
      return {};                                                               \
    } else {                                                                   \
      auto result = std::filesystem::name_(std::forward<Args>(args)..., ec);   \
      if (ec) {                                                                \
        return nonstd::make_unexpected(ec);                                    \
      }                                                                        \
      return result;                                                           \
    }                                                                          \
  }

#define DEFINE_FS_PREDICATE_WRAPPER(name_, fnspec_)                            \
  template<typename... Args> bool name_(Args&&... args)                        \
  {                                                                            \
    std::error_code ec;                                                        \
    auto result = std::filesystem::name_(std::forward<Args>(args)..., ec);     \
    return !ec && result;                                                      \
  }

DEFINE_FS_WRAPPER(canonical, (path{}))
DEFINE_FS_WRAPPER(create_directories, (path{}))
DEFINE_FS_WRAPPER(create_directory, (path{}))
DEFINE_FS_WRAPPER(create_hard_link, (path{}, path{}))
DEFINE_FS_WRAPPER(current_path, ())
DEFINE_FS_WRAPPER(read_symlink, (path{}))
DEFINE_FS_WRAPPER(remove, (path{}))
DEFINE_FS_WRAPPER(remove_all, (path{}))
DEFINE_FS_WRAPPER(temp_directory_path, ())

DEFINE_FS_PREDICATE_WRAPPER(exists, (path{}))
DEFINE_FS_PREDICATE_WRAPPER(is_directory, (path{}))

#undef DEFINE_FS_PREDICATE_WRAPPER
#undef DEFINE_FS_WRAPPER

} // namespace util::filesystem
