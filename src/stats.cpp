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
#include "cleanup.hpp"
#include "counters.hpp"
#include "hashutil.hpp"
#include "lockfile.hpp"

#include <cmath>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

// How long (in microseconds) to wait before breaking a stale lock.
constexpr unsigned k_lock_staleness_limit = 2000000;

static char*
format_size(uint64_t size)
{
  char* s = format_human_readable_size(size);
  reformat(&s, "%11s", s);
  return s;
}

char*
format_size_times_1024(uint64_t size)
{
  return format_size(size * 1024);
}

char*
format_timestamp(uint64_t timestamp)
{
  if (timestamp > 0) {
    struct tm tm;
    localtime_r((time_t*)&timestamp, &tm);
    char buffer[100];
    strftime(buffer, sizeof(buffer), "%c", &tm);
    return format("    %s", buffer);
  } else {
    return NULL;
  }
}

// Parse a stats file from a buffer, adding to the counters.
static void
parse_stats(struct counters* counters, const char* buf)
{
  size_t i = 0;
  const char* p = buf;
  while (true) {
    char* p2;
    long val = strtol(p, &p2, 10);
    if (p2 == p) {
      break;
    }
    if (counters->size < i + 1) {
      counters_resize(counters, i + 1);
    }
    counters->data[i] += val;
    i++;
    p = p2;
  }
}

// Write out a stats file.
void
stats_write(const char* path, struct counters* counters)
{
  AtomicFile file(path, AtomicFile::Mode::text);
  for (size_t i = 0; i < counters->size; ++i) {
    file.write(fmt::format("{}\n", counters->data[i]));
  }
  try {
    file.commit();
  } catch (const Error& e) {
    // Make failure to write a stats file a soft error since it's not important
    // enough to fail whole the process.
    cc_log("Error: %s", e.what());
  }
}

static void
init_counter_updates(Context& ctx)
{
  if (!ctx.counter_updates) {
    ctx.counter_updates = counters_init(STATS_END);
  }
}

static double
stats_hit_rate(struct counters* counters)
{
  unsigned direct = counters->data[STATS_CACHEHIT_DIR];
  unsigned preprocessed = counters->data[STATS_CACHEHIT_CPP];
  unsigned hit = direct + preprocessed;
  unsigned miss = counters->data[STATS_CACHEMISS];
  unsigned total = hit + miss;
  return total > 0 ? (100.0 * hit) / total : 0.0;
}

static void
stats_collect(const Config& config,
              struct counters* counters,
              time_t* last_updated)
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

    counters->data[STATS_ZEROTIMESTAMP] = 0; // Don't add
    stats_read(fname, counters);
    zero_timestamp =
      std::max(counters->data[STATS_ZEROTIMESTAMP], zero_timestamp);
    auto st = Stat::stat(fname);
    if (st && st.mtime() > *last_updated) {
      *last_updated = st.mtime();
    }
    free(fname);
  }

  counters->data[STATS_ZEROTIMESTAMP] = zero_timestamp;
}

// Record that a number of bytes and files have been added to the cache. Size
// is in bytes.
void
stats_update_size(Context& ctx, const char* sfile, int64_t size, int files)
{
  if (size == 0 && files == 0) {
    return;
  }

  struct counters* updates;
  if (sfile == ctx.stats_file) {
    init_counter_updates(ctx);
    updates = ctx.counter_updates;
  } else {
    updates = counters_init(STATS_END);
  }
  updates->data[STATS_NUMFILES] += files;
  updates->data[STATS_TOTALSIZE] += size / 1024;
  if (sfile != ctx.stats_file) {
    stats_flush_to_file(ctx, sfile, updates);
    counters_free(updates);
  }
}

// Read in the stats from one directory and add to the counters.
void
stats_read(const char* sfile, struct counters* counters)
{
  char* data = read_text_file(sfile, 1024);
  if (data) {
    parse_stats(counters, data);
  }
  free(data);
}

