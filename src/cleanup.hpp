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

#include "Util.hpp"

#include <string>

class Config;
class Context;

void clean_up_dir(const std::string& subdir,
                  uint64_t max_size,
                  uint32_t max_files,
                  time_t max_age,
                  const Util::ProgressReceiver& progress_receiver);

void clean_up_all(const Config& config,
                  const Util::ProgressReceiver& progress_receiver);

void wipe_all(const Context& ctx,
              const Util::ProgressReceiver& progress_receiver);

void clean_old(const Context& ctx,
               const Util::ProgressReceiver& progress_receiver,
               time_t max_age);
