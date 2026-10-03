// Copyright (C) 2010-2026 Joel Rosdahl and other contributors
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

#include <ccache/config.hpp>

#include <filesystem>
#include <string_view>

struct LanguageInfo
{
  // Whether the language is already preprocessed.
  bool preprocessed;
};

// Guess the language of `path` based on its extension and a compiler type.
// Returns the empty string if the extension is unknown.
std::string_view language_for_file(const std::filesystem::path& path,
                                   CompilerType compiler_type);

// Return information about `language`, or nullptr if it is unsupported.
const LanguageInfo* language_info_for_language(std::string_view language);