// Write counter updates in updates to sfile.
void
stats_flush_to_file(Context& ctx, const char* sfile, struct counters* updates)
{
  if (!updates) {
    return;
  }

  if (ctx.config.disable()) {
    // Just log result, don't update statistics.
    cc_log("Result: disabled");
    return;
  }

  if (!ctx.config.log_file().empty() || ctx.config.debug()) {
    for (int i = 0; i < STATS_END; ++i) {
      if (updates->data[ctx.stats_info[i].stat] != 0
          && !(ctx.stats_info[i].flags & FLAG_NOZERO)) {
        cc_log("Result: %s", ctx.stats_info[i].message);
      }
    }
  }

  if (!ctx.config.stats()) {
    return;
  }

  bool should_flush = false;
  for (int i = 0; i < STATS_END; ++i) {
    if (updates->data[i] > 0) {
      should_flush = true;
      break;
    }
  }
  if (!should_flush) {
    return;
  }

  if (!sfile) {
    char* stats_dir;

    // A NULL sfile means that we didn't get past calculate_object_hash(), so
    // we just choose one of stats files in the 16 subdirectories.
    stats_dir = format(
      "%s/%x", ctx.config.cache_dir().c_str(), hash_from_int(getpid()) % 16);
    sfile = format("%s/stats", stats_dir);
    free(stats_dir);
  }

  if (!lockfile_acquire(sfile, k_lock_staleness_limit)) {
    return;
  }

  struct counters* counters = counters_init(STATS_END);
  stats_read(sfile, counters);
  for (int i = 0; i < STATS_END; ++i) {
    counters->data[i] += updates->data[i];
  }
  stats_write(sfile, counters);
  lockfile_release(sfile);

  char* subdir = x_dirname(sfile);
  bool need_cleanup = false;

  if (ctx.config.max_files() != 0
      && counters->data[STATS_NUMFILES] > ctx.config.max_files() / 16) {
    cc_log("Need to clean up %s since it holds %u files (limit: %u files)",
           subdir,
           counters->data[STATS_NUMFILES],
           ctx.config.max_files() / 16);
    need_cleanup = true;
  }
  if (ctx.config.max_size() != 0
      && counters->data[STATS_TOTALSIZE] > ctx.config.max_size() / 1024 / 16) {
    cc_log("Need to clean up %s since it holds %u KiB (limit: %lu KiB)",
           subdir,
           counters->data[STATS_TOTALSIZE],
           (unsigned long)ctx.config.max_size() / 1024 / 16);
    need_cleanup = true;
  }

  if (need_cleanup) {
    double factor = ctx.config.limit_multiple() / 16;
    uint64_t max_size = round(ctx.config.max_size() * factor);
    uint32_t max_files = round(ctx.config.max_files() * factor);
    clean_up_dir(subdir, max_size, max_files, [](double) {});
  }

  free(subdir);
  counters_free(counters);
}

// Write counter updates in counter_updates to disk.
void
stats_flush(void* context)
{
  Context& ctx = *static_cast<Context*>(context);
  stats_flush_to_file(ctx, ctx.stats_file, ctx.counter_updates);

  // TODO!: is cleanup in Context OK?
  //  counters_free(ctx.counter_updates);
  //  ctx.counter_updates = NULL;
}

// Update a normal stat.
void
stats_update(Context& ctx, enum stats stat)
{
  assert(stat > STATS_NONE && stat < STATS_END);
  init_counter_updates(ctx);
  ctx.counter_updates->data[stat]++;
}

// Get the pending update of a counter value.
unsigned
stats_get_pending(Context& ctx, enum stats stat)
{
  init_counter_updates(ctx);
  return ctx.counter_updates->data[stat];
}

