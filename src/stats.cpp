// Copyright (C) 2002-2004 Andrew Tridgell
// Copyright (C) 2009-2020 Joel Rosdahl and other contributors
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

// Routines to handle the stats files. The stats file is stored one per cache
// subdirectory to make this more scalable.

#include "stats.hpp"

#include "AtomicFile.hpp"
#include "Context.hpp"
#include "Counters.hpp"
#include "Lockfile.hpp"
#include "cleanup.hpp"
#include "hashutil.hpp"
#include "logging.hpp"

#include "third_party/fmt/core.h"

#include <cmath>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define FLAG_NOZERO 1 // don't zero with the -z option
#define FLAG_ALWAYS 2 // always show, even if zero
#define FLAG_NEVER 4  // never show

// Returns a formatted version of a statistics value, or NULL if the statistics
// line shouldn't be printed. Caller frees.
using format_fn = char* (*)(uint64_t value);

static char* format_size_times_1024(uint64_t size);
static char* format_timestamp(uint64_t timestamp);

// Statistics fields in display order.
static struct
{
  enum stats stat;
  const char* id;      // for --print-stats
  const char* message; // for --show-stats
  format_fn format;    // NULL -> use plain integer format
  unsigned flags;
} stats_info[] = {
  {STATS_ZEROTIMESTAMP,
   "stats_zeroed_timestamp",
   "stats zeroed",
   format_timestamp,
   FLAG_ALWAYS},
  {STATS_CACHEHIT_DIR,
   "direct_cache_hit",
   "cache hit (direct)",
   nullptr,
   FLAG_ALWAYS},
  {STATS_CACHEHIT_CPP,
   "preprocessed_cache_hit",
   "cache hit (preprocessed)",
   nullptr,
   FLAG_ALWAYS},
  {STATS_CACHEMISS, "cache_miss", "cache miss", nullptr, FLAG_ALWAYS},
  {STATS_LINK, "called_for_link", "called for link", nullptr, 0},
  {STATS_PREPROCESSING,
   "called_for_preprocessing",
   "called for preprocessing",
   nullptr,
   0},
  {STATS_MULTIPLE,
   "multiple_source_files",
   "multiple source files",
   nullptr,
   0},
  {STATS_STDOUT,
   "compiler_produced_stdout",
   "compiler produced stdout",
   nullptr,
   0},
  {STATS_NOOUTPUT,
   "compiler_produced_no_output",
   "compiler produced no output",
   nullptr,
   0},
  {STATS_EMPTYOUTPUT,
   "compiler_produced_empty_output",
   "compiler produced empty output",
   nullptr,
   0},
  {STATS_STATUS, "compile_failed", "compile failed", nullptr, 0},
  {STATS_ERROR, "internal_error", "ccache internal error", nullptr, 0},
  {STATS_PREPROCESSOR, "preprocessor_error", "preprocessor error", nullptr, 0},
  {STATS_CANTUSEPCH,
   "could_not_use_precompiled_header",
   "can't use precompiled header",
   nullptr,
   0},
  {STATS_CANTUSEMODULES,
   "could_not_use_modules",
   "can't use modules",
   nullptr,
   0},
  {STATS_COMPILER,
   "could_not_find_compiler",
   "couldn't find the compiler",
   nullptr,
   0},
  {STATS_MISSING, "missing_cache_file", "cache file missing", nullptr, 0},
  {STATS_ARGS, "bad_compiler_arguments", "bad compiler arguments", nullptr, 0},
  {STATS_SOURCELANG,
   "unsupported_source_language",
   "unsupported source language",
   nullptr,
   0},
  {STATS_COMPCHECK,
   "compiler_check_failed",
   "compiler check failed",
   nullptr,
   0},
  {STATS_CONFTEST, "autoconf_test", "autoconf compile/link", nullptr, 0},
  {STATS_UNSUPPORTED_OPTION,
   "unsupported_compiler_option",
   "unsupported compiler option",
   nullptr,
   0},
  {STATS_UNSUPPORTED_DIRECTIVE,
   "unsupported_code_directive",
   "unsupported code directive",
   nullptr,
   0},
  {STATS_OUTSTDOUT, "output_to_stdout", "output to stdout", nullptr, 0},
  {STATS_BADOUTPUTFILE,
   "bad_output_file",
   "could not write to output file",
   nullptr,
   0},
  {STATS_NOINPUT, "no_input_file", "no input file", nullptr, 0},
  {STATS_BADEXTRAFILE,
   "error_hashing_extra_file",
   "error hashing extra file",
   nullptr,
   0},
  {STATS_NUMCLEANUPS,
   "cleanups_performed",
   "cleanups performed",
   nullptr,
   FLAG_ALWAYS},
  {STATS_NUMFILES,
   "files_in_cache",
   "files in cache",
   nullptr,
   FLAG_NOZERO | FLAG_ALWAYS},
  {STATS_TOTALSIZE,
   "cache_size_kibibyte",
   "cache size",
   format_size_times_1024,
   FLAG_NOZERO | FLAG_ALWAYS},
  {STATS_OBSOLETE_MAXFILES,
   "OBSOLETE",
   "OBSOLETE",
   nullptr,
   FLAG_NOZERO | FLAG_NEVER},
  {STATS_OBSOLETE_MAXSIZE,
   "OBSOLETE",
   "OBSOLETE",
   nullptr,
   FLAG_NOZERO | FLAG_NEVER},
  {STATS_NONE, nullptr, nullptr, nullptr, 0}};

