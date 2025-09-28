// Copyright (C) 2023-2024 Joel Rosdahl and other contributors
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

#pragma once

#include <tl/expected.hpp>

#include <cstdint>
#include <filesystem>
#include <system_error>

namespace util::filesystem {

using directory_iterator = std::filesystem::directory_iterator;
using path = std::filesystem::path;

// Define wrapper with no parameters returning non-void result.
#define DEF_WRAP_0_R(name_, r_)                                                \
  inline tl::expected<r_, std::error_code> name_()                             \
  {                                                                            \
    std::error_code ec_;                                                       \
    auto result_ = std::filesystem::name_(ec_);                                \
    if (ec_) {                                                                 \
      return tl::unexpected(ec_);                                              \
    }                                                                          \
    return result_;                                                            \
  }

// Define wrapper with one parameter returning non-void result.
#define DEF_WRAP_1_R(name_, r_, t1_, p1_)                                      \
  inline tl::expected<r_, std::error_code> name_(t1_ p1_)                      \
  {                                                                            \
    std::error_code ec_;                                                       \
    auto result_ = std::filesystem::name_(p1_, ec_);                           \
    if (ec_) {                                                                 \
      return tl::unexpected(ec_);                                              \
    }                                                                          \
    return result_;                                                            \
  }

// Define predicate wrapper with one parameter. Returns true if there's no error
// and the wrapped function returned true.
#define DEF_WRAP_1_P(name_, r_, t1_, p1_)                                      \
  inline r_ name_(t1_ p1_)                                                     \
  {                                                                            \
    std::error_code ec_;                                                       \
    auto result_ = std::filesystem::name_(p1_, ec_);                           \
    return !ec_ && result_;                                                    \
  }

// Define predicate wrapper with one parameter. Returns true if there's no error
// and the wrapped function returned true.
#define DEF_WRAP_2_P(name_, r_, t1_, p1_, t2_, p2_)                            \
  inline r_ name_(t1_ p1_, t2_ p2_)                                            \
  {                                                                            \
    std::error_code ec_;                                                       \
    auto result_ = std::filesystem::name_(p1_, p2_, ec_);                      \
    return !ec_ && result_;                                                    \
  }

// Define wrapper with one parameter returning void.
#define DEF_WRAP_1_V(name_, r_, t1_, p1_)                                      \
  inline tl::expected<r_, std::error_code> name_(t1_ p1_)                      \
  {                                                                            \
    std::error_code ec_;                                                       \
    std::filesystem::name_(p1_, ec_);                                          \
    if (ec_) {                                                                 \
      return tl::unexpected(ec_);                                              \
    }                                                                          \
    return {};                                                                 \
  }

// Define wrapper with two parameters returning void.
#define DEF_WRAP_2_V(name_, r_, t1_, p1_, t2_, p2_)                            \
  inline tl::expected<r_, std::error_code> name_(t1_ p1_, t2_ p2_)             \
  {                                                                            \
    std::error_code ec_;                                                       \
    std::filesystem::name_(p1_, p2_, ec_);                                     \
    if (ec_) {                                                                 \
      return tl::unexpected(ec_);                                              \
    }                                                                          \
    return {};                                                                 \
  }

// clang-format off

//           name,                ret,            pt1,         pn1,    pt2,         pn2
DEF_WRAP_1_R(canonical,           path,           const path&, p)
DEF_WRAP_1_R(create_directories,  bool,           const path&, p)
DEF_WRAP_1_R(create_directory,    bool,           const path&, p)
DEF_WRAP_2_V(create_hard_link,    void,           const path&, target, const path&, link)
DEF_WRAP_2_V(create_symlink,      void,           const path&, target, const path&, link)
DEF_WRAP_0_R(current_path,        path)
DEF_WRAP_1_V(current_path,        void,           const path&, p)
DEF_WRAP_2_P(equivalent,          bool,           const path&, p1,     const path&, p2)
DEF_WRAP_1_P(exists,              bool,           const path&, p)
DEF_WRAP_1_P(is_directory,        bool,           const path&, p)
DEF_WRAP_1_P(is_regular_file,     bool,           const path&, p)
DEF_WRAP_1_R(read_symlink,        path,           const path&, p)
DEF_WRAP_1_R(remove,              bool,           const path&, p)
DEF_WRAP_1_R(remove_all,          std::uintmax_t, const path&, p)
DEF_WRAP_0_R(temp_directory_path, path)
DEF_WRAP_1_R(weakly_canonical,    path,           const path&, p)

// clang-format on

#undef DEF_WRAP_0_R
#undef DEF_WRAP_1_R
#undef DEF_WRAP_1_P
#undef DEF_WRAP_1_V
#undef DEF_WRAP_2_V

// Note: Mingw-w64's std::filesystem::rename is buggy and doesn't properly
// overwrite an existing file, at least in version 9.1.0, hence this custom
// wrapper.
tl::expected<void, std::error_code> rename(const path& old_p,
                                           const path& new_p);

} // namespace util::filesystem
