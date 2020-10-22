// Copyright (C) 2020 Joel Rosdahl and other contributors
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

#include "Statistics.hpp"

#include "AtomicFile.hpp"
#include "Config.hpp"
#include "Lockfile.hpp"
#include "Logging.hpp"
#include "Util.hpp"
#include "exceptions.hpp"
#include "fmtmacros.hpp"

const unsigned FLAG_NOZERO = 1; // don't zero with the -z option
const unsigned FLAG_ALWAYS = 2; // always show, even if zero
const unsigned FLAG_NEVER = 4;  // never show

using nonstd::nullopt;
using nonstd::optional;

// Returns a formatted version of a statistics value, or the empty string if the
// statistics line shouldn't be printed.
using FormatFunction = std::string (*)(uint64_t value);

static std::string
format_size(uint64_t size)
{
  return FMT("{:>11}", Util::format_human_readable_size(size));
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
    const auto tm = Util::localtime(timestamp);
    char buffer[100] = "?";
    if (tm) {
      strftime(buffer, sizeof(buffer), "%c", &*tm);
    }
    return std::string("    ") + buffer;
  } else {
    return {};
  }
}

static double
hit_rate(const Counters& counters)
{
  const uint64_t direct = counters.get(Statistic::direct_cache_hit);
  const uint64_t preprocessed = counters.get(Statistic::preprocessed_cache_hit);
  const uint64_t hit = direct + preprocessed;
  const uint64_t miss = counters.get(Statistic::cache_miss);
  const uint64_t total = hit + miss;
  return total > 0 ? (100.0 * hit) / total : 0.0;
}

static void
for_each_level_1_and_2_stats_file(
  const std::string& cache_dir,
  const std::function<void(const std::string& path)> function)
{
  for (size_t level_1 = 0; level_1 <= 0xF; ++level_1) {
    function(FMT("{}/{:x}/stats", cache_dir, level_1));
    for (size_t level_2 = 0; level_2 <= 0xF; ++level_2) {
      function(FMT("{}/{:x}/{:x}/stats", cache_dir, level_1, level_2));
    }
  }
}

static std::pair<Counters, time_t>
collect_counters(const Config& config)
{
  Counters counters;
  uint64_t zero_timestamp = 0;
  time_t last_updated = 0;

  // Add up the stats in each directory.
  for_each_level_1_and_2_stats_file(
    config.cache_dir(), [&](const std::string& path) {
      counters.set(Statistic::stats_zeroed_timestamp, 0); // Don't add
      counters.increment(Statistics::read(path));
      zero_timestamp = std::max(counters.get(Statistic::stats_zeroed_timestamp),
                                zero_timestamp);
      last_updated = std::max(last_updated, Stat::stat(path).mtime());
    });

  counters.set(Statistic::stats_zeroed_timestamp, zero_timestamp);
  return std::make_pair(counters, last_updated);
}