static char*
format_size(uint64_t size)
{
  char* s = format_human_readable_size(size);
  reformat(&s, "%11s", s);
  return s;
}

static char*
format_size_times_1024(uint64_t size)
{
  return format_size(size * 1024);
}

static char*
format_timestamp(uint64_t timestamp)
{
  if (timestamp > 0) {
    struct tm tm;
    localtime_r((time_t*)&timestamp, &tm);
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%c", &tm);
    return format("    %s", buffer);
  } else {
    return nullptr;
  }
}

// Parse a stats file from a buffer, adding to the counters.
static void
parse_stats(Counters& counters, const char* buf)
{
  size_t i = 0;
  const char* p = buf;
  while (true) {
    char* p2;
    long val = strtol(p, &p2, 10);
    if (p2 == p) {
      break;
    }
    counters[i] += val;
    i++;
    p = p2;
  }
}

// Write out a stats file.
void
stats_write(const std::string& path, const Counters& counters)
{
  AtomicFile file(path, AtomicFile::Mode::text);
  for (size_t i = 0; i < counters.size(); ++i) {
    file.write(fmt::format("{}\n", counters[i]));
  }
  try {
    file.commit();
  } catch (const Error& e) {
    // Make failure to write a stats file a soft error since it's not important
    // enough to fail whole the process.
    cc_log("Error: %s", e.what());
  }
}

static double
stats_hit_rate(const Counters& counters)
{
  unsigned direct = counters[STATS_CACHEHIT_DIR];
  unsigned preprocessed = counters[STATS_CACHEHIT_CPP];
  unsigned hit = direct + preprocessed;
  unsigned miss = counters[STATS_CACHEMISS];
  unsigned total = hit + miss;
  return total > 0 ? (100.0 * hit) / total : 0.0;
}

static void
stats_collect(const Config& config, Counters& counters, time_t* last_updated)
{
  unsigned zero_timestamp = 0;

  *last_updated = 0;

  // Add up the stats in each directory.
  for (int dir = -1; dir <= 0xF; dir++) {
    char* fname;

    if (dir == -1) {
      fname = format("%s/stats", config.cache_dir().c_str());
    } else {
      fname = format("%s/%1x/stats", config.cache_dir().c_str(), dir);
    }

    counters[STATS_ZEROTIMESTAMP] = 0; // Don't add
    stats_read(fname, counters);
    zero_timestamp = std::max(counters[STATS_ZEROTIMESTAMP], zero_timestamp);
    auto st = Stat::stat(fname);
    if (st && st.mtime() > *last_updated) {
      *last_updated = st.mtime();
    }
    free(fname);
  }

  counters[STATS_ZEROTIMESTAMP] = zero_timestamp;
}

