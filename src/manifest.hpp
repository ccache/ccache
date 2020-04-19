// Copyright (C) 2009-2019 Joel Rosdahl and other contributors
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

#include <string>
#include <unordered_map>

class Config;
class Context;
struct digest;

extern const uint8_t k_manifest_magic[4];
extern const uint8_t k_manifest_version;

struct digest* manifest_get(const Context& ctx, const std::string& path);
bool manifest_put(const Config& config,
                  const std::string& path,
                  const struct digest& result_name,
                  const std::unordered_map<std::string, digest>& included_files,
                  time_t time_of_compilation,
                  bool save_timestamp);
bool manifest_dump(const std::string& path, FILE* stream);
