// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include "compress.hpp"

#include "AtomicFile.hpp"
#include "CacheEntryReader.hpp"
#include "CacheEntryWriter.hpp"
#include "Context.hpp"
#include "File.hpp"
#include "StdMakeUnique.hpp"
#include "ThreadPool.hpp"
#include "manifest.hpp"
#include "result.hpp"
#include "stats.hpp"

#include "third_party/fmt/core.h"

#include <string>
#include <thread>

static File
open_file(const std::string& path, const char* mode)
{
  File f(path, mode);
  if (!f) {
    throw Error(
      fmt::format("failed to open {} for reading: {}", path, strerror(errno)));
  }
  return f;
}

static std::unique_ptr<CacheEntryReader>
create_reader(const CacheFile& cache_file, FILE* stream)
{
  if (cache_file.type() == CacheFile::Type::unknown) {
    throw Error(fmt::format("unknown file type for {}", cache_file.path()));
  }

  switch (cache_file.type()) {
  case CacheFile::Type::result:
    return std::make_unique<CacheEntryReader>(
      stream, k_result_magic, k_result_version);

  case CacheFile::Type::manifest:
    return std::make_unique<CacheEntryReader>(
      stream, k_manifest_magic, k_manifest_version);

  case CacheFile::Type::unknown:
    assert(false); // Handled at function entry.
    return {};
  }

  assert(false);
  return {};
}

static std::unique_ptr<CacheEntryWriter>
create_writer(FILE* stream,
              const CacheEntryReader& reader,
              Compression::Type compression_type,
              int8_t compression_level)
{
  return std::make_unique<CacheEntryWriter>(stream,
                                            reader.magic(),
                                            reader.version(),
                                            compression_type,
                                            compression_level,
                                            reader.payload_size());
}

static void
recompress_file(Context& ctx,
                const std::string& stats_file,
                const CacheFile& cache_file,
                int8_t level)
{
  auto file = open_file(cache_file.path(), "rb");
  auto reader = create_reader(cache_file, file.get());

  int8_t current_level = reader->compression_type() == Compression::Type::none
                           ? 0
                           : reader->compression_level();
  if (current_level == level) {
    return;
  }

  cc_log("Recompressing %s to level %d", cache_file.path().c_str(), level);
  AtomicFile atomic_new_file(cache_file.path(), AtomicFile::Mode::binary);
  auto writer = create_writer(atomic_new_file.stream(),
                              *reader,
                              level == 0 ? Compression::Type::none
                                         : Compression::Type::zstd,
                              level);

  char buffer[READ_BUFFER_SIZE];
  size_t bytes_left = reader->payload_size();
  while (bytes_left > 0) {
    size_t bytes_to_read = std::min(bytes_left, sizeof(buffer));
    reader->read(buffer, bytes_to_read);
    writer->write(buffer, bytes_to_read);
    bytes_left -= bytes_to_read;
  }
  reader->finalize();
  writer->finalize();

  file.close();

  uint64_t old_size =
    Stat::stat(cache_file.path(), Stat::OnError::log).size_on_disk();
  atomic_new_file.commit();
  uint64_t new_size =
    Stat::stat(cache_file.path(), Stat::OnError::log).size_on_disk();

  size_t size_delta = new_size - old_size;
  if (ctx.stats_file() == stats_file) {
    stats_update_size(ctx.counter_updates, size_delta, 0);
  } else {
    Counters counters;
    stats_update_size(counters, size_delta, 0);
    stats_flush_to_file(ctx.config, stats_file, counters);
  }

  cc_log("Recompression of %s done", cache_file.path().c_str());
}

void
compress_stats(const Config& config,
               const Util::ProgressReceiver& progress_receiver)
{
  uint64_t on_disk_size = 0;
  uint64_t compr_size = 0;
  uint64_t compr_orig_size = 0;
  uint64_t incompr_size = 0;

  Util::for_each_level_1_subdir(
    config.cache_dir(),
    [&](const std::string& subdir,
        const Util::ProgressReceiver& sub_progress_receiver) {
      std::vector<std::shared_ptr<CacheFile>> files;
      Util::get_level_1_files(
        subdir,
        [&](double progress) { sub_progress_receiver(progress / 2); },
        files);

      for (size_t i = 0; i < files.size(); ++i) {
        const auto& cache_file = files[i];
        on_disk_size += cache_file->lstat().size_on_disk();

        try {
          auto file = open_file(cache_file->path(), "rb");
          auto reader = create_reader(*cache_file, file.get());
          compr_size += cache_file->lstat().size();
          compr_orig_size += reader->content_size();
        } catch (Error&) {
          incompr_size += cache_file->lstat().size();
        }

        sub_progress_receiver(1.0 / 2 + 1.0 * i / files.size() / 2);
      }
    },
    progress_receiver);

  if (isatty(STDOUT_FILENO)) {
    printf("\n\n");
  }

  double ratio = compr_size > 0 ? ((double)compr_orig_size) / compr_size : 0.0;
  double savings = ratio > 0.0 ? 100.0 - (100.0 / ratio) : 0.0;

  char* on_disk_size_str = format_human_readable_size(on_disk_size);
  char* cache_size_str = format_human_readable_size(compr_size + incompr_size);
  char* compr_size_str = format_human_readable_size(compr_size);
  char* compr_orig_size_str = format_human_readable_size(compr_orig_size);
  char* incompr_size_str = format_human_readable_size(incompr_size);

  printf("Total data:            %8s (%s disk blocks)\n",
         cache_size_str,
         on_disk_size_str);
  printf("Compressible data:     %8s (%.1f%% of original size)\n",
         compr_size_str,
         100.0 - savings);
  printf("  - Original size:     %8s\n", compr_orig_size_str);
  printf(
    "  - Compression ratio: %5.3f x  (%.1f%% space savings)\n", ratio, savings);
  printf("Incompressible data:   %8s\n", incompr_size_str);

  free(incompr_size_str);
  free(compr_orig_size_str);
  free(compr_size_str);
  free(cache_size_str);
  free(on_disk_size_str);
}

void
compress_recompress(Context& ctx,
                    int8_t level,
                    const Util::ProgressReceiver& progress_receiver)
{
  const size_t threads = std::thread::hardware_concurrency();
  const size_t read_ahead = 2 * threads;
  ThreadPool thread_pool(threads, read_ahead);

  Util::for_each_level_1_subdir(
    ctx.config.cache_dir(),
    [&](const std::string& subdir,
        const Util::ProgressReceiver& sub_progress_receiver) {
      std::vector<std::shared_ptr<CacheFile>> files;
      Util::get_level_1_files(
        subdir,
        [&](double progress) { sub_progress_receiver(0.1 * progress); },
        files);

      auto stats_file = subdir + "/stats";

      for (size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];

        if (file->type() != CacheFile::Type::unknown) {
          thread_pool.enqueue([&ctx, stats_file, file, level] {
            try {
              recompress_file(ctx, stats_file, *file, level);
            } catch (Error&) {
              // Ignore for now.
            }
          });
        }

        sub_progress_receiver(0.1 + 0.9 * i / files.size());
      }

      if (Util::ends_with(subdir, "f")) {
        // Wait here instead of after Util::for_each_level_1_subdir to avoid
        // updating the progress bar to 100% before all work is done.
        thread_pool.shut_down();
      }
    },
    progress_receiver);

  if (isatty(STDOUT_FILENO)) {
    printf("\n");
  }
}
