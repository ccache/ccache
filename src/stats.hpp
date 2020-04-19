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
enum stats {
  STATS_NONE = 0,
  STATS_STDOUT = 1,
  STATS_STATUS = 2,
  STATS_ERROR = 3,
  STATS_CACHEMISS = 4,
  STATS_PREPROCESSOR = 5,
  STATS_COMPILER = 6,
  STATS_MISSING = 7,
  STATS_CACHEHIT_CPP = 8,
  STATS_ARGS = 9,
  STATS_LINK = 10,
  STATS_NUMFILES = 11,
  STATS_TOTALSIZE = 12,
  STATS_OBSOLETE_MAXFILES = 13,
  STATS_OBSOLETE_MAXSIZE = 14,
  STATS_SOURCELANG = 15,
  STATS_BADOUTPUTFILE = 16,
  STATS_NOINPUT = 17,
  STATS_MULTIPLE = 18,
  STATS_CONFTEST = 19,
  STATS_UNSUPPORTED_OPTION = 20,
  STATS_OUTSTDOUT = 21,
  STATS_CACHEHIT_DIR = 22,
  STATS_NOOUTPUT = 23,
  STATS_EMPTYOUTPUT = 24,
  STATS_BADEXTRAFILE = 25,
  STATS_COMPCHECK = 26,
  STATS_CANTUSEPCH = 27,
  STATS_PREPROCESSING = 28,
  STATS_NUMCLEANUPS = 29,
  STATS_UNSUPPORTED_DIRECTIVE = 30,
  STATS_ZEROTIMESTAMP = 31,
  STATS_CANTUSEMODULES = 32,

  STATS_END
};

void stats_update(Context& ctx, enum stats stat);
void stats_flush(void* context);
void stats_flush_to_file(const Config& config,
                         std::string sfile,
                         const Counters& updates);
void stats_zero(const Config& config);
void stats_summary(const Config& config);
void stats_print(const Config& config);

void stats_update_size(Counters& counters, int64_t size, int files);
void stats_get_obsolete_limits(const char* dir,
                               unsigned* maxfiles,
                               uint64_t* maxsize);
void stats_set_sizes(const char* dir, unsigned num_files, uint64_t total_size);
void stats_add_cleanup(const char* dir, unsigned count);
void stats_read(const std::string& path, Counters& counters);
void stats_write(const std::string& path, const Counters& counters);
