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
#include "Logging.hpp"
#include "Manifest.hpp"
#include "Result.hpp"
#include "Statistics.hpp"
#include "StdMakeUnique.hpp"
#include "ThreadPool.hpp"
#include "ZstdCompressor.hpp"
#include "assertions.hpp"
#include "fmtmacros.hpp"

#include "third_party/fmt/core.h"

#include <string>
#include <thread>

using nonstd::optional;

namespace {

class RecompressionStatistics
{
public:
  void update(uint64_t content_size,
              uint64_t old_size,
              uint64_t new_size,
              uint64_t incompressible_size);
  uint64_t content_size() const;
  uint64_t old_size() const;
  uint64_t new_size() const;
  uint64_t incompressible_size() const;

private:
  mutable std::mutex m_mutex;
  uint64_t m_content_size = 0;
  uint64_t m_old_size = 0;
  uint64_t m_new_size = 0;
  uint64_t m_incompressible_size = 0;
};

void
RecompressionStatistics::update(uint64_t content_size,
                                uint64_t old_size,
                                uint64_t new_size,
                                uint64_t incompressible_size)
{
  std::unique_lock<std::mutex> lock(m_mutex);
  m_incompressible_size += incompressible_size;
  m_content_size += content_size;
  m_old_size += old_size;
  m_new_size += new_size;
}

uint64_t
RecompressionStatistics::content_size() const
{
  std::unique_lock<std::mutex> lock(m_mutex);
  return m_content_size;
}

uint64_t
RecompressionStatistics::old_size() const
{
  std::unique_lock<std::mutex> lock(m_mutex);
  return m_old_size;
}

uint64_t
RecompressionStatistics::new_size() const
{
  std::unique_lock<std::mutex> lock(m_mutex);
  return m_new_size;
}

uint64_t
RecompressionStatistics::incompressible_size() const
{
  std::unique_lock<std::mutex> lock(m_mutex);
  return m_incompressible_size;
}

File
open_file(const std::string& path, const char* mode)
{
  File f(path, mode);
  if (!f) {
    throw Error("failed to open {} for reading: {}", path, strerror(errno));
  }
  return f;
}

std::unique_ptr<CacheEntryReader>
create_reader(const CacheFile& cache_file, FILE* stream)
{
  switch (cache_file.type()) {
  case CacheFile::Type::result:
    return std::make_unique<CacheEntryReader>(
      stream, Result::k_magic, Result::k_version);

  case CacheFile::Type::manifest:
    return std::make_unique<CacheEntryReader>(
      stream, Manifest::k_magic, Manifest::k_version);

  case CacheFile::Type::unknown:
    // fallthrough
    ;
  }

  throw Error("unknown file type for {}", cache_file.path());
}

std::unique_ptr<CacheEntryWriter>
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

void
recompress_file(RecompressionStatistics& statistics,
                const std::string& stats_file,
                const CacheFile& cache_file,
                optional<int8_t> level)
{
  auto file = open_file(cache_file.path(), "rb");
  auto reader = create_reader(cache_file, file.get());

  auto old_stat = Stat::stat(cache_file.path(), Stat::OnError::log);
  uint64_t content_size = reader->content_size();
  int8_t wanted_level =
    level ? (*level == 0 ? ZstdCompressor::default_compression_level : *level)
          : 0;

  if (reader->compression_level() == wanted_level) {
    statistics.update(content_size, old_stat.size(), old_stat.size(), 0);
    return;
  }

  LOG("Recompressing {} to {}",
      cache_file.path(),
      level ? FMT("level {}", wanted_level) : "uncompressed");
  AtomicFile atomic_new_file(cache_file.path(), AtomicFile::Mode::binary);
  auto writer =
    create_writer(atomic_new_file.stream(),
                  *reader,
                  level ? Compression::Type::zstd : Compression::Type::none,
                  wanted_level);

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

  atomic_new_file.commit();
  auto new_stat = Stat::stat(cache_file.path(), Stat::OnError::log);

  Statistics::update(stats_file, [=](Counters& cs) {
    cs.increment(Statistic::cache_size_kibibyte,
                 Util::size_change_kibibyte(old_stat, new_stat));
  });

  statistics.update(content_size, old_stat.size(), new_stat.size(), 0);

  LOG("Recompression of {} done", cache_file.path());
}

} // namespace

