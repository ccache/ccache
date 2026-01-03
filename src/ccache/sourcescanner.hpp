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

#pragma once

#include <filesystem>
#include <string_view>
#include <vector>

namespace sourcescanner {

struct EmbedDirective
{
  std::string path;
  bool is_system; // <...> vs "..."
};

// Scan source code for C23 #embed directives and return the referenced paths.
// Handles quoted ("...") and system (<...>) includes, line continuations, and
// embed parameters. Does not handle #embed inside comments or string literals;
// false positives are acceptable for dependency tracking purposes.
std::vector<EmbedDirective> scan_for_embed_directives(std::string_view source);

} // namespace sourcescanner
