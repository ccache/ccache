// Copyright (C) 2019-2022 Joel Rosdahl and other contributors
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

#include "LocalStorage.hpp"

#include <AtomicFile.hpp>
#include <Context.hpp>
#include <File.hpp>
#include <Logging.hpp>
#include <TemporaryFile.hpp>
#include <ThreadPool.hpp>
#include <assertions.hpp>
#include <core/CacheEntry.hpp>
#include <core/FileRecompressor.hpp>
#include <core/Manifest.hpp>
#include <core/Result.hpp>
#include <core/exceptions.hpp>
#include <core/wincompat.hpp>
#include <fmtmacros.hpp>
#include <storage/local/StatsFile.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/string.hpp>

#include <third_party/fmt/core.h>

#include <atomic>
#include <memory>
#include <string>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

namespace storage::local {

CompressionStatistics
LocalStorage::get_compression_statistics(
  const ProgressReceiver& progress_receiver) const
{
  CompressionStatistics cs{};

  for_each_level_1_subdir(
    m_config.cache_dir(),
    [&](const auto& subdir, const auto& sub_progress_receiver) {
      const std::vector<CacheFile> files = get_level_1_files(
        subdir, [&](double progress) { sub_progress_receiver(progress / 2); });

      for (size_t i = 0; i < files.size(); ++i) {
        const auto& cache_file = files[i];
        cs.on_disk_size += cache_file.lstat().size_on_disk();

        try {
          core::CacheEntry::Header header(cache_file.path());
          cs.compr_size += cache_file.lstat().size();
          cs.content_size += header.entry_size;
        } catch (core::Error&) {
          cs.incompr_size += cache_file.lstat().size();
        }

        sub_progress_receiver(1.0 / 2 + 1.0 * i / files.size() / 2);
      }
    },
    progress_receiver);

  return cs;
}

void
LocalStorage::recompress(const std::optional<int8_t> level,
                         const uint32_t threads,
                         const ProgressReceiver& progress_receiver)
{
  const size_t read_ahead =
    std::max(static_cast<size_t>(10), 2 * static_cast<size_t>(threads));
  ThreadPool thread_pool(threads, read_ahead);
  core::FileRecompressor recompressor;

  std::atomic<uint64_t> incompressible_size = 0;

  for_each_level_1_subdir(
    m_config.cache_dir(),
    [&](const auto& subdir, const auto& sub_progress_receiver) {
      std::vector<CacheFile> files =
        get_level_1_files(subdir, [&](double progress) {
          sub_progress_receiver(0.1 * progress);
        });

      auto stats_file = subdir + "/stats";

      for (size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];

        if (file.type() != CacheFile::Type::unknown) {
          thread_pool.enqueue(
            [&recompressor, &incompressible_size, level, stats_file, file] {
              try {
                int64_t size_change_kibibyte =
                  recompressor.recompress(file.path(), level);
                StatsFile(stats_file).update([=](auto& cs) {
                  cs.increment(core::Statistic::cache_size_kibibyte,
                               size_change_kibibyte);
                });
              } catch (core::Error&) {
                // Ignore for now.
                incompressible_size += file.lstat().size_on_disk();
              }
            });
        } else if (!TemporaryFile::is_tmp_file(file.path())) {
          incompressible_size += file.lstat().size_on_disk();
        }

        sub_progress_receiver(0.1 + 0.9 * i / files.size());
      }

      if (util::ends_with(subdir, "f")) {
        // Wait here instead of after for_each_level_1_subdir to avoid
        // updating the progress bar to 100% before all work is done.
        thread_pool.shut_down();
      }
    },
    progress_receiver);

  // In case there was no f subdir, shut down the thread pool now.
  thread_pool.shut_down();

  if (isatty(STDOUT_FILENO)) {
    PRINT_RAW(stdout, "\n\n");
  }

  const double old_ratio = recompressor.old_size() > 0
                             ? static_cast<double>(recompressor.content_size())
                                 / recompressor.old_size()
                             : 0.0;
  const double old_savings =
    old_ratio > 0.0 ? 100.0 - (100.0 / old_ratio) : 0.0;
  const double new_ratio = recompressor.new_size() > 0
                             ? static_cast<double>(recompressor.content_size())
                                 / recompressor.new_size()
                             : 0.0;
  const double new_savings =
    new_ratio > 0.0 ? 100.0 - (100.0 / new_ratio) : 0.0;
  const int64_t size_difference =
    static_cast<int64_t>(recompressor.new_size())
    - static_cast<int64_t>(recompressor.old_size());

  const std::string old_compr_size_str =
    Util::format_human_readable_size(recompressor.old_size());
  const std::string new_compr_size_str =
    Util::format_human_readable_size(recompressor.new_size());
  const std::string content_size_str =
    Util::format_human_readable_size(recompressor.content_size());
  const std::string incompr_size_str =
    Util::format_human_readable_size(incompressible_size);
  const std::string size_difference_str =
    FMT("{}{}",
        size_difference < 0 ? "-" : (size_difference > 0 ? "+" : " "),
        Util::format_human_readable_size(
          size_difference < 0 ? -size_difference : size_difference));

  PRINT(stdout, "Original data:         {:>8s}\n", content_size_str);
  PRINT(stdout,
        "Old compressed data:   {:>8s} ({:.1f}% of original size)\n",
        old_compr_size_str,
        100.0 - old_savings);
  PRINT(stdout,
        "  - Compression ratio: {:>5.3f} x  ({:.1f}% space savings)\n",
        old_ratio,
        old_savings);
  PRINT(stdout,
        "New compressed data:   {:>8s} ({:.1f}% of original size)\n",
        new_compr_size_str,
        100.0 - new_savings);
  PRINT(stdout,
        "  - Compression ratio: {:>5.3f} x  ({:.1f}% space savings)\n",
        new_ratio,
        new_savings);
  PRINT(stdout, "Size change:          {:>9s}\n", size_difference_str);
}

} // namespace storage::local