// Record that a number of bytes and files have been added to the cache. Size
// is in bytes.
void
stats_update_size(Counters& counters, int64_t size, int files)
{
  if (size == 0 && files == 0) {
    return;
  }

  counters[STATS_TOTALSIZE] += size / 1024;
  counters[STATS_NUMFILES] += files;
}

// Read in the stats from one directory and add to the counters.
void
stats_read(const std::string& sfile, Counters& counters)
{
  char* data = read_text_file(sfile.c_str(), 1024);
  if (data) {
    parse_stats(counters, data);
  }
  free(data);
}

// Write counter updates in updates to sfile.
void
stats_flush_to_file(const Config& config,
                    std::string sfile,
                    const Counters& updates)
{
  if (updates.all_zero()) {
    return;
  }

  if (config.disable()) {
    // Just log result, don't update statistics.
    cc_log("Result: disabled");
    return;
  }

  if (!config.log_file().empty() || config.debug()) {
    for (auto& info : stats_info) {
      if (updates[info.stat] != 0 && !(info.flags & FLAG_NOZERO)) {
        cc_log("Result: %s", info.message);
      }
    }
  }

  if (!config.stats()) {
    return;
  }

  Counters counters;

  {
    Lockfile lock(sfile);
    if (!lock.acquired()) {
      return;
    }

    stats_read(sfile, counters);
    for (int i = 0; i < STATS_END; ++i) {
      counters[i] += updates[i];
    }
    stats_write(sfile, counters);
  }

  std::string subdir(Util::dir_name(sfile));
  bool need_cleanup = false;

  if (config.max_files() != 0
      && counters[STATS_NUMFILES] > config.max_files() / 16) {
    cc_log("Need to clean up %s since it holds %u files (limit: %u files)",
           subdir.c_str(),
           counters[STATS_NUMFILES],
           config.max_files() / 16);
    need_cleanup = true;
  }
  if (config.max_size() != 0
      && counters[STATS_TOTALSIZE] > config.max_size() / 1024 / 16) {
    cc_log("Need to clean up %s since it holds %u KiB (limit: %lu KiB)",
           subdir.c_str(),
           counters[STATS_TOTALSIZE],
           (unsigned long)config.max_size() / 1024 / 16);
    need_cleanup = true;
  }

  if (need_cleanup) {
    double factor = config.limit_multiple() / 16;
    uint64_t max_size = round(config.max_size() * factor);
    uint32_t max_files = round(config.max_files() * factor);
    clean_up_dir(subdir, max_size, max_files, [](double) {});
  }
}

// Write counter updates in counter_updates to disk.
void
stats_flush(void* context)
{
  const Context& ctx = *static_cast<Context*>(context);
  stats_flush_to_file(ctx.config, ctx.stats_file(), ctx.counter_updates);
}

// Update a normal stat.
void
stats_update(Context& ctx, enum stats stat)
{
  assert(stat > STATS_NONE && stat < STATS_END);
  ctx.counter_updates[stat] += 1;
}

