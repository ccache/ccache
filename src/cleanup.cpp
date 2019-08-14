// Copyright (C) 2002-2006 Andrew Tridgell
// Copyright (C) 2009-2019 Joel Rosdahl and other contributors
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

#include "Config.hpp"
#include "ccache.hpp"

#include <math.h>

struct files
{
  char* fname;
  time_t mtime;
  uint64_t size;
};

static struct files** files;
static unsigned allocated; // Size of the files array.
static unsigned num_files; // Number of used entries in the files array.

static uint64_t cache_size;
static size_t files_in_cache;
static uint64_t cache_size_threshold;
static size_t files_in_cache_threshold;

// File comparison function that orders files in mtime order, oldest first.
static int
files_compare(struct files** f1, struct files** f2)
{
  if ((*f2)->mtime == (*f1)->mtime) {
    return strcmp((*f1)->fname, (*f2)->fname);
  }
  if ((*f2)->mtime > (*f1)->mtime) {
    return -1;
  }
  return 1;
}

// This builds the list of files in the cache.
static void
traverse_fn(const char* fname, struct stat* st)
{
  if (!S_ISREG(st->st_mode)) {
    return;
  }

  char* p = x_basename(fname);
  if (str_eq(p, "stats")) {
    goto out;
  }

  if (str_startswith(p, ".nfs")) {
    // Ignore temporary NFS files that may be left for open but deleted files.
    goto out;
  }

  // Delete any tmp files older than 1 hour.
  if (strstr(p, ".tmp.") && st->st_mtime + 3600 < time(NULL)) {
    x_unlink(fname);
    goto out;
  }

  if (strstr(p, "CACHEDIR.TAG")) {
    goto out;
  }

  if (num_files == allocated) {
    allocated = 10000 + num_files * 2;
    files = (struct files**)x_realloc(files, sizeof(struct files*) * allocated);
  }

  files[num_files] = (struct files*)x_malloc(sizeof(struct files));
  files[num_files]->fname = x_strdup(fname);
  files[num_files]->mtime = st->st_mtime;
  files[num_files]->size = file_size(st);
  cache_size += files[num_files]->size;
  files_in_cache++;
  num_files++;

out:
  free(p);
}

static void
delete_file(const char* path, size_t size, bool update_counters)
{
  bool deleted = x_try_unlink(path) == 0;
  if (!deleted && errno != ENOENT && errno != ESTALE) {
    cc_log("Failed to unlink %s (%s)", path, strerror(errno));
  } else if (update_counters) {
    // The counters are intentionally subtracted even if there was no file to
    // delete since the final cache size calculation will be incorrect if they
    // aren't. (This can happen when there are several parallel ongoing
    // cleanups of the same directory.)
    cache_size -= size;
    files_in_cache--;
  }
}

// Sort the files we've found and delete the oldest ones until we are below the
// thresholds.
static bool
sort_and_clean(void)
{
  if (num_files > 1) {
    // Sort in ascending mtime order.
    qsort(files, num_files, sizeof(struct files*), (COMPAR_FN_T)files_compare);
  }

  // Delete enough files to bring us below the threshold.
  bool cleaned = false;
  for (unsigned i = 0; i < num_files; i++) {
    const char* ext;

    if ((cache_size_threshold == 0 || cache_size <= cache_size_threshold)
        && (files_in_cache_threshold == 0
            || files_in_cache <= files_in_cache_threshold)) {
      break;
    }

    ext = get_extension(files[i]->fname);
    if (str_eq(ext, ".stderr")) {
      // Make sure that the .o file is deleted before .stderr, because if the
      // ccache process gets killed after deleting the .stderr but before
      // deleting the .o, the cached result will be inconsistent. (.stderr is
      // the only file that is optional; any other file missing from the cache
      // will be detected by get_file_from_cache.)
      char* base = remove_extension(files[i]->fname);
      char* o_file = format("%s.o", base);

      // Don't subtract this extra deletion from the cache size; that
      // bookkeeping will be done when the loop reaches the .o file. If the
      // loop doesn't reach the .o file since the target limits have been
      // reached, the bookkeeping won't happen, but that small counter
      // discrepancy won't do much harm and it will correct itself in the next
      // cleanup.
      delete_file(o_file, 0, false);

      free(o_file);
      free(base);
    }
    delete_file(files[i]->fname, files[i]->size, true);
    cleaned = true;
  }
  return cleaned;
}

// Clean up one cache subdirectory.
void
clean_up_dir(const Config& config, const char* dir, double limit_multiple)
{
  cc_log("Cleaning up cache directory %s", dir);

  // When "max files" or "max cache size" is reached, one of the 16 cache
  // subdirectories is cleaned up. When doing so, files are deleted (in LRU
  // order) until the levels are below limit_multiple.
  double cache_size_float = round(config.max_size() * limit_multiple / 16);
  cache_size_threshold = (uint64_t)cache_size_float;
  double files_in_cache_float = round(config.max_files() * limit_multiple / 16);
  files_in_cache_threshold = (size_t)files_in_cache_float;

  num_files = 0;
  cache_size = 0;
  files_in_cache = 0;

  // Build a list of files.
  traverse(dir, traverse_fn);

  // Clean the cache.
  cc_log("Before cleanup: %.0f KiB, %.0f files",
         (double)cache_size / 1024,
         (double)files_in_cache);
  bool cleaned = sort_and_clean();
  cc_log("After cleanup: %.0f KiB, %.0f files",
         (double)cache_size / 1024,
         (double)files_in_cache);

  if (cleaned) {
    cc_log("Cleaned up cache directory %s", dir);
    stats_add_cleanup(dir, 1);
  }

  stats_set_sizes(dir, files_in_cache, cache_size);

  // Free it up.
  for (unsigned i = 0; i < num_files; i++) {
    free(files[i]->fname);
    free(files[i]);
    files[i] = NULL;
  }
  if (files) {
    free(files);
  }
  allocated = 0;
  files = NULL;

  num_files = 0;
  cache_size = 0;
  files_in_cache = 0;
}

// Clean up all cache subdirectories.
void
clean_up_all(const Config& config)
{
  for (int i = 0; i <= 0xF; i++) {
    char* dname = format("%s/%1x", config.cache_dir().c_str(), i);
    clean_up_dir(config, dname, 1.0);
    free(dname);
  }
}

// Traverse function for wiping files.
static void
wipe_fn(const char* fname, struct stat* st)
{
  if (!S_ISREG(st->st_mode)) {
    return;
  }

  char* p = x_basename(fname);
  if (str_eq(p, "stats")) {
    free(p);
    return;
  }
  free(p);

  files_in_cache++;

  x_unlink(fname);
}

// Wipe one cache subdirectory.
static void
wipe_dir(const char* dir)
{
  cc_log("Clearing out cache directory %s", dir);

  files_in_cache = 0;

  traverse(dir, wipe_fn);

  if (files_in_cache > 0) {
    cc_log("Cleared out cache directory %s", dir);
    stats_add_cleanup(dir, 1);
  }

  files_in_cache = 0;
}

// Wipe all cached files in all subdirectories.
void
wipe_all(const Config& config)
{
  for (int i = 0; i <= 0xF; i++) {
    char* dname = format("%s/%1x", config.cache_dir().c_str(), i);
    wipe_dir(dname);
    free(dname);
  }

  // Fix the counters.
  clean_up_all(config);
}
