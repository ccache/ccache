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

#include "Statistics.hpp"

#include <Config.hpp>
#include <Logging.hpp>
#include <Util.hpp>
#include <fmtmacros.hpp>

namespace core {

using core::Statistic;

// Returns a formatted version of a statistics value, or the empty string if the
// statistics line shouldn't be printed.
using FormatFunction = std::string (*)(uint64_t value);

static std::string format_size_times_1024(uint64_t value);
static std::string format_timestamp(uint64_t value);

const unsigned FLAG_NOZERO = 1;     // don't zero with the -z option
const unsigned FLAG_ALWAYS = 2;     // always show, even if zero
const unsigned FLAG_NEVER = 4;      // never show
const unsigned FLAG_NOSTATSLOG = 8; // don't show for statslog

namespace {

struct StatisticsField
{
  StatisticsField(const Statistic statistic_,
                  const char* const id_,
                  const char* const message_,
                  const unsigned flags_ = 0,
                  const FormatFunction format_ = nullptr)
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
  STATISTICS_FIELD(
    cleanups_performed, "cleanups performed", FLAG_NOSTATSLOG | FLAG_ALWAYS),
  STATISTICS_FIELD(files_in_cache,
                   "files in cache",
                   FLAG_NOZERO | FLAG_NOSTATSLOG | FLAG_ALWAYS),
  STATISTICS_FIELD(cache_size_kibibyte,
                   "cache size",
                   FLAG_NOZERO | FLAG_NOSTATSLOG | FLAG_ALWAYS,
                   format_size_times_1024),
  STATISTICS_FIELD(obsolete_max_files, "OBSOLETE", FLAG_NOZERO | FLAG_NEVER),
  STATISTICS_FIELD(obsolete_max_size, "OBSOLETE", FLAG_NOZERO | FLAG_NEVER),
  STATISTICS_FIELD(none, nullptr),
};

static std::string
format_size(const uint64_t value)
{
  return FMT("{:>11}", Util::format_human_readable_size(value));
}

static std::string
format_size_times_1024(const uint64_t value)
{
  return format_size(value * 1024);
}

static std::string
format_timestamp(const uint64_t value)
{
  if (value > 0) {
    const auto tm = Util::localtime(value);
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
hit_rate(const core::StatisticsCounters& counters)
{
  const uint64_t direct = counters.get(Statistic::direct_cache_hit);
  const uint64_t preprocessed = counters.get(Statistic::preprocessed_cache_hit);
  const uint64_t hit = direct + preprocessed;
  const uint64_t miss = counters.get(Statistic::cache_miss);
  const uint64_t total = hit + miss;
  return total > 0 ? (100.0 * hit) / total : 0.0;
}

Statistics::Statistics(const StatisticsCounters& counters)
  : m_counters(counters)
{
}

static const StatisticsField*
get_result(const core::StatisticsCounters& counters)
{
  for (const auto& field : k_statistics_fields) {
    if (counters.get(field.statistic) != 0 && !(field.flags & FLAG_NOZERO)) {
      return &field;
    }
  }
  return nullptr;
}

nonstd::optional<std::string>
Statistics::get_result_id() const
{
  const auto result = get_result(m_counters);
  if (result) {
    return result->id;
  }
  return nonstd::nullopt;
}

nonstd::optional<std::string>
Statistics::get_result_message() const
{
  const auto result = get_result(m_counters);
  if (result) {
    return result->message;
  }
  return nonstd::nullopt;
}

std::string
Statistics::format_config_header(const Config& config)
{
  std::string result;

  result += FMT("{:36}{}\n", "cache directory", config.cache_dir());
  result += FMT("{:36}{}\n", "primary config", config.primary_config_path());
  result += FMT(
    "{:36}{}\n", "secondary config (readonly)", config.secondary_config_path());

  return result;
}

std::string
Statistics::format_human_readable(const time_t last_updated,
                                  const bool from_log) const
{
  std::string result;

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
    if (m_counters.get(statistic) == 0
        && !(k_statistics_fields[i].flags & FLAG_ALWAYS)) {
      continue;
    }

    // don't show cache directory info if reading from a log
    if (from_log && (k_statistics_fields[i].flags & FLAG_NOSTATSLOG)) {
      continue;
    }

    const std::string value =
      k_statistics_fields[i].format
        ? k_statistics_fields[i].format(m_counters.get(statistic))
        : FMT("{:8}", m_counters.get(statistic));
    if (!value.empty()) {
      result += FMT("{:32}{}\n", k_statistics_fields[i].message, value);
    }

    if (statistic == Statistic::cache_miss) {
      double percent = hit_rate(m_counters);
      result += FMT("{:34}{:6.2f} %\n", "cache hit rate", percent);
    }
  }

  return result;
}

std::string
Statistics::format_config_footer(const Config& config)
{
  std::string result;

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
Statistics::format_machine_readable(const time_t last_updated) const
{
  std::string result;

  result += FMT("stats_updated_timestamp\t{}\n", last_updated);

  for (size_t i = 0; k_statistics_fields[i].message; i++) {
    if (!(k_statistics_fields[i].flags & FLAG_NEVER)) {
      result += FMT("{}\t{}\n",
                    k_statistics_fields[i].id,
                    m_counters.get(k_statistics_fields[i].statistic));
    }
  }

  return result;
}

std::unordered_map<std::string, Statistic>
Statistics::get_id_map()
{
  std::unordered_map<std::string, Statistic> result;
  for (const auto& field : k_statistics_fields) {
    result[field.id] = field.statistic;
  }
  return result;
}

std::vector<Statistic>
Statistics::get_zeroable_fields()
{
  std::vector<Statistic> result;
  for (const auto& field : k_statistics_fields) {
    if (!(field.flags & FLAG_NOZERO)) {
      result.push_back(field.statistic);
    }
  }
  return result;
}

} // namespace core