// Sum and display the total stats for all cache dirs.
void
stats_summary(const Config& config)
{
  Counters counters;
  time_t last_updated;
  stats_collect(config, counters, &last_updated);

  fmt::print("cache directory                     {}\n", config.cache_dir());
  fmt::print("primary config                      {}\n",
             config.primary_config_path());
  fmt::print("secondary config (readonly)         {}\n",
             config.secondary_config_path());
  if (last_updated > 0) {
    struct tm tm;
    localtime_r(&last_updated, &tm);
    char timestamp[100];
    strftime(timestamp, sizeof(timestamp), "%c", &tm);
    printf("stats updated                       %s\n", timestamp);
  }

  // ...and display them.
  for (int i = 0; stats_info[i].message; i++) {
    enum stats stat = stats_info[i].stat;

    if (stats_info[i].flags & FLAG_NEVER) {
      continue;
    }
    if (counters[stat] == 0 && !(stats_info[i].flags & FLAG_ALWAYS)) {
      continue;
    }

    char* value;
    if (stats_info[i].format) {
      value = stats_info[i].format(counters[stat]);
    } else {
      value = format("%8u", counters[stat]);
    }
    if (value) {
      printf("%-31s %s\n", stats_info[i].message, value);
      free(value);
    }

    if (stat == STATS_CACHEMISS) {
      double percent = stats_hit_rate(counters);
      printf("cache hit rate                    %6.2f %%\n", percent);
    }
  }

  if (config.max_files() != 0) {
    printf("max files                       %8u\n", config.max_files());
  }
  if (config.max_size() != 0) {
    char* value = format_size(config.max_size());
    printf("max cache size                  %s\n", value);
    free(value);
  }
}

// Print machine-parsable (tab-separated) statistics counters.
void
stats_print(const Config& config)
{
  Counters counters;
  time_t last_updated;
  stats_collect(config, counters, &last_updated);

  printf("stats_updated_timestamp\t%llu\n", (unsigned long long)last_updated);

  for (int i = 0; stats_info[i].message; i++) {
    if (!(stats_info[i].flags & FLAG_NEVER)) {
      printf("%s\t%u\n", stats_info[i].id, counters[stats_info[i].stat]);
    }
  }
}

// Zero all the stats structures.
void
stats_zero(const Config& config)
{
  char* fname = format("%s/stats", config.cache_dir().c_str());
  Util::unlink_safe(fname);
  free(fname);

  time_t timestamp = time(nullptr);

  for (int dir = 0; dir <= 0xF; dir++) {
    Counters counters;
    fname = format("%s/%1x/stats", config.cache_dir().c_str(), dir);
    if (!Stat::stat(fname)) {
      // No point in trying to reset the stats file if it doesn't exist.
      free(fname);
      continue;
    }
    Lockfile lock(fname);
    if (lock.acquired()) {
      stats_read(fname, counters);
      for (unsigned i = 0; stats_info[i].message; i++) {
        if (!(stats_info[i].flags & FLAG_NOZERO)) {
          counters[stats_info[i].stat] = 0;
        }
      }
      counters[STATS_ZEROTIMESTAMP] = timestamp;
      stats_write(fname, counters);
    }
    free(fname);
  }
}

// Get the per-directory limits.
void
stats_get_obsolete_limits(const char* dir,
                          unsigned* maxfiles,
                          uint64_t* maxsize)
{
  Counters counters;
  char* sname = format("%s/stats", dir);
  stats_read(sname, counters);
  *maxfiles = counters[STATS_OBSOLETE_MAXFILES];
  *maxsize = (uint64_t)counters[STATS_OBSOLETE_MAXSIZE] * 1024;
  free(sname);
}

// Set the per-directory sizes.
void
stats_set_sizes(const char* dir, unsigned num_files, uint64_t total_size)
{
  Counters counters;
  char* statsfile = format("%s/stats", dir);
  Lockfile lock(statsfile);
  if (lock.acquired()) {
    stats_read(statsfile, counters);
    counters[STATS_NUMFILES] = num_files;
    counters[STATS_TOTALSIZE] = total_size / 1024;
    stats_write(statsfile, counters);
  }
  free(statsfile);
}

// Count directory cleanup run.
void
stats_add_cleanup(const char* dir, unsigned count)
{
  Counters counters;
  char* statsfile = format("%s/stats", dir);
  Lockfile lock(statsfile);
  if (lock.acquired()) {
    stats_read(statsfile, counters);
    counters[STATS_NUMCLEANUPS] += count;
    stats_write(statsfile, counters);
  }
  free(statsfile);
}
