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

#pragma once

#include <ccache/compiler/invoke.hpp>
#include <ccache/util/assertions.hpp>
#include <ccache/util/filesystem.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace fs = util::filesystem;

class Config;

namespace ccache {
namespace compiler {

enum class type {
  auto_guess,
  clang,
  clang_cl,
  gcc,
  icl,
  icx,
  icx_cl,
  msvc,
  nvcc,
  other
};

// TODO: Enable shorter enum syntax once project standard is C++20.
// using enum type;

struct Description;

// Representation of a compiler type.
//
// A compiler type instance is used to distinguish the specific type of
// compiler, such as Clang, GCC, or MSVC. A wrapperr class is used around an
// underlying enum in order to support convenience functionality like
// conversion operators.
class Type
{
  type m_type;

public:
  [[nodiscard]] static auto guess(const fs::path& path) -> Type;
  [[nodiscard]] constexpr static auto parse(std::string_view name) noexcept
    -> Type;

  constexpr Type(type type) noexcept;

  explicit operator std::string() const noexcept;
  constexpr operator std::string_view() const noexcept;
  constexpr operator type() const noexcept;

  [[nodiscard]] constexpr auto describe() const noexcept -> const Description&;
};

// Representation of a compiler-related file extension.
//
// A wrapper class is used around a string_view in order to support
// convenience functionality around extension string manipulation.
class FileExt
{
  std::string_view m_repr;

public:
  constexpr FileExt(const char* repr) noexcept;
  constexpr FileExt(std::string_view repr) noexcept;

  explicit operator std::string() const noexcept;
  constexpr operator std::string_view() const noexcept;
};

// Representation of a collection of compiler-related file extensions.
struct FileExts
{
  std::optional<FileExt> binary_module_interface;
  std::optional<FileExt> dynamic_dependency_info;
  FileExt object;
  std::optional<FileExt> precompiled_header;
};

// Representation of a collection of compiler-related paths.
struct Paths
{
  std::optional<std::string_view> binary_module_path;
};

// Representation of a compiler description.
//
// A compiler description instance collects all of the distinguishing
// information about a compiler, like the compiler binary name, file
// extensions, and paths.
struct Description
{
  Type type;
  std::optional<std::string_view> name;
  FileExts file_exts;
  Paths paths;
};
static_assert(std::is_trivially_copyable_v<Description>);

} // namespace compiler

class Compiler
{
public:
  using Description = compiler::Description;
  using FileExt = compiler::FileExt;
  using FileExts = compiler::FileExts;
  using Paths = compiler::Paths;
  using Type = compiler::Type;
  using type = compiler::type;

private:
  Compiler::Type m_type;
  std::optional<std::string> m_name;
  Compiler::FileExts m_file_exts;
  Compiler::Paths m_paths;

  Compiler(const Compiler::Description& desc) noexcept;

public:
  Compiler(Compiler::type type) noexcept;
  template<
    class T,
    class = std::enable_if_t<std::is_same_v<T, std::optional<std::string>>>>
  Compiler(type t, T&& name) noexcept;

  explicit operator std::string() const noexcept;
  constexpr operator std::string_view() const noexcept;

  constexpr operator Compiler::Type() const noexcept;
  constexpr operator Compiler::type() const noexcept;

  // TODO: simplify accessors with deducing-this once project standard is C++23.

  [[nodiscard]] constexpr auto type_() const noexcept -> const Compiler::Type&;
  [[nodiscard]] constexpr auto type_() noexcept -> Compiler::Type&;

  [[nodiscard]] constexpr auto name() const noexcept
    -> const std::optional<std::string>&;
  [[nodiscard]] constexpr auto name() noexcept -> std::optional<std::string>&;

