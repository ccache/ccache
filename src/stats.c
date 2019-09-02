// Copyright (C) 2002-2004 Andrew Tridgell
// Copyright (C) 2009-2019 Joel Rosdahl
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

#include "ccache.h"
#include "hashutil.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern char *stats_file;
extern struct conf *conf;
extern unsigned lock_staleness_limit;
extern char *primary_config_path;
extern char *secondary_config_path;

static struct counters *counter_updates;

#define FLAG_NOZERO 1 // don't zero with the -z option
#define FLAG_ALWAYS 2 // always show, even if zero
#define FLAG_NEVER 4 // never show

// Returns a formatted version of a statistics value, or NULL if the statistics
// line shouldn't be printed. Caller frees.
typedef char *(*format_fn)(uint64_t value);

static char *format_size_times_1024(uint64_t size);
static char *format_timestamp(uint64_t timestamp);
static char *format_milliseconds(uint64_t milliseconds);
static void stats_flush_to_file(const char *sfile, struct counters *updates);

// Statistics fields in display order.
static struct {
	enum stats stat;
	const char *id; // for --print-stats
	const char *message; // for --show-stats
	format_fn format; // NULL -> use plain integer format
	unsigned flags;
} stats_info[] = {
	{
		STATS_ZEROTIMESTAMP,
		"stats_zeroed_timestamp",
		"stats zeroed",
		format_timestamp,
		FLAG_ALWAYS
	},
	{
		STATS_CACHEHIT_DIR,
		"direct_cache_hit",
		"cache hit (direct)",
		NULL,
		FLAG_ALWAYS
	},
	{
		STATS_CACHEHIT_CPP,
		"preprocessed_cache_hit",
		"cache hit (preprocessed)",
		NULL,
		FLAG_ALWAYS
	},
	{
		STATS_TOCACHE,
		"cache_miss",
		"cache miss",
		NULL,
		FLAG_ALWAYS
	},
	{
		STATS_TIME_REAL,
		"time_real_ms",
		"time (real)",
		format_milliseconds,
		FLAG_ALWAYS
	},
	{
		STATS_TIME_USER,
		"time_user_ms",
		"time (user)",
		format_milliseconds,
		FLAG_ALWAYS
	},
	{
		STATS_TIME_SYS,
		"time_sys_ms",
		"time (sys)",
		format_milliseconds,
		FLAG_ALWAYS
	},
	{
		STATS_TIME_CACHE,
		"time_cache_ms",
		"time (cache)",
		format_milliseconds,
		0
	},
	{
		STATS_TIME_COMPILE,
		"time_compile_ms",
		"time (compile)",
		format_milliseconds,
		0
	},
	{
		STATS_TIME_SAVED,
		"time_saved_ms",
		"time (saved)",
		format_milliseconds,
		FLAG_ALWAYS
	},
	{
		STATS_LINK,
		"called_for_link",
		"called for link",
		NULL,
		0
	},
	{
		STATS_PREPROCESSING,
		"called_for_preprocessing",
		"called for preprocessing",
		NULL,
		0
	},
	{
		STATS_MULTIPLE,
		"multiple_source_files",
		"multiple source files",
		NULL,
		0
	},
	{
		STATS_STDOUT,
		"compiler_produced_stdout",
		"compiler produced stdout",
		NULL,
		0
	},
	{
		STATS_NOOUTPUT,
		"compiler_produced_no_output",
		"compiler produced no output",
		NULL,
		0
	},
	{
		STATS_EMPTYOUTPUT,
		"compiler_produced_empty_output",
		"compiler produced empty output",
		NULL,
		0
	},
	{
		STATS_STATUS,
		"compile_failed",
		"compile failed",
		NULL,
		0
	},
	{
		STATS_ERROR,
		"internal_error",
		"ccache internal error",
		NULL,
		0
	},
	{
		STATS_PREPROCESSOR,
		"preprocessor_error",
		"preprocessor error",
		NULL,
		0
	},
	{
		STATS_CANTUSEPCH,
		"could_not_use_precompiled_header",
		"can't use precompiled header",
		NULL,
		0
	},
	{
		STATS_COMPILER,
		"could_not_find_compiler",
		"couldn't find the compiler",
		NULL,
		0
	},
	{
		STATS_MISSING,
		"missing_cache_file",
		"cache file missing",
		NULL,
		0
	},
	{
		STATS_ARGS,
		"bad_compiler_arguments",
		"bad compiler arguments",
		NULL,
		0
	},
	{
		STATS_SOURCELANG,
		"unsupported_source_language",
		"unsupported source language",
		NULL,
		0
	},
	{
		STATS_COMPCHECK,
		"compiler_check_failed",
		"compiler check failed",
		NULL,
		0
	},
	{
		STATS_CONFTEST,
		"autoconf_test",
		"autoconf compile/link",
		NULL,
		0
	},
	{
		STATS_UNSUPPORTED_OPTION,
		"unsupported_compiler_option",
		"unsupported compiler option",
		NULL,
		0
	},
	{
		STATS_UNSUPPORTED_DIRECTIVE,
		"unsupported_code_directive",
		"unsupported code directive",
		NULL,
		0
	},
	{
		STATS_OUTSTDOUT,
		"output_to_stdout",
		"output to stdout",
		NULL,
		0
	},
	{
		STATS_BADOUTPUTFILE,
		"bad_output_file",
		"could not write to output file",
		NULL,
		0
	},
	{
		STATS_NOINPUT,
		"no_input_file",
		"no input file",
		NULL,
		0
	},
	{
		STATS_BADEXTRAFILE,
		"error_hashing_extra_file",
		"error hashing extra file",
		NULL,
		0
	},
	{
		STATS_NUMCLEANUPS,
		"cleanups_performed",
		"cleanups performed",
		NULL,
		FLAG_ALWAYS
	},
	{
		STATS_NUMFILES,
		"files_in_cache",
		"files in cache",
		NULL,
		FLAG_NOZERO|FLAG_ALWAYS
	},
	{
		STATS_TOTALSIZE,
		"cache_size_kibibyte",
		"cache size",
		format_size_times_1024,
		FLAG_NOZERO|FLAG_ALWAYS
	},
	{
		STATS_OBSOLETE_MAXFILES,
		"OBSOLETE",
		"OBSOLETE",
		NULL,
		FLAG_NOZERO|FLAG_NEVER
	},
	{
		STATS_OBSOLETE_MAXSIZE,
		"OBSOLETE",
		"OBSOLETE",
		NULL,
		FLAG_NOZERO|FLAG_NEVER
	},
	{
		STATS_NONE,
		NULL,
		NULL,
		NULL,
		0
	}
};