void
compress_stats(const Config& config,
               const Util::ProgressReceiver& progress_receiver)
{
  uint64_t on_disk_size = 0;
  uint64_t compr_size = 0;
  uint64_t content_size = 0;
  uint64_t incompr_size = 0;

  Util::for_each_level_1_subdir(
    config.cache_dir(),
    [&](const std::string& subdir,
        const Util::ProgressReceiver& sub_progress_receiver) {
      const std::vector<FileInfo> files = Util::get_level_1_files(
        subdir, [&](double progress) { sub_progress_receiver(progress / 2); });

      for (size_t i = 0; i < files.size(); ++i) {
        const auto& cache_file = files[i];
        on_disk_size += cache_file.size_on_disk();

        try {
          auto file = open_file(cache_file.path(), "rb");
          auto reader = create_reader(CacheFile(cache_file.path()), file.get());
          compr_size += cache_file.size();
          content_size += reader->content_size();
        } catch (Error&) {
          incompr_size += cache_file.size();
        }

        sub_progress_receiver(1.0 / 2 + 1.0 * i / files.size() / 2);
      }
    },
    progress_receiver);

  if (isatty(STDOUT_FILENO)) {
    PRINT_RAW(stdout, "\n\n");
  }

  double ratio =
    compr_size > 0 ? static_cast<double>(content_size) / compr_size : 0.0;
  double savings = ratio > 0.0 ? 100.0 - (100.0 / ratio) : 0.0;

  std::string on_disk_size_str = Util::format_human_readable_size(on_disk_size);
  std::string cache_size_str =
    Util::format_human_readable_size(compr_size + incompr_size);
  std::string compr_size_str = Util::format_human_readable_size(compr_size);
  std::string content_size_str = Util::format_human_readable_size(content_size);
  std::string incompr_size_str = Util::format_human_readable_size(incompr_size);

  PRINT(stdout,
        "Total data:            {:>8s} ({} disk blocks)\n",
        cache_size_str,
        on_disk_size_str);
  PRINT(stdout,
        "Compressed data:       {:>8s} ({:.1f}% of original size)\n",
        compr_size_str,
        100.0 - savings);
  PRINT(stdout, "  - Original data:     {:>8s}\n", content_size_str);
  PRINT(stdout,
        "  - Compression ratio: {:>5.3f} x  ({:.1f}% space savings)\n",
        ratio,
        savings);
  PRINT(stdout, "Incompressible data:   {:>8s}\n", incompr_size_str);
}

void
compress_recompress(Context& ctx,
                    optional<int8_t> level,
                    const Util::ProgressReceiver& progress_receiver)
{
  const size_t threads = std::thread::hardware_concurrency();
  const size_t read_ahead = 2 * threads;
  ThreadPool thread_pool(threads, read_ahead);
  RecompressionStatistics statistics;

  Util::for_each_level_1_subdir(
    ctx.config.cache_dir(),
    [&](const std::string& subdir,
        const Util::ProgressReceiver& sub_progress_receiver) {
      const std::vector<FileInfo> files =
        Util::get_level_1_files(subdir, [&](double progress) {
          sub_progress_receiver(0.1 * progress);
        });

      auto stats_file = subdir + "/stats";

      for (size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];

        if (CacheFile(file.path()).type() != CacheFile::Type::unknown) {
          const std::string& filePath = file.path();
          thread_pool.enqueue([&statistics, stats_file, filePath, level] {
            try {
              recompress_file(
                statistics, stats_file, CacheFile(filePath), level);
            } catch (Error&) {
              // Ignore for now.
            }
          });
        } else {
          statistics.update(0, 0, 0, file.size());
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
    PRINT_RAW(stdout, "\n\n");
  }

  double old_ratio =
    statistics.old_size() > 0
      ? static_cast<double>(statistics.content_size()) / statistics.old_size()
      : 0.0;
  double old_savings = old_ratio > 0.0 ? 100.0 - (100.0 / old_ratio) : 0.0;
  double new_ratio =
    statistics.new_size() > 0
      ? static_cast<double>(statistics.content_size()) / statistics.new_size()
      : 0.0;
  double new_savings = new_ratio > 0.0 ? 100.0 - (100.0 / new_ratio) : 0.0;
  int64_t size_difference = static_cast<int64_t>(statistics.new_size())
                            - static_cast<int64_t>(statistics.old_size());

  std::string old_compr_size_str =
    Util::format_human_readable_size(statistics.old_size());
  std::string new_compr_size_str =
    Util::format_human_readable_size(statistics.new_size());
  std::string content_size_str =
    Util::format_human_readable_size(statistics.content_size());
  std::string incompr_size_str =
    Util::format_human_readable_size(statistics.incompressible_size());
  std::string size_difference_str =
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
