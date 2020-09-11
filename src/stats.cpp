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
#include "Logging.hpp"
#include "cleanup.hpp"
#include "hashutil.hpp"

#include "third_party/fmt/core.h"

#include <cmath>

const unsigned FLAG_NOZERO = 1; // don't zero with the -z option
const unsigned FLAG_ALWAYS = 2; // always show, even if zero
const unsigned FLAG_NEVER = 4;  // never show

using Logging::log;

// Returns a formatted version of a statistics value, or the empty string if the
// statistics line shouldn't be printed.
using FormatFunction = std::string (*)(uint64_t value);

static std::string format_size_times_1024(uint64_t size);
static std::string format_timestamp(uint64_t timestamp);

namespace {

struct StatisticsField
{
  StatisticsField(Statistic statistic,
                  const char* id,
                  const char* message,
                  unsigned flags = 0,
                  FormatFunction format = nullptr)
    : statistic(statistic),
      id(id),
      message(message),
      flags(flags),
      format(format)
  {
  }

  const Statistic statistic;
  const char* const id;        // for --print-stats
  const char* const message;   // for --show-stats
  const unsigned flags;        // bitmask of FLAG_* values
  const FormatFunction format; // nullptr -> use plain integer format
};

} // namespace

#define STATISTICS_FIELD(id, ...)                                              \
  {                                                                            \
    Statistic::id, #id, __VA_ARGS__                                            \
  }

// Statistics fields in display order.
const StatisticsField k_statistics_fields[] = {
  STATISTICS_FIELD(
    stats_zeroed_timestamp, "stats zeroed", FLAG_ALWAYS, format_timestamp),
  STATISTICS_FIELD(direct_cache_hit, "cache hit (direct)", FLAG_ALWAYS),
  STATISTICS_FIELD(
    preprocessed_cache_hit, "cache hit (preprocessed)", FLAG_ALWAYS),
  STATISTICS_FIELD(cache_miss, "cache miss", FLAG_ALWAYS),
  STATISTICS_FIELD(called_for_link, "called for link"),
  STATISTICS_FIELD(called_for_preprocessing, "called for preprocessing"),
  STATISTICS_FIELD(multiple_source_files, "multiple source files"),
  STATISTICS_FIELD(compiler_produced_stdout, "compiler produced stdout"),
  STATISTICS_FIELD(compiler_produced_no_output, "compiler produced no output"),
  STATISTICS_FIELD(compiler_produced_empty_output,
                   "compiler produced empty output"),
  STATISTICS_FIELD(compile_failed, "compile failed"),
  STATISTICS_FIELD(internal_error, "ccache internal error"),
  STATISTICS_FIELD(preprocessor_error, "preprocessor error"),
  STATISTICS_FIELD(could_not_use_precompiled_header,
                   "can't use precompiled header"),
  STATISTICS_FIELD(could_not_use_modules, "can't use modules"),
  STATISTICS_FIELD(could_not_find_compiler, "couldn't find the compiler"),
  STATISTICS_FIELD(missing_cache_file, "cache file missing"),
  STATISTICS_FIELD(bad_compiler_arguments, "bad compiler arguments"),
  STATISTICS_FIELD(unsupported_source_language, "unsupported source language"),
  STATISTICS_FIELD(compiler_check_failed, "compiler check failed"),
  STATISTICS_FIELD(autoconf_test, "autoconf compile/link"),
  STATISTICS_FIELD(unsupported_compiler_option, "unsupported compiler option"),
  STATISTICS_FIELD(unsupported_code_directive, "unsupported code directive"),
  STATISTICS_FIELD(output_to_stdout, "output to stdout"),
  STATISTICS_FIELD(bad_output_file, "could not write to output file"),
  STATISTICS_FIELD(no_input_file, "no input file"),
  STATISTICS_FIELD(error_hashing_extra_file, "error hashing extra file"),
  STATISTICS_FIELD(cleanups_performed, "cleanups performed", FLAG_ALWAYS),
  STATISTICS_FIELD(files_in_cache, "files in cache", FLAG_NOZERO | FLAG_ALWAYS),
  STATISTICS_FIELD(cache_size_kibibyte,
                   "cache size",
                   FLAG_NOZERO | FLAG_ALWAYS,
                   format_size_times_1024),
  STATISTICS_FIELD(obsolete_max_files, "OBSOLETE", FLAG_NOZERO | FLAG_NEVER),
  STATISTICS_FIELD(obsolete_max_size, "OBSOLETE", FLAG_NOZERO | FLAG_NEVER),
  STATISTICS_FIELD(none, nullptr),
};

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
    auto tm = Util::localtime(timestamp);
    char buffer[100] = "?";
    if (tm) {
      strftime(buffer, sizeof(buffer), "%c", &*tm);
    }
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
    unsigned long long val = std::strtoull(p, &p2, 10);
    if (p2 == p) {
      break;
    }
    counters.increment(static_cast<Statistic>(i), val);
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
    file.write(fmt::format("{}\n", counters.get_raw(i)));
  }
  try {
    file.commit();
  } catch (const Error& e) {
    // Make failure to write a stats file a soft error since it's not important
    // enough to fail whole the process AND because it is called in the Context
    // destructor.
    log("Error: {}", e.what());
  }
}

static double
stats_hit_rate(const Counters& counters)
{
  uint64_t direct = counters.get(Statistic::direct_cache_hit);
  uint64_t preprocessed = counters.get(Statistic::preprocessed_cache_hit);
  uint64_t hit = direct + preprocessed;
  uint64_t miss = counters.get(Statistic::cache_miss);
  uint64_t total = hit + miss;
  return total > 0 ? (100.0 * hit) / total : 0.0;
}

