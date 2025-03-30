// Copyright (C) 2023-2024 Joel Rosdahl and other contributors
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

#include "compiler.hpp"

#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>

namespace {
namespace compiler::type {

Compiler::Type
do_guess(const fs::path& path)
{
  const auto name = util::to_lowercase(
    util::pstr(util::with_extension(path.filename(), "")).str());
  if (name.find("clang-cl") != std::string_view::npos) {
    return Compiler::type::clang_cl;
  } else if (name.find("clang") != std::string_view::npos) {
    return Compiler::type::clang;
  } else if (name.find("gcc") != std::string_view::npos
             || name.find("g++") != std::string_view::npos) {
    return Compiler::type::gcc;
  } else if (name.find("nvcc") != std::string_view::npos) {
    return Compiler::type::nvcc;
  } else if (name == "icl") {
    return Compiler::type::icl;
  } else if (name == "icx") {
    return Compiler::type::icx;
  } else if (name == "icx-cl") {
    return Compiler::type::icx_cl;
  } else if (name == "cl") {
    return Compiler::type::msvc;
  } else {
    return Compiler::type::other;
  }
}

#ifndef _WIN32

fs::path
follow_symlinks(const fs::path& path)
{
  // Follow symlinks to the real compiler to learn its name. We're not using
  // util::real_path in order to save some unnecessary stat calls.
  fs::path p = path;
  while (true) {
    auto symlink_target = fs::read_symlink(p);
    if (!symlink_target) {
      // Not a symlink.
      break;
    }
    if (symlink_target->is_absolute()) {
      p = *symlink_target;
    } else {
      p = p.parent_path() / *symlink_target;
    }
  }
  if (p != path) {
    LOG("Followed symlinks from {} to {} when guessing compiler type", path, p);
  }
  return p;
}

fs::path
probe(const fs::path& path)
{
  // Detect whether a generically named compiler (e.g. /usr/bin/cc) is a hard
  // link to a compiler with a more specific name.
  std::string name = util::pstr(path.filename()).str();
  if (name == "cc" || name == "c++") {
    static const char* candidate_names[] = {"gcc", "g++", "clang", "clang++"};
    for (const char* candidate_name : candidate_names) {
      fs::path candidate = path.parent_path() / candidate_name;
      if (fs::equivalent(candidate, path)) {
        LOG("Detected that {} is equivalent to {} when guessing compiler type",
            path,
            candidate);
        return candidate;
      }
    }
  }
  return path;
}
#endif

} // namespace compiler::type
} // namespace

auto
Compiler::Type::guess(const fs::path& path) -> Type
{
  const auto type = compiler::type::do_guess(path);
#ifdef _WIN32
  return type;
#else
  if (type == Compiler::type::other) {
    using namespace compiler::type;
    return do_guess(probe(follow_symlinks(path)));
  } else {
    return type;
  }
#endif
}

Compiler::FileExt::operator std::string() const noexcept
{
  return std::string(this->operator std::string_view());
}

Compiler::Type::operator std::string() const noexcept
{
  return std::string(this->operator std::string_view());
}

Compiler::Compiler(Description desc) noexcept
  : m_type(desc.type),
    m_name(desc.name),
    m_file_exts(desc.file_exts)
{
}

Compiler::Compiler(type t) noexcept : Compiler(Type(t).describe())
{
}
