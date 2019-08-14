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

#include "Config.hpp"
#include "hashutil.hpp"

#include "third_party/hashtable.h"

extern const char MANIFEST_MAGIC[4];
#define MANIFEST_VERSION 2

struct digest* manifest_get(const Config& config, const char* manifest_path);
bool manifest_put(const char* manifest_path,
                  struct digest* result_digest,
                  struct hashtable* included_files);
bool manifest_dump(const char* manifest_path, FILE* stream);