static void
stats_collect(const Config& config, Counters& counters, time_t* last_updated)
{
  uint64_t zero_timestamp = 0;

  *last_updated = 0;

  // Add up the stats in each directory.
  for (int dir = -1; dir <= 0xF; dir++) {
    std::string fname;

    if (dir == -1) {
      fname = config.cache_dir() + "/stats";
    } else {
      fname = fmt::format("{}/{:x}/stats", config.cache_dir(), dir);
    }

    counters.set(Statistic::stats_zeroed_timestamp, 0); // Don't add
    stats_read(fname, counters);
    zero_timestamp =
      std::max(counters.get(Statistic::stats_zeroed_timestamp), zero_timestamp);
    auto st = Stat::stat(fname);
    if (st && st.mtime() > *last_updated) {
      *last_updated = st.mtime();
    }
  }

  counters.set(Statistic::stats_zeroed_timestamp, zero_timestamp);
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
    log("Result: disabled");
    return;
  }

  if (!config.log_file().empty() || config.debug()) {
    for (auto& field : k_statistics_fields) {
      if (updates.get(field.statistic) != 0 && !(field.flags & FLAG_NOZERO)) {
        log("Result: {}", field.message);
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
    counters.increment(updates);
    stats_write(sfile, counters);
  }

  std::string subdir(Util::dir_name(sfile));
  bool need_cleanup = false;

  if (config.max_files() != 0
      && counters.get(Statistic::files_in_cache) > config.max_files() / 16) {
    log("Need to clean up {} since it holds {} files (limit: {} files)",
        subdir,
        counters.get(Statistic::files_in_cache),
        config.max_files() / 16);
    need_cleanup = true;
  }
  if (config.max_size() != 0
      && counters.get(Statistic::cache_size_kibibyte)
           > config.max_size() / 1024 / 16) {
    log("Need to clean up {} since it holds {} KiB (limit: {} KiB)",
        subdir,
        counters.get(Statistic::cache_size_kibibyte),
        config.max_size() / 1024 / 16);
    need_cleanup = true;
  }

  if (need_cleanup) {
    double factor = config.limit_multiple() / 16;
    uint64_t max_size = round(config.max_size() * factor);
    uint32_t max_files = round(config.max_files() * factor);
    time_t max_age = 0;
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
    auto tm = Util::localtime(last_updated);
    char timestamp[100] = "?";
    if (tm) {
      strftime(timestamp, sizeof(timestamp), "%c", &*tm);
    }
    fmt::print("stats updated                       {}\n", timestamp);
  }

  // ...and display them.
  for (size_t i = 0; k_statistics_fields[i].message; i++) {
    Statistic statistic = k_statistics_fields[i].statistic;

    if (k_statistics_fields[i].flags & FLAG_NEVER) {
      continue;
    }
    if (counters.get(statistic) == 0
        && !(k_statistics_fields[i].flags & FLAG_ALWAYS)) {
      continue;
    }

    std::string value;
    if (k_statistics_fields[i].format) {
      value = k_statistics_fields[i].format(counters.get(statistic));
    } else {
      value = fmt::format("{:8}", counters.get(statistic));
    }
    if (!value.empty()) {
      fmt::print("{:31} {}\n", k_statistics_fields[i].message, value);
    }

    if (statistic == Statistic::cache_miss) {
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

  for (size_t i = 0; k_statistics_fields[i].message; i++) {
    if (!(k_statistics_fields[i].flags & FLAG_NEVER)) {
      fmt::print("{}\t{}\n",
                 k_statistics_fields[i].id,
                 counters.get(k_statistics_fields[i].statistic));
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
      for (size_t i = 0; k_statistics_fields[i].message; i++) {
        if (!(k_statistics_fields[i].flags & FLAG_NOZERO)) {
          counters.set(k_statistics_fields[i].statistic, 0);
        }
      }
      counters.set(Statistic::stats_zeroed_timestamp, timestamp);
      stats_write(fname, counters);
    }
  }
}

// Get the per-directory limits.
void
stats_get_obsolete_limits(const std::string& dir,
                          uint64_t* maxfiles,
                          uint64_t* maxsize)
{
  assert(maxfiles);
  assert(maxsize);

  Counters counters;
  std::string sname = dir + "/stats";
  stats_read(sname, counters);
  *maxfiles = counters.get(Statistic::obsolete_max_files);
  *maxsize = counters.get(Statistic::obsolete_max_size) * 1024;
}

// Set the per-directory sizes.
void
stats_set_sizes(const std::string& dir, uint64_t num_files, uint64_t total_size)
{
  Counters counters;
  std::string statsfile = dir + "/stats";
  Lockfile lock(statsfile);
  if (lock.acquired()) {
    stats_read(statsfile, counters);
    counters.set(Statistic::files_in_cache, num_files);
    counters.set(Statistic::cache_size_kibibyte, total_size / 1024);
    stats_write(statsfile, counters);
  }
}

// Count directory cleanup run.
void
stats_add_cleanup(const std::string& dir, uint64_t count)
{
  Counters counters;
  std::string statsfile = dir + "/stats";
  Lockfile lock(statsfile);
  if (lock.acquired()) {
    stats_read(statsfile, counters);
    counters.increment(Statistic::cleanups_performed, count);
    stats_write(statsfile, counters);
  }
}
