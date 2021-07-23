// Copyright (C) 2021 Joel Rosdahl and other contributors
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

#include "mainoptions.hpp"

#include <Checksum.hpp>
#include <Config.hpp>
#include <Fd.hpp>
#include <Hash.hpp>
#include <InodeCache.hpp>
#include <Manifest.hpp>
#include <ProgressBar.hpp>
#include <ResultDumper.hpp>
#include <ResultExtractor.hpp>
#include <ccache.hpp>
#include <core/Statistics.hpp>
#include <core/StatsLog.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <storage/Storage.hpp>
#include <storage/primary/PrimaryStorage.hpp>
#include <util/TextTable.hpp>
#include <util/expected.hpp>
#include <util/string.hpp>

#include <third_party/nonstd/optional.hpp>

#include <fcntl.h>

#include <string>

#ifdef HAVE_UNISTD_H
#  include <unistd.h>
#endif

#ifdef HAVE_GETOPT_LONG
#  include <getopt.h>
#elif defined(_WIN32)
#  include <third_party/win32/getopt.h>
#else
extern "C" {
#  include <third_party/getopt_long.h>
}
#endif

namespace core {

constexpr const char VERSION_TEXT[] =
  R"({0} version {1}
Features: {2}

Copyright (C) 2002-2007 Andrew Tridgell
Copyright (C) 2009-2021 Joel Rosdahl and other contributors

See <https://ccache.dev/credits.html> for a complete list of contributors.

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.
)";

constexpr const char USAGE_TEXT[] =
  R"(Usage:
    {0} [options]
    {0} compiler [compiler options]
    compiler [compiler options]          (via symbolic link)

Common options:
    -c, --cleanup              delete old files and recalculate size counters
                               (normally not needed as this is done
                               automatically)
    -C, --clear                clear the cache completely (except configuration)
        --config-path PATH     operate on configuration file PATH instead of the
                               default
    -d, --directory PATH       operate on cache directory PATH instead of the
                               default
        --evict-older-than AGE remove files older than AGE (unsigned integer
                               with a d (days) or s (seconds) suffix)
    -F, --max-files NUM        set maximum number of files in cache to NUM (use
                               0 for no limit)
    -M, --max-size SIZE        set maximum size of cache to SIZE (use 0 for no
                               limit); available suffixes: k, M, G, T (decimal)
                               and Ki, Mi, Gi, Ti (binary); default suffix: G
    -X, --recompress LEVEL     recompress the cache to level LEVEL (integer or
                               "uncompressed") using the Zstandard algorithm;
                               see "Cache compression" in the manual for details
    -o, --set-config KEY=VAL   set configuration item KEY to value VAL
    -x, --show-compression     show compression statistics
    -p, --show-config          show current configuration options in
                               human-readable format
        --show-log-stats       print statistics counters from the stats log
                               in human-readable format
    -s, --show-stats           show summary of configuration and statistics
                               counters in human-readable format
    -z, --zero-stats           zero statistics counters

    -h, --help                 print this help text
    -V, --version              print version and copyright information

Options for scripting or debugging:
        --checksum-file PATH   print the checksum (64 bit XXH3) of the file at
                               PATH
        --dump-manifest PATH   dump manifest file at PATH in text format
        --dump-result PATH     dump result file at PATH in text format
        --extract-result PATH  extract data stored in result file at PATH to the
                               current working directory
    -k, --get-config KEY       print the value of configuration key KEY
        --hash-file PATH       print the hash (160 bit BLAKE3) of the file at
                               PATH
        --print-stats          print statistics counter IDs and corresponding
                               values in machine-parsable format

See also the manual on <https://ccache.dev/documentation.html>.
)";

static void
configuration_printer(const std::string& key,
                      const std::string& value,
                      const std::string& origin)
{
  PRINT(stdout, "({}) {} = {}\n", origin, key, value);
}

static void
print_compression_statistics(const storage::primary::CompressionStatistics& cs)
{
  const double ratio = cs.compr_size > 0
                         ? static_cast<double>(cs.content_size) / cs.compr_size
                         : 0.0;
  const double savings = ratio > 0.0 ? 100.0 - (100.0 / ratio) : 0.0;

  using C = util::TextTable::Cell;
  auto human_readable = Util::format_human_readable_size;
  util::TextTable table;

  table.add_row({
    C("Total data:"),
    C(human_readable(cs.compr_size + cs.incompr_size)).right_align(),
    C(FMT("({} disk blocks)", human_readable(cs.on_disk_size))),
  });
  table.add_row({
    C("Compressed data:"),
    C(human_readable(cs.compr_size)).right_align(),
    C(FMT("({:.1f}% of original size)", 100.0 - savings)),
  });
  table.add_row({
    C("  Original size:"),
    C(human_readable(cs.content_size)).right_align(),
  });
  table.add_row({
    C("  Compression ratio:"),
    C(FMT("{:.3f} x ", ratio)).right_align(),
    C(FMT("({:.1f}% space savings)", savings)),
  });
  table.add_row({
    C("Incompressible data:"),
    C(human_readable(cs.incompr_size)).right_align(),
  });

  PRINT_RAW(stdout, table.render());
}

