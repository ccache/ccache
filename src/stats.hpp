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

#include <string>

class Config;
class Context;

// Statistics fields in storage order.
enum class Stats {
  NONE = 0,
  STDOUT = 1,
  STATUS = 2,
  INTERNAL_ERROR = 3,
  CACHEMISS = 4,
  PREPROCESSOR = 5,
  COMPILER = 6,
  MISSING = 7,
  CACHEHIT_CPP = 8,
  ARGS = 9,
  LINK = 10,
  NUMFILES = 11,
  TOTALSIZE = 12,
  OBSOLETE_MAXFILES = 13,
  OBSOLETE_MAXSIZE = 14,
  SOURCELANG = 15,
  BADOUTPUTFILE = 16,
  NOINPUT = 17,
  MULTIPLE = 18,
  CONFTEST = 19,
  UNSUPPORTED_OPTION = 20,
  OUTSTDOUT = 21,
  CACHEHIT_DIR = 22,
  NOOUTPUT = 23,
  EMPTYOUTPUT = 24,
  BADEXTRAFILE = 25,
  COMPCHECK = 26,
  CANTUSEPCH = 27,
  PREPROCESSING = 28,
  NUMCLEANUPS = 29,
  UNSUPPORTED_DIRECTIVE = 30,
  ZEROTIMESTAMP = 31,
  CANTUSEMODULES = 32,

  END
};

void stats_update(Context& ctx, Stats stat);
void stats_flush(Context& ctx);
void stats_flush_to_file(const Config& config,
                         const std::string& sfile,
                         const Counters& updates);
void stats_zero(const Context& ctx);
void stats_summary(const Context& ctx);
void stats_print(const Config& config);

void stats_update_size(Counters& counters, int64_t size, int files);
void stats_get_obsolete_limits(const std::string& dir,
                               unsigned* maxfiles,
                               uint64_t* maxsize);
void stats_set_sizes(const std::string& dir,
                     unsigned num_files,
                     uint64_t total_size);
void stats_add_cleanup(const std::string& dir, unsigned count);
void stats_read(const std::string& path, Counters& counters);
void stats_write(const std::string& path, const Counters& counters);