namespace {

struct StatisticsField
{
  StatisticsField(Statistic statistic_,
                  const char* id_,
                  const char* message_,
                  unsigned flags_ = 0,
                  FormatFunction format_ = nullptr)
    : statistic(statistic_),
      id(id_),
      message(message_),
      flags(flags_),
      format(format_)
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

namespace Statistics {

Counters
read(const std::string& path)
{
  Counters counters;

  std::string data;
  try {
    data = Util::read_file(path);
  } catch (const Error&) {
    // Ignore.
    return counters;
  }

  size_t i = 0;
  const char* str = data.c_str();
  while (true) {
    char* end;
    const uint64_t value = std::strtoull(str, &end, 10);
    if (end == str) {
      break;
    }
    counters.set_raw(i, value);
    ++i;
    str = end;
  }

  return counters;
}

optional<Counters>
update(const std::string& path,
       std::function<void(Counters& counters)> function)
{
  Lockfile lock(path);
  if (!lock.acquired()) {
    LOG("Failed to acquire lock for {}", path);
    return nullopt;
  }

  auto counters = Statistics::read(path);
  function(counters);

  AtomicFile file(path, AtomicFile::Mode::text);
  for (size_t i = 0; i < counters.size(); ++i) {
    file.write(FMT("{}\n", counters.get_raw(i)));
  }
  try {
    file.commit();
  } catch (const Error& e) {
    // Make failure to write a stats file a soft error since it's not
    // important enough to fail whole the process and also because it is
    // called in the Context destructor.
    LOG("Error: {}", e.what());
  }

  return counters;
}

optional<std::string>
get_result(const Counters& counters)
{
  for (const auto& field : k_statistics_fields) {
    if (counters.get(field.statistic) != 0 && !(field.flags & FLAG_NOZERO)) {
      return field.message;
    }
  }
  return nullopt;
}

void
zero_all_counters(const Config& config)
{
  const time_t timestamp = time(nullptr);

  for_each_level_1_and_2_stats_file(
    config.cache_dir(), [=](const std::string& path) {
      Statistics::update(path, [=](Counters& cs) {
        for (size_t i = 0; k_statistics_fields[i].message; ++i) {
          if (!(k_statistics_fields[i].flags & FLAG_NOZERO)) {
            cs.set(k_statistics_fields[i].statistic, 0);
          }
        }
        cs.set(Statistic::stats_zeroed_timestamp, timestamp);
      });
    });
}

std::string
format_human_readable(const Config& config)
{
  Counters counters;
  time_t last_updated;
  std::tie(counters, last_updated) = collect_counters(config);
  std::string result;

  result += FMT("{:36}{}\n", "cache directory", config.cache_dir());
  result += FMT("{:36}{}\n", "primary config", config.primary_config_path());
  result += FMT(
    "{:36}{}\n", "secondary config (readonly)", config.secondary_config_path());
  if (last_updated > 0) {
    const auto tm = Util::localtime(last_updated);
    char timestamp[100] = "?";
    if (tm) {
      strftime(timestamp, sizeof(timestamp), "%c", &*tm);
    }
    result += FMT("{:36}{}\n", "stats updated", timestamp);
  }

  // ...and display them.
  for (size_t i = 0; k_statistics_fields[i].message; i++) {
    const Statistic statistic = k_statistics_fields[i].statistic;

    if (k_statistics_fields[i].flags & FLAG_NEVER) {
      continue;
    }
    if (counters.get(statistic) == 0
        && !(k_statistics_fields[i].flags & FLAG_ALWAYS)) {
      continue;
    }

    const std::string value =
      k_statistics_fields[i].format
        ? k_statistics_fields[i].format(counters.get(statistic))
        : FMT("{:8}", counters.get(statistic));
    if (!value.empty()) {
      result += FMT("{:32}{}\n", k_statistics_fields[i].message, value);
    }

    if (statistic == Statistic::cache_miss) {
      double percent = hit_rate(counters);
      result += FMT("{:34}{:6.2f} %\n", "cache hit rate", percent);
    }
  }

  if (config.max_files() != 0) {
    result += FMT("{:32}{:8}\n", "max files", config.max_files());
  }
  if (config.max_size() != 0) {
    result +=
      FMT("{:32}{}\n", "max cache size", format_size(config.max_size()));
  }

  return result;
}

std::string
format_machine_readable(const Config& config)
{
  Counters counters;
  time_t last_updated;
  std::tie(counters, last_updated) = collect_counters(config);
  std::string result;

  result += FMT("stats_updated_timestamp\t{}\n", last_updated);

  for (size_t i = 0; k_statistics_fields[i].message; i++) {
    if (!(k_statistics_fields[i].flags & FLAG_NEVER)) {
      result += FMT("{}\t{}\n",
                    k_statistics_fields[i].id,
                    counters.get(k_statistics_fields[i].statistic));
    }
  }

  return result;
}

} // namespace Statistics