static char *
format_size(uint64_t size)
{
	char *s = format_human_readable_size(size);
	reformat(&s, "%11s", s);
	return s;
}

static char *
format_milliseconds(uint64_t milliseconds)
{
	return format("%8.2f s", milliseconds*1e-3);
}

static char *
format_size_times_1024(uint64_t size)
{
	return format_size(size * 1024);
}

static char *
format_timestamp(uint64_t timestamp)
{
	if (timestamp > 0) {
		struct tm tm;
		localtime_r((time_t *)&timestamp, &tm);
		char buffer[100];
		strftime(buffer, sizeof(buffer), "%c", &tm);
		return format("    %s", buffer);
	} else {
		return NULL;
	}
}

// Parse a stats file from a buffer, adding to the counters.
static void
parse_stats(struct counters *counters, const char *buf)
{
	size_t i = 0;
	const char *p = buf;
	while (true) {
		char *p2;
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
stats_write(const char *path, struct counters *counters)
{
	char *tmp_file = format("%s.tmp", path);
	FILE *f = create_tmp_file(&tmp_file, "wb");
	for (size_t i = 0; i < counters->size; i++) {
		if (fprintf(f, "%u\n", counters->data[i]) < 0) {
			fatal("Failed to write to %s", tmp_file);
		}
	}
	fclose(f);
	x_rename(tmp_file, path);
	free(tmp_file);
}

static void
init_counter_updates(void)
{
	if (!counter_updates) {
		counter_updates = counters_init(STATS_END);
	}
}

static double
stats_hit_rate(struct counters *counters)
{
	unsigned direct = counters->data[STATS_CACHEHIT_DIR];
	unsigned preprocessed = counters->data[STATS_CACHEHIT_CPP];
	unsigned hit = direct + preprocessed;
	unsigned miss = counters->data[STATS_TOCACHE];
	unsigned total = hit + miss;
	return total > 0 ? (100.0 * hit) / total : 0.0;
}

static double
stats_time_cpu(struct counters *counters)
{
	unsigned real = counters->data[STATS_TIME_REAL];
	unsigned user = counters->data[STATS_TIME_USER];
	unsigned sys = counters->data[STATS_TIME_SYS];
	unsigned time = user + sys;
	return real > 0 ? (100.0 * time) / real : 0.0;
}

static double
stats_time_efficiency(struct counters *counters)
{
	unsigned cache = counters->data[STATS_TIME_CACHE];
	unsigned compile = counters->data[STATS_TIME_COMPILE];
	unsigned saved = counters->data[STATS_TIME_SAVED];
	unsigned total = cache + compile + saved;
	return total > 0 ? (100.0 * saved) / total : 0.0;
}

static void
stats_collect(struct counters *counters, time_t *last_updated)
{
	struct stat st;
	unsigned zero_timestamp = 0;

	*last_updated = 0;

	// Add up the stats in each directory.
	for (int dir = -1; dir <= 0xF; dir++) {
		char *fname;

		if (dir == -1) {
			fname = format("%s/stats", conf->cache_dir);
		} else {
			fname = format("%s/%1x/stats", conf->cache_dir, dir);
		}

		counters->data[STATS_ZEROTIMESTAMP] = 0; // Don't add
		stats_read(fname, counters);
		zero_timestamp = MAX(counters->data[STATS_ZEROTIMESTAMP], zero_timestamp);
		if (stat(fname, &st) == 0 && st.st_mtime > *last_updated) {
			*last_updated = st.st_mtime;
		}
		free(fname);
	}

	counters->data[STATS_ZEROTIMESTAMP] = zero_timestamp;
}

// Record that a number of bytes and files have been added to the cache. Size
// is in bytes.
void
stats_update_size(const char *sfile, int64_t size, int files)
{
	struct counters *updates;
	if (sfile == stats_file) {
		init_counter_updates();
		updates = counter_updates;
	} else {
		updates = counters_init(STATS_END);
	}
	updates->data[STATS_NUMFILES] += files;
	updates->data[STATS_TOTALSIZE] += size / 1024;
	if (sfile != stats_file) {
		stats_flush_to_file(sfile, updates);
		counters_free(updates);
	}
}

// Read in the stats from one directory and add to the counters.
void
stats_read(const char *sfile, struct counters *counters)
{
	char *data = read_text_file(sfile, 1024);
	if (data) {
		parse_stats(counters, data);
	}
	free(data);
}

// Write counter updates in updates to sfile.
static void
stats_flush_to_file(const char *sfile, struct counters *updates)
{
	assert(conf);

	if (!conf->stats) {
		return;
	}

	if (!updates) {
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
		char *stats_dir;

		// A NULL sfile means that we didn't get past calculate_object_hash(), so
		// we just choose one of stats files in the 16 subdirectories.
		stats_dir = format("%s/%x", conf->cache_dir, hash_from_int(getpid()) % 16);
		sfile = format("%s/stats", stats_dir);
		free(stats_dir);
	}

	if (!lockfile_acquire(sfile, lock_staleness_limit)) {
		return;
	}

	struct counters *counters = counters_init(STATS_END);
	stats_read(sfile, counters);
	for (int i = 0; i < STATS_END; ++i) {
		counters->data[i] += updates->data[i];
	}
	stats_write(sfile, counters);
	lockfile_release(sfile);

	if (!str_eq(conf->log_file, "") || conf->debug) {
		for (int i = 0; i < STATS_END; ++i) {
			if (updates->data[stats_info[i].stat] != 0
			    && !(stats_info[i].flags & FLAG_NOZERO)) {
				cc_log("Result: %s", stats_info[i].message);
			}
		}
	}

	char *subdir = dirname(sfile);
	bool need_cleanup = false;

	if (conf->max_files != 0
	    && counters->data[STATS_NUMFILES] > conf->max_files / 16) {
		cc_log("Need to clean up %s since it holds %u files (limit: %u files)",
		       subdir,
		       counters->data[STATS_NUMFILES],
		       conf->max_files / 16);
		need_cleanup = true;
	}
	if (conf->max_size != 0
	    && counters->data[STATS_TOTALSIZE] > conf->max_size / 1024 / 16) {
		cc_log("Need to clean up %s since it holds %u KiB (limit: %lu KiB)",
		       subdir,
		       counters->data[STATS_TOTALSIZE],
		       (unsigned long)conf->max_size / 1024 / 16);
		need_cleanup = true;
	}

	if (need_cleanup) {
		clean_up_dir(conf, subdir, conf->limit_multiple);
	}

	free(subdir);
	counters_free(counters);
}

// Write counter updates in counter_updates to disk.
void
stats_flush(void)
{
	stats_flush_to_file(stats_file, counter_updates);
	counters_free(counter_updates);
	counter_updates = NULL;
}

// Update a normal stat.
void
stats_update(enum stats stat)
{
	assert(stat > STATS_NONE && stat < STATS_END);
	init_counter_updates();
	counter_updates->data[stat]++;
}

// Update a time stat.
void
stats_update_time(enum stats stat, double seconds)
{
	assert(stat > STATS_NONE && stat < STATS_END);
	init_counter_updates();
	counter_updates->data[stat]+=seconds*1e3; // ms
}

// Get the pending update of a counter value.
unsigned
stats_get_pending(enum stats stat)
{
	init_counter_updates();
	return counter_updates->data[stat];
}

// Sum and display the total stats for all cache dirs.
void
stats_summary(void)
{
	assert(conf);

	struct counters *counters = counters_init(STATS_END);
	time_t last_updated;
	stats_collect(counters, &last_updated);

	printf("cache directory                     %s\n", conf->cache_dir);
	printf("primary config                      %s\n",
	       primary_config_path ? primary_config_path : "");
	printf("secondary config      (readonly)    %s\n",
	       secondary_config_path ? secondary_config_path : "");
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
		if (counters->data[stat] == 0 && !(stats_info[i].flags & FLAG_ALWAYS)) {
			continue;
		}

		char *value;
		if (stats_info[i].format) {
			value = stats_info[i].format(counters->data[stat]);
		} else {
			value = format("%8u", counters->data[stat]);
		}
		if (value) {
			printf("%-31s %s\n", stats_info[i].message, value);
			free(value);
		}

		if (stat == STATS_TOCACHE) {
			double percent = stats_hit_rate(counters);
			printf("cache hit rate                    %6.2f %%\n", percent);
		}
		if (stat == STATS_TIME_SYS) {
			double percent = stats_time_cpu(counters);
			printf("cpu utilization                   %6.2f %%\n", percent);
		}
		if (stat == STATS_TIME_SAVED) {
			double percent = stats_time_efficiency(counters);
			printf("cache efficiency                  %6.2f %%\n", percent);
		}
	}

	if (conf->max_files != 0) {
		printf("max files                       %8u\n", conf->max_files);
	}
	if (conf->max_size != 0) {
		char *value = format_size(conf->max_size);
		printf("max cache size                  %s\n", value);
		free(value);
	}

	counters_free(counters);
}

// Print machine-parsable (tab-separated) statistics counters.
void
stats_print(void)
{
	assert(conf);

	struct counters *counters = counters_init(STATS_END);
	time_t last_updated;
	stats_collect(counters, &last_updated);

	printf("stats_updated_timestamp\t%llu\n", (unsigned long long)last_updated);

	for (int i = 0; stats_info[i].message; i++) {
		if (!(stats_info[i].flags & FLAG_NEVER)) {
			printf("%s\t%u\n", stats_info[i].id, counters->data[stats_info[i].stat]);
		}
	}

	counters_free(counters);
}

// Zero all the stats structures.
void
stats_zero(void)
{
	assert(conf);

	char *fname = format("%s/stats", conf->cache_dir);
	x_unlink(fname);
	free(fname);

	time_t timestamp = time(NULL);

	for (int dir = 0; dir <= 0xF; dir++) {
		struct counters *counters = counters_init(STATS_END);
		struct stat st;
		fname = format("%s/%1x/stats", conf->cache_dir, dir);
		if (stat(fname, &st) != 0) {
			// No point in trying to reset the stats file if it doesn't exist.
			free(fname);
			continue;
		}
		if (lockfile_acquire(fname, lock_staleness_limit)) {
			stats_read(fname, counters);
			for (unsigned i = 0; stats_info[i].message; i++) {
				if (!(stats_info[i].flags & FLAG_NOZERO)) {
					counters->data[stats_info[i].stat] = 0;
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
stats_get_obsolete_limits(const char *dir, unsigned *maxfiles,
                          uint64_t *maxsize)
{
	struct counters *counters = counters_init(STATS_END);
	char *sname = format("%s/stats", dir);
	stats_read(sname, counters);
	*maxfiles = counters->data[STATS_OBSOLETE_MAXFILES];
	*maxsize = (uint64_t)counters->data[STATS_OBSOLETE_MAXSIZE] * 1024;
	free(sname);
	counters_free(counters);
}

// Set the per-directory sizes.
void
stats_set_sizes(const char *dir, unsigned num_files, uint64_t total_size)
{
	struct counters *counters = counters_init(STATS_END);
	char *statsfile = format("%s/stats", dir);
	if (lockfile_acquire(statsfile, lock_staleness_limit)) {
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
stats_add_cleanup(const char *dir, unsigned count)
{
	struct counters *counters = counters_init(STATS_END);
	char *statsfile = format("%s/stats", dir);
	if (lockfile_acquire(statsfile, lock_staleness_limit)) {
		stats_read(statsfile, counters);
		counters->data[STATS_NUMCLEANUPS] += count;
		stats_write(statsfile, counters);
		lockfile_release(statsfile);
	}
	free(statsfile);
	counters_free(counters);
}
