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

#include "Counters.hpp"

#include "third_party/nonstd/optional.hpp"

#include <string>

class Config;
class Context;

void stats_zero(const Context& ctx);
void stats_summary(const Context& ctx);
void stats_print(const Config& config);

void stats_set_sizes(const std::string& dir,
                     uint64_t num_files,
                     uint64_t total_size);
void stats_add_cleanup(const std::string& dir, uint64_t count);

nonstd::optional<std::string> stats_get_result(const Counters& counters);