// Sum and display the total stats for all cache dirs.
void
stats_summary(Context& ctx)
{
  struct counters* counters = counters_init(STATS_END);
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
    printf("stats updated                       %s\n", timestamp);
  }

  // ...and display them.
  for (int i = 0; ctx.stats_info[i].message; i++) {
    enum stats stat = ctx.stats_info[i].stat;

    if (ctx.stats_info[i].flags & FLAG_NEVER) {
      continue;
    }
    if (counters->data[stat] == 0 && !(ctx.stats_info[i].flags & FLAG_ALWAYS)) {
      continue;
    }

    char* value;
    if (ctx.stats_info[i].format) {
      value = ctx.stats_info[i].format(counters->data[stat]);
    } else {
      value = format("%8u", counters->data[stat]);
    }
    if (value) {
      printf("%-31s %s\n", ctx.stats_info[i].message, value);
      free(value);
    }

    if (stat == STATS_CACHEMISS) {
      double percent = stats_hit_rate(counters);
      printf("cache hit rate                    %6.2f %%\n", percent);
    }
  }

  if (ctx.config.max_files() != 0) {
    printf("max files                       %8u\n", ctx.config.max_files());
  }
  if (ctx.config.max_size() != 0) {
    char* value = format_size(ctx.config.max_size());
    printf("max cache size                  %s\n", value);
    free(value);
  }

  counters_free(counters);
}

// Print machine-parsable (tab-separated) statistics counters.
void
stats_print(Context& ctx)
{
  struct counters* counters = counters_init(STATS_END);
  time_t last_updated;
  stats_collect(ctx.config, counters, &last_updated);

  printf("stats_updated_timestamp\t%llu\n", (unsigned long long)last_updated);

  for (int i = 0; ctx.stats_info[i].message; i++) {
    if (!(ctx.stats_info[i].flags & FLAG_NEVER)) {
      printf("%s\t%u\n",
             ctx.stats_info[i].id,
             counters->data[ctx.stats_info[i].stat]);
    }
  }

  counters_free(counters);
}

// Zero all the stats structures.
void
stats_zero(Context& ctx)
{
  char* fname = format("%s/stats", ctx.config.cache_dir().c_str());
  x_unlink(fname);
  free(fname);

  time_t timestamp = time(NULL);

  for (int dir = 0; dir <= 0xF; dir++) {
    struct counters* counters = counters_init(STATS_END);
    fname = format("%s/%1x/stats", ctx.config.cache_dir().c_str(), dir);
    if (!Stat::stat(fname)) {
      // No point in trying to reset the stats file if it doesn't exist.
      free(fname);
      continue;
    }
    if (lockfile_acquire(fname, k_lock_staleness_limit)) {
      stats_read(fname, counters);
      for (unsigned i = 0; ctx.stats_info[i].message; i++) {
        if (!(ctx.stats_info[i].flags & FLAG_NOZERO)) {
          counters->data[ctx.stats_info[i].stat] = 0;
        }
      }
      counters->data[STATS_ZEROTIMESTAMP] = timestamp;
      stats_write(fname, counters);
      lockfile_release(fname);
    }
    counters_free(counters);
    free(fname);
  }
}

// Get the per-directory limits.
void
stats_get_obsolete_limits(const char* dir,
                          unsigned* maxfiles,
                          uint64_t* maxsize)
{
  struct counters* counters = counters_init(STATS_END);
  char* sname = format("%s/stats", dir);
  stats_read(sname, counters);
  *maxfiles = counters->data[STATS_OBSOLETE_MAXFILES];
  *maxsize = (uint64_t)counters->data[STATS_OBSOLETE_MAXSIZE] * 1024;
  free(sname);
  counters_free(counters);
}

// Set the per-directory sizes.
void
stats_set_sizes(const char* dir, unsigned num_files, uint64_t total_size)
{
  struct counters* counters = counters_init(STATS_END);
  char* statsfile = format("%s/stats", dir);
  if (lockfile_acquire(statsfile, k_lock_staleness_limit)) {
    stats_read(statsfile, counters);
    counters->data[STATS_NUMFILES] = num_files;
    counters->data[STATS_TOTALSIZE] = total_size / 1024;
    stats_write(statsfile, counters);
    lockfile_release(statsfile);
  }
  free(statsfile);
  counters_free(counters);
}

// Count directory cleanup run.
void
stats_add_cleanup(const char* dir, unsigned count)
{
  struct counters* counters = counters_init(STATS_END);
  char* statsfile = format("%s/stats", dir);
  if (lockfile_acquire(statsfile, k_lock_staleness_limit)) {
    stats_read(statsfile, counters);
    counters->data[STATS_NUMCLEANUPS] += count;
    stats_write(statsfile, counters);
    lockfile_release(statsfile);
  }
  free(statsfile);
  counters_free(counters);
}
