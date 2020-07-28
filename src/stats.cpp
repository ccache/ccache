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

#define FLAG_NOZERO 1 // don't zero with the -z option
#define FLAG_ALWAYS 2 // always show, even if zero
#define FLAG_NEVER 4  // never show

// Returns a formatted version of a statistics value, or the empty string if the
// statistics line shouldn't be printed.
using format_fn = std::string (*)(uint64_t value);

static std::string format_size_times_1024(uint64_t size);
static std::string format_timestamp(uint64_t timestamp);

// Statistics fields in display order.
static struct
{
  enum stats stat;
  const char* id;      // for --print-stats
  const char* message; // for --show-stats
  format_fn format;    // nullptr -> use plain integer format
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

static std::string
format_size(uint64_t size)
{
  return fmt::format("{:>11}", Util::format_human_readable_size(size));
}

static std::string
format_size_times_1024(uint64_t size)
{
  return format_size(size * 1024);
}

static std::string
format_timestamp(uint64_t timestamp)
{
  if (timestamp > 0) {
    struct tm tm;
    localtime_r(reinterpret_cast<time_t*>(&timestamp), &tm);
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%c", &tm);
    return std::string("    ") + buffer;
  } else {
    return {};
  }
}

// Parse a stats file from a buffer, adding to the counters.
static void
parse_stats(Counters& counters, const std::string& buf)
{
  size_t i = 0;
  const char* p = buf.c_str();
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
    std::string fname;

    if (dir == -1) {
      fname = config.cache_dir() + "/stats";
    } else {
      fname = fmt::format("{}/{:x}/stats", config.cache_dir(), dir);
    }

    counters[STATS_ZEROTIMESTAMP] = 0; // Don't add
    stats_read(fname, counters);
    zero_timestamp = std::max(counters[STATS_ZEROTIMESTAMP], zero_timestamp);
    auto st = Stat::stat(fname);
    if (st && st.mtime() > *last_updated) {
      *last_updated = st.mtime();
    }
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
stats_read(const std::string& path, Counters& counters)
{
  try {
    std::string data = Util::read_file(path);
    parse_stats(counters, data);
  } catch (Error&) {
    // Ignore.
  }
}

// Write counter updates in updates to sfile.
void
stats_flush_to_file(const Config& config,
                    const std::string& sfile,
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
    uint32_t max_age = 0;
    clean_up_dir(
      subdir, max_size, max_files, max_age, [](double /*progress*/) {});
  }
}

// Write counter updates in counter_updates to disk.
void
stats_flush(Context& ctx)
{
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
stats_summary(const Context& ctx)
{
  Counters counters;
  time_t last_updated;
  stats_collect(ctx.config, counters, &last_updated);

  fmt::print("cache directory                     {}\n",
             ctx.config.cache_dir());
  fmt::print("primary config                      {}\n",
             ctx.config.primary_config_path());
  fmt::print("secondary config (readonly)         {}\n",
             ctx.config.secondary_config_path());
  if (last_updated > 0) {
    struct tm tm;
    localtime_r(&last_updated, &tm);
    char timestamp[100];
    strftime(timestamp, sizeof(timestamp), "%c", &tm);
    fmt::print("stats updated                       {}\n", timestamp);
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

    std::string value;
    if (stats_info[i].format) {
      value = stats_info[i].format(counters[stat]);
    } else {
      value = fmt::format("{:8}", counters[stat]);
    }
    if (!value.empty()) {
      fmt::print("{:31} {}\n", stats_info[i].message, value);
    }

    if (stat == STATS_CACHEMISS) {
      double percent = stats_hit_rate(counters);
      fmt::print("cache hit rate                    {:6.2f} %\n", percent);
    }
  }

  if (ctx.config.max_files() != 0) {
    fmt::print("max files                       {:8}\n",
               ctx.config.max_files());
  }
  if (ctx.config.max_size() != 0) {
    fmt::print("max cache size                  {}\n",
               format_size(ctx.config.max_size()));
  }
}

// Print machine-parsable (tab-separated) statistics counters.
void
stats_print(const Config& config)
{
  Counters counters;
  time_t last_updated;
  stats_collect(config, counters, &last_updated);

  fmt::print("stats_updated_timestamp\t{}\n", last_updated);

  for (int i = 0; stats_info[i].message; i++) {
    if (!(stats_info[i].flags & FLAG_NEVER)) {
      fmt::print("{}\t{}\n", stats_info[i].id, counters[stats_info[i].stat]);
    }
  }
}

// Zero all the stats structures.
void
stats_zero(const Context& ctx)
{
  // Remove old legacy stats file at cache top directory.
  Util::unlink_safe(ctx.config.cache_dir() + "/stats");

  time_t timestamp = time(nullptr);

  for (int dir = 0; dir <= 0xF; dir++) {
    Counters counters;
    auto fname = fmt::format("{}/{:x}/stats", ctx.config.cache_dir(), dir);
    if (!Stat::stat(fname)) {
      // No point in trying to reset the stats file if it doesn't exist.
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
  }
}

// Get the per-directory limits.
void
stats_get_obsolete_limits(const std::string& dir,
                          unsigned* maxfiles,
                          uint64_t* maxsize)
{
  assert(maxfiles);
  assert(maxsize);

  Counters counters;
  std::string sname = dir + "/stats";
  stats_read(sname, counters);
  *maxfiles = counters[STATS_OBSOLETE_MAXFILES];
  *maxsize = static_cast<uint64_t>(counters[STATS_OBSOLETE_MAXSIZE]) * 1024;
}

// Set the per-directory sizes.
void
stats_set_sizes(const std::string& dir, unsigned num_files, uint64_t total_size)
{
  Counters counters;
  std::string statsfile = dir + "/stats";
  Lockfile lock(statsfile);
  if (lock.acquired()) {
    stats_read(statsfile, counters);
    counters[STATS_NUMFILES] = num_files;
    counters[STATS_TOTALSIZE] = total_size / 1024;
    stats_write(statsfile, counters);
  }
}

// Count directory cleanup run.
void
stats_add_cleanup(const std::string& dir, unsigned count)
{
  Counters counters;
  std::string statsfile = dir + "/stats";
  Lockfile lock(statsfile);
  if (lock.acquired()) {
    stats_read(statsfile, counters);
    counters[STATS_NUMCLEANUPS] += count;
    stats_write(statsfile, counters);
  }
}