  [[nodiscard]] constexpr auto file_exts() const noexcept
    -> const Compiler::FileExts&;
  [[nodiscard]] constexpr auto paths() const noexcept -> const Compiler::Paths&;
};

// Compiler::FileExt methods
namespace compiler {
constexpr FileExt::FileExt(const char* repr) noexcept
  : m_repr(std::string_view(repr))
{
}

constexpr FileExt::FileExt(std::string_view repr) noexcept : m_repr(repr)
{
}

constexpr FileExt::operator std::string_view() const noexcept
{
  return m_repr;
}
} // namespace compiler

// Compiler::Type methods
namespace compiler {
constexpr auto
Type::parse(std::string_view name) noexcept -> Type
{
  if (name == "clang") {
    return Compiler::type::clang;
  } else if (name == "clang-cl") {
    return Compiler::type::clang_cl;
  } else if (name == "gcc") {
    return Compiler::type::gcc;
  } else if (name == "icl") {
    return Compiler::type::icl;
  } else if (name == "icx") {
    return Compiler::type::icx;
  } else if (name == "icx-cl") {
    return Compiler::type::icx_cl;
  } else if (name == "msvc") {
    return Compiler::type::msvc;
  } else if (name == "nvcc") {
    return Compiler::type::nvcc;
  } else if (name == "other") {
    return Compiler::type::other;
  } else {
    // Allow any unknown name for forward compatibility.
    return Compiler::type::auto_guess;
  }
}

constexpr Type::Type(type type) noexcept : m_type(type)
{
}

constexpr Type::operator std::string_view() const noexcept
{
  if (m_type == Compiler::type::auto_guess) {
    return "auto";
  } else {
    // TODO: enforce this with some static_assert over all defined compilers.
    ASSERT(describe().name.has_value());
    return *describe().name;
  }
}

constexpr Type::operator type() const noexcept
{
  return m_type;
}
} // namespace compiler

namespace compiler::descriptions {

// TODO: Use designated initializers once project standard is C++20.

// clang-format off
inline static constexpr Description auto_guess = {
  Compiler::type::auto_guess, // type
  {},                         // name
  {
    {},                       // binary module interface
    {},                       // dynamic dependency info
    ".o",                     // object
    ".gch"                    // precompiled header
  },
  {
    {}                        // binary module path
  }
};

inline static constexpr Description clang = {
  Compiler::type::clang,      // type
  "clang",                    // name
  {
    {},                       // binary module interface
    {},                       // dynamic dependency info
    ".o",                     // object
    ".gch"                    // precompiled header
  },
  {
    {}                        // binary module path
  }
};

inline static constexpr Description clang_cl = {
  Compiler::type::clang_cl,   // type
  "clang-cl",                 // name
  {
    {},                       // binary module interface
    {},                       // dynamic dependency info
    ".obj",                   // object
    ".pch"                    // precompiled header
  },
  {
    {}                        // binary module path
  }
};

inline static constexpr Description gcc = {
  Compiler::type::gcc,        // type
  "gcc",                      // name
  {
    ".gcm",                   // binary module interface
    ".ddi",                   // dynamic dependency info
    ".o",                     // object
    ".gch"                    // precompiled header
  },
  {
    "gcm.cache"               // binary module path
  }
};

inline static constexpr Description icl = {
  Compiler::type::icl,        // type
  "icl",                      // name
  {
    {},                       // binary module interface
    {},                       // dynamic dependency info
    ".obj",                   // object
    ".pch"                    // precompiled header
  },
  {
    {}                        // binary module path
  }
};

inline static constexpr Description icx = {
  Compiler::type::icx,        // type
  "icx",                      // name
  {
    {},                       // binary module interface
    {},                       // dynamic dependency info
    ".obj",                   // object
    ".pch"                    // precompiled header
  },
  {
    {}                        // binary module path
  }
};

inline static constexpr Description icx_cl = {
  Compiler::type::icx_cl,     // type
  "icx-cl",                   // name
  {
    {},                       // binary module interface
    {},                       // dynamic dependency info
    ".obj",                   // object
    ".pch"                    // precompiled header
  },
  {
    {}                        // binary module path
  }
};

inline static constexpr Description msvc = {
  Compiler::type::msvc,       // type
  "msvc",                     // name
  {
    {},                       // binary module interface
    {},                       // dynamic dependency info
    ".obj",                   // object
    ".pch"                    // precompiled header
  },
  {
    {}                        // binary module path
  }
};

inline static constexpr Description nvcc = {
  Compiler::type::nvcc,       // type
  "nvcc",                     // name
  {
    {},                       // binary module interface
    {},                       // dynamic dependency info
    ".o",                     // object
    ".gch"                    // precompiled header
  },
  {
    {}                        // binary module path
  }
};

inline static constexpr Description other = {
  Compiler::type::other,      // type
  "other",                    // name
  {
    {},                       // binary module interface
    {},                       // dynamic dependency info
    ".o",                     // object
    ".gch"                    // precompiled header
  },
  {
    {}                        // binary module path
  }
};

// clang-format on

} // namespace compiler::descriptions

// Compiler::Type methods
namespace compiler {
constexpr auto
Compiler::Type::describe() const noexcept -> const Description&
{
  switch (m_type) {
  case Compiler::type::auto_guess:
    return descriptions::auto_guess;
  case Compiler::type::clang:
    return descriptions::clang;
  case Compiler::type::clang_cl:
    return descriptions::clang_cl;
  case Compiler::type::gcc:
    return descriptions::gcc;
  case Compiler::type::icl:
    return descriptions::icl;
  case Compiler::type::icx:
    return descriptions::icx;
  case Compiler::type::icx_cl:
    return descriptions::icx_cl;
  case Compiler::type::msvc:
    return descriptions::msvc;
  case Compiler::type::nvcc:
    return descriptions::nvcc;
  case Compiler::type::other:
    return descriptions::other;
  default:
    ASSERT(false);
  }
}
} // namespace compiler

// Compiler methods

template<class T, class>
Compiler::Compiler(type t, T&& name) noexcept : Compiler(t)
{
  m_name = std::forward<decltype(name)>(name);
}

constexpr Compiler::operator Type() const noexcept
{
  return Type(this->operator type());
}

constexpr Compiler::operator type() const noexcept
{
  return m_type;
}

constexpr Compiler::operator std::string_view() const noexcept
{
  return type_().operator std::string_view();
}

constexpr auto
Compiler::type_() const noexcept -> const Type&
{
  return m_type;
}

constexpr auto
Compiler::type_() noexcept -> Type&
{
  return m_type;
}

constexpr auto
Compiler::name() const noexcept -> const std::optional<std::string>&
{
  return m_name;
}

constexpr auto
Compiler::name() noexcept -> std::optional<std::string>&
{
  return m_name;
}

constexpr auto
Compiler::file_exts() const noexcept -> const FileExts&
{
  return m_file_exts;
}

constexpr auto
Compiler::paths() const noexcept -> const Paths&
{
  return m_paths;
}
} // namespace ccache