static std::string
get_version_text()
{
  return FMT(
    VERSION_TEXT, CCACHE_NAME, CCACHE_VERSION, storage::get_features());
}

std::string
get_usage_text()
{
  return FMT(USAGE_TEXT, CCACHE_NAME);
}

int
process_main_options(int argc, const char* const* argv)
{
  enum longopts {
    CHECKSUM_FILE,
    CONFIG_PATH,
    DUMP_MANIFEST,
    DUMP_RESULT,
    EVICT_OLDER_THAN,
    EXTRACT_RESULT,
    HASH_FILE,
    PRINT_STATS,
    SHOW_LOG_STATS,
  };
  static const struct option options[] = {
    {"checksum-file", required_argument, nullptr, CHECKSUM_FILE},
    {"cleanup", no_argument, nullptr, 'c'},
    {"clear", no_argument, nullptr, 'C'},
    {"config-path", required_argument, nullptr, CONFIG_PATH},
    {"directory", required_argument, nullptr, 'd'},
    {"dump-manifest", required_argument, nullptr, DUMP_MANIFEST},
    {"dump-result", required_argument, nullptr, DUMP_RESULT},
    {"evict-older-than", required_argument, nullptr, EVICT_OLDER_THAN},
    {"extract-result", required_argument, nullptr, EXTRACT_RESULT},
    {"get-config", required_argument, nullptr, 'k'},
    {"hash-file", required_argument, nullptr, HASH_FILE},
    {"help", no_argument, nullptr, 'h'},
    {"max-files", required_argument, nullptr, 'F'},
    {"max-size", required_argument, nullptr, 'M'},
    {"print-stats", no_argument, nullptr, PRINT_STATS},
    {"recompress", required_argument, nullptr, 'X'},
    {"set-config", required_argument, nullptr, 'o'},
    {"show-compression", no_argument, nullptr, 'x'},
    {"show-config", no_argument, nullptr, 'p'},
    {"show-log-stats", no_argument, nullptr, SHOW_LOG_STATS},
    {"show-stats", no_argument, nullptr, 's'},
    {"version", no_argument, nullptr, 'V'},
    {"zero-stats", no_argument, nullptr, 'z'},
    {nullptr, 0, nullptr, 0}};

  int c;
  while ((c = getopt_long(argc,
                          const_cast<char* const*>(argv),
                          "cCd:k:hF:M:po:sVxX:z",
                          options,
                          nullptr))
         != -1) {
    Config config;
    config.read();

    std::string arg = optarg ? optarg : std::string();

    switch (c) {
    case CHECKSUM_FILE: {
      Checksum checksum;
      Fd fd(arg == "-" ? STDIN_FILENO : open(arg.c_str(), O_RDONLY));
      Util::read_fd(*fd, [&checksum](const void* data, size_t size) {
        checksum.update(data, size);
      });
      PRINT(stdout, "{:016x}\n", checksum.digest());
      break;
    }

    case CONFIG_PATH:
      Util::setenv("CCACHE_CONFIGPATH", arg);
      break;

    case DUMP_MANIFEST:
      return Manifest::dump(arg, stdout) ? 0 : 1;

    case DUMP_RESULT: {
      ResultDumper result_dumper(stdout);
      Result::Reader result_reader(arg);
      auto error = result_reader.read(result_dumper);
      if (error) {
        PRINT(stderr, "Error: {}\n", *error);
      }
      return error ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    case EVICT_OLDER_THAN: {
      auto seconds = Util::parse_duration(arg);
      ProgressBar progress_bar("Evicting...");
      storage::primary::PrimaryStorage(config).clean_old(
        [&](double progress) { progress_bar.update(progress); }, seconds);
      if (isatty(STDOUT_FILENO)) {
        PRINT_RAW(stdout, "\n");
      }
      break;
    }

    case EXTRACT_RESULT: {
      ResultExtractor result_extractor(".");
      Result::Reader result_reader(arg);
      auto error = result_reader.read(result_extractor);
      if (error) {
        PRINT(stderr, "Error: {}\n", *error);
      }
      return error ? EXIT_FAILURE : EXIT_SUCCESS;
    }

    case HASH_FILE: {
      Hash hash;
      if (arg == "-") {
        hash.hash_fd(STDIN_FILENO);
      } else {
        hash.hash_file(arg);
      }
      PRINT(stdout, "{}\n", hash.digest().to_string());
      break;
    }

    case PRINT_STATS: {
      StatisticsCounters counters;
      time_t last_updated;
      std::tie(counters, last_updated) =
        storage::primary::PrimaryStorage(config).get_all_statistics();
      Statistics statistics(counters);
      PRINT_RAW(stdout, statistics.format_machine_readable(last_updated));
      break;
    }

    case 'c': // --cleanup
    {
      ProgressBar progress_bar("Cleaning...");
      storage::primary::PrimaryStorage(config).clean_all(
        [&](double progress) { progress_bar.update(progress); });
      if (isatty(STDOUT_FILENO)) {
        PRINT_RAW(stdout, "\n");
      }
      break;
    }

    case 'C': // --clear
    {
      ProgressBar progress_bar("Clearing...");
      storage::primary::PrimaryStorage(config).wipe_all(
        [&](double progress) { progress_bar.update(progress); });
      if (isatty(STDOUT_FILENO)) {
        PRINT_RAW(stdout, "\n");
      }
#ifdef INODE_CACHE_SUPPORTED
      InodeCache(config).drop();
#endif
      break;
    }

    case 'd': // --directory
      Util::setenv("CCACHE_DIR", arg);
      break;

    case 'h': // --help
      PRINT(stdout, USAGE_TEXT, CCACHE_NAME, CCACHE_NAME);
      exit(EXIT_SUCCESS);

    case 'k': // --get-config
      PRINT(stdout, "{}\n", config.get_string_value(arg));
      break;

    case 'F': { // --max-files
      auto files = util::value_or_throw<Error>(util::parse_unsigned(arg));
      config.set_value_in_file(config.primary_config_path(), "max_files", arg);
      if (files == 0) {
        PRINT_RAW(stdout, "Unset cache file limit\n");
      } else {
        PRINT(stdout, "Set cache file limit to {}\n", files);
      }
      break;
    }

    case 'M': { // --max-size
      uint64_t size = Util::parse_size(arg);
      config.set_value_in_file(config.primary_config_path(), "max_size", arg);
      if (size == 0) {
        PRINT_RAW(stdout, "Unset cache size limit\n");
      } else {
        PRINT(stdout,
              "Set cache size limit to {}\n",
              Util::format_human_readable_size(size));
      }
      break;
    }

    case 'o': { // --set-config
      // Start searching for equal sign at position 1 to improve error message
      // for the -o=K=V case (key "=K" and value "V").
      size_t eq_pos = arg.find('=', 1);
      if (eq_pos == std::string::npos) {
        throw Error("missing equal sign in \"{}\"", arg);
      }
      std::string key = arg.substr(0, eq_pos);
      std::string value = arg.substr(eq_pos + 1);
      config.set_value_in_file(config.primary_config_path(), key, value);
      break;
    }

    case 'p': // --show-config
      config.visit_items(configuration_printer);
      break;

    case SHOW_LOG_STATS: {
      if (config.stats_log().empty()) {
        throw Fatal("No stats log has been configured");
      }
      PRINT(stdout, "{:36}{}\n", "stats log", config.stats_log());
      Statistics statistics(StatsLog(config.stats_log()).read());
      const auto timestamp =
        Stat::stat(config.stats_log(), Stat::OnError::log).mtime();
      PRINT_RAW(stdout, statistics.format_human_readable(timestamp, true));
      break;
    }

    case 's': { // --show-stats
      StatisticsCounters counters;
      time_t last_updated;
      std::tie(counters, last_updated) =
        storage::primary::PrimaryStorage(config).get_all_statistics();
      Statistics statistics(counters);
      PRINT_RAW(stdout, statistics.format_config_header(config));
      PRINT_RAW(stdout, statistics.format_human_readable(last_updated, false));
      PRINT_RAW(stdout, statistics.format_config_footer(config));
      break;
    }

    case 'V': // --version
      PRINT_RAW(stdout, get_version_text());
      exit(EXIT_SUCCESS);

    case 'x': // --show-compression
    {
      ProgressBar progress_bar("Scanning...");
      const auto compression_statistics =
        storage::primary::PrimaryStorage(config).get_compression_statistics(
          [&](double progress) { progress_bar.update(progress); });
      if (isatty(STDOUT_FILENO)) {
        PRINT_RAW(stdout, "\n\n");
      }
      print_compression_statistics(compression_statistics);
      break;
    }

    case 'X': // --recompress
    {
      nonstd::optional<int8_t> wanted_level;
      if (arg == "uncompressed") {
        wanted_level = nonstd::nullopt;
      } else {
        wanted_level = util::value_or_throw<Error>(
          util::parse_signed(arg, INT8_MIN, INT8_MAX, "compression level"));
      }

      ProgressBar progress_bar("Recompressing...");
      storage::primary::PrimaryStorage(config).recompress(
        wanted_level, [&](double progress) { progress_bar.update(progress); });
      break;
    }

    case 'z': // --zero-stats
      storage::primary::PrimaryStorage(config).zero_all_statistics();
      PRINT_RAW(stdout, "Statistics zeroed\n");
      break;

    default:
      PRINT(stderr, USAGE_TEXT, CCACHE_NAME, CCACHE_NAME);
      exit(EXIT_FAILURE);
    }
  }

  return 0;
}

} // namespace core
