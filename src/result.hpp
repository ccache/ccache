// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#include "system.hpp"

#include "logging.hpp"

#include <map>
#include <string>

class Context;

extern const uint8_t k_result_magic[4];
extern const uint8_t k_result_version;

using UnderlyingFileTypeInt = uint8_t;
enum class FileType : UnderlyingFileTypeInt {
  // These values are written into the cache result file. This means they must
  // never be changed or removed unless the result file version is incremented.
  // Adding new values is OK.
  object = 0,
  dependency = 1,
  stderr_output = 2,
  coverage = 3,
  stackusage = 4,
  diagnostic = 5,
  dwarf_object = 6,
};

using ResultFileMap = std::map<FileType, std::string /*path*/>;

bool result_get(const Context& ctx,
                const std::string& path,
                const ResultFileMap& result_file_map);
bool result_put(Context& ctx,
                const std::string& path,
                const ResultFileMap& result_file_map);
bool result_dump(const Context& ctx, const std::string& path, FILE* stream);
