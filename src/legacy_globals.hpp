// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "ccache.hpp"
#include "hash.hpp"

#include <string>
#include <unordered_map>

// variable descriptions are in the .cpp file

extern char* current_working_dir;

extern unsigned lock_staleness_limit;

extern char** ignore_headers;

extern size_t ignore_headers_len;

extern char* manifest_stats_file;

extern enum guessed_compiler guessed_compiler;

extern char* included_pch_file;
