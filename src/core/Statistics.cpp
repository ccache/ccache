// Copyright (C) 2021-2022 Joel Rosdahl and other contributors
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
#include <util/TextTable.hpp>
#include <util/string.hpp>

#include <algorithm>

namespace core {

using core::Statistic;

const unsigned FLAG_NOZERO = 1U << 0;      // don't zero with --zero-stats
const unsigned FLAG_NEVER = 1U << 1;       // don't include in --print-stats
const unsigned FLAG_ERROR = 1U << 2;       // include in error count
const unsigned FLAG_UNCACHEABLE = 1U << 3; // include in uncacheable count

namespace {

struct StatisticsField
{
  StatisticsField(const Statistic statistic_,
                  const char* const id_,
                  const char* const description_,
                  const unsigned flags_ = 0)
    : statistic(statistic_),
      id(id_),
      description(description_),
      flags(flags_)
  {
  }

  const Statistic statistic;
  const char* const id;          // for --print-stats
  const char* const description; // for --show-stats --verbose
  const unsigned flags;          // bitmask of FLAG_* values
};

} // namespace

#define FIELD(id, ...)                                                         \
  {                                                                            \
    Statistic::id, #id, __VA_ARGS__                                            \
  }

const StatisticsField k_statistics_fields[] = {
  // Field "none" intentionally omitted.
  FIELD(autoconf_test, "Autoconf compile/link", FLAG_UNCACHEABLE),
  FIELD(bad_compiler_arguments, "Bad compiler arguments", FLAG_UNCACHEABLE),
  FIELD(bad_output_file, "Could not write to output file", FLAG_ERROR),
  FIELD(cache_miss, nullptr),
  FIELD(cache_size_kibibyte, nullptr, FLAG_NOZERO),
  FIELD(called_for_link, "Called for linking", FLAG_UNCACHEABLE),
  FIELD(called_for_preprocessing, "Called for preprocessing", FLAG_UNCACHEABLE),
  FIELD(cleanups_performed, nullptr),
  FIELD(compile_failed, "Compilation failed", FLAG_UNCACHEABLE),
  FIELD(compiler_check_failed, "Compiler check failed", FLAG_ERROR),
  FIELD(compiler_produced_no_output,
        "Compiler output file missing",
        FLAG_UNCACHEABLE),
  FIELD(compiler_produced_empty_output,
        "Compiler produced empty output",
        FLAG_UNCACHEABLE),
  FIELD(compiler_produced_stdout, "Compiler produced stdout", FLAG_UNCACHEABLE),
  FIELD(could_not_find_compiler, "Could not find compiler", FLAG_ERROR),
  FIELD(could_not_use_modules, "Could not use modules", FLAG_UNCACHEABLE),
  FIELD(could_not_use_precompiled_header,
        "Could not use precompiled header",
        FLAG_UNCACHEABLE),
  FIELD(direct_cache_hit, nullptr),
  FIELD(direct_cache_miss, nullptr),
  FIELD(error_hashing_extra_file, "Error hashing extra file", FLAG_ERROR),
  FIELD(files_in_cache, nullptr, FLAG_NOZERO),
  FIELD(internal_error, "Internal error", FLAG_ERROR),
  FIELD(local_storage_hit, nullptr),
  FIELD(local_storage_miss, nullptr),
  FIELD(local_storage_read_hit, nullptr),
  FIELD(local_storage_read_miss, nullptr),
  FIELD(local_storage_write, nullptr),
  FIELD(missing_cache_file, "Missing cache file", FLAG_ERROR),
  FIELD(multiple_source_files, "Multiple source files", FLAG_UNCACHEABLE),
  FIELD(no_input_file, "No input file", FLAG_UNCACHEABLE),
  FIELD(obsolete_max_files, nullptr, FLAG_NOZERO | FLAG_NEVER),
  FIELD(obsolete_max_size, nullptr, FLAG_NOZERO | FLAG_NEVER),
  FIELD(output_to_stdout, "Output to stdout", FLAG_UNCACHEABLE),
  FIELD(preprocessed_cache_hit, nullptr),
  FIELD(preprocessed_cache_miss, nullptr),
  FIELD(preprocessor_error, "Preprocessing failed", FLAG_UNCACHEABLE),
  FIELD(recache, "Forced recache", FLAG_UNCACHEABLE),
  FIELD(remote_storage_error, nullptr),
  FIELD(remote_storage_hit, nullptr),
  FIELD(remote_storage_miss, nullptr),
  FIELD(remote_storage_read_hit, nullptr),
  FIELD(remote_storage_read_miss, nullptr),
  FIELD(remote_storage_write, nullptr),
  FIELD(remote_storage_timeout, nullptr),
  FIELD(stats_zeroed_timestamp, nullptr),
  FIELD(
    unsupported_code_directive, "Unsupported code directive", FLAG_UNCACHEABLE),
  FIELD(unsupported_compiler_option,
        "Unsupported compiler option",
        FLAG_UNCACHEABLE),
  FIELD(unsupported_environment_variable,
        "Unsupported environment variable",
        FLAG_UNCACHEABLE),
  FIELD(unsupported_source_language,
        "Unsupported source language",
        FLAG_UNCACHEABLE),
};

static_assert(sizeof(k_statistics_fields) / sizeof(k_statistics_fields[0])
              == static_cast<size_t>(Statistic::END) - 1);

static std::string
format_timestamp(const util::TimePoint& value)
{
  if (value.sec() == 0) {
    return "never";
  } else {
    const auto tm = Util::localtime(value);
    char buffer[100] = "?";
    if (tm) {
      strftime(buffer, sizeof(buffer), "%c", &*tm);
    }
    return buffer;
  }
}

static std::string
percent(const uint64_t nominator, const uint64_t denominator)
{
  if (denominator == 0) {
    return "";
  } else if (nominator >= denominator) {
    return FMT("({:5.1f}%)", (100.0 * nominator) / denominator);
  } else {
    return FMT("({:5.2f}%)", (100.0 * nominator) / denominator);
  }
}

Statistics::Statistics(const StatisticsCounters& counters)
  : m_counters(counters)
{
}

std::vector<std::string>
Statistics::get_statistics_ids() const
{
  std::vector<std::string> result;
  for (const auto& field : k_statistics_fields) {
    if (!(field.flags & FLAG_NOZERO)) {
      for (size_t i = 0; i < m_counters.get(field.statistic); ++i) {
        result.emplace_back(field.id);
      }
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

uint64_t
Statistics::count_stats(const unsigned flags) const
{
  uint64_t sum = 0;
  for (const auto& field : k_statistics_fields) {
    if (field.flags & flags) {
      sum += m_counters.get(field.statistic);
    }
  }
  return sum;
}

std::vector<std::pair<std::string, uint64_t>>
Statistics::get_stats(unsigned flags, const bool all) const
{
  std::vector<std::pair<std::string, uint64_t>> result;
  for (const auto& field : k_statistics_fields) {
    const auto count = m_counters.get(field.statistic);
    if ((field.flags & flags) && (all || count > 0)) {
      result.emplace_back(field.description, count);
    }
  }
  return result;
}

static void
add_ratio_row(util::TextTable& table,
              const std::string& text,
              const uint64_t nominator,
              const uint64_t denominator)
{
  if (denominator > 0) {
    table.add_row({
      text,
      nominator,
      "/",
      denominator,
      percent(nominator, denominator),
    });
  } else {
    table.add_row({text, nominator});
  }
}

std::string
Statistics::format_human_readable(const Config& config,
                                  const util::TimePoint& last_updated,
                                  const uint8_t verbosity,
                                  const bool from_log) const
{
  util::TextTable table;
  using C = util::TextTable::Cell;

#define S(x_) m_counters.get(Statistic::x_)

  const uint64_t d_hits = S(direct_cache_hit);
  const uint64_t d_misses = S(direct_cache_miss);
  const uint64_t p_hits = S(preprocessed_cache_hit);
  const uint64_t p_misses = S(preprocessed_cache_miss);
  const uint64_t hits = d_hits + p_hits;
  const uint64_t misses = S(cache_miss);
  const uint64_t uncacheable = count_stats(FLAG_UNCACHEABLE);
  const uint64_t errors = count_stats(FLAG_ERROR);
  const uint64_t total_calls = hits + misses + errors + uncacheable;

  auto cmp_fn = [](const auto& e1, const auto& e2) {
    return e1.first.compare(e2.first) < 0;
  };

  if (verbosity > 0 && !from_log) {
    table.add_row({"Cache directory:", C(config.cache_dir()).colspan(4)});
    table.add_row({"Config file:", C(config.config_path()).colspan(4)});
    table.add_row(
      {"System config file:", C(config.system_config_path()).colspan(4)});
    table.add_row(
      {"Stats updated:", C(format_timestamp(last_updated)).colspan(4)});
    if (verbosity > 1) {
      const util::TimePoint last_zeroed(S(stats_zeroed_timestamp));
      table.add_row(
        {"Stats zeroed:", C(format_timestamp(last_zeroed)).colspan(4)});
    }
  }

  if (total_calls > 0 || verbosity > 1) {
    add_ratio_row(table, "Cacheable calls:", hits + misses, total_calls);
    add_ratio_row(table, "  Hits:", hits, hits + misses);
    add_ratio_row(table, "    Direct:", d_hits, hits);
    add_ratio_row(table, "    Preprocessed:", p_hits, hits);
    add_ratio_row(table, "  Misses:", misses, hits + misses);
  }

  if (uncacheable > 0 || verbosity > 1) {
    add_ratio_row(table, "Uncacheable calls:", uncacheable, total_calls);
    if (verbosity > 0) {
      auto uncacheable_stats = get_stats(FLAG_UNCACHEABLE, verbosity > 1);
      std::sort(uncacheable_stats.begin(), uncacheable_stats.end(), cmp_fn);
      for (const auto& [name, value] : uncacheable_stats) {
        add_ratio_row(table, FMT("  {}:", name), value, uncacheable);
      }
    }
  }

  if (errors > 0 || verbosity > 1) {
    add_ratio_row(table, "Errors:", errors, total_calls);
    if (verbosity > 0) {
      auto error_stats = get_stats(FLAG_ERROR, verbosity > 1);
      std::sort(error_stats.begin(), error_stats.end(), cmp_fn);
      for (const auto& [name, value] : error_stats) {
        add_ratio_row(table, FMT("  {}:", name), value, errors);
      }
    }
  }

  if (total_calls > 0 && verbosity > 0) {
    table.add_heading("Successful lookups:");
    add_ratio_row(table, "  Direct:", d_hits, d_hits + d_misses);
    add_ratio_row(table, "  Preprocessed:", p_hits, p_hits + p_misses);
  }

  const uint64_t g = 1'000'000'000;
  const uint64_t local_hits = S(local_storage_hit);
  const uint64_t local_misses = S(local_storage_miss);
  const uint64_t local_reads =
    S(local_storage_read_hit) + S(local_storage_read_miss);
  const uint64_t local_writes = S(local_storage_write);
  const uint64_t local_size = S(cache_size_kibibyte) * 1024;
  const uint64_t cleanups = S(cleanups_performed);
  const uint64_t remote_hits = S(remote_storage_hit);
  const uint64_t remote_misses = S(remote_storage_miss);
  const uint64_t remote_reads =
    S(remote_storage_read_hit) + S(remote_storage_read_miss);
  const uint64_t remote_writes = S(remote_storage_write);
  const uint64_t remote_errors = S(remote_storage_error);
  const uint64_t remote_timeouts = S(remote_storage_timeout);

  table.add_heading("Local storage:");
  if (!from_log) {
    std::vector<C> size_cells{
      "  Cache size (GB):",
      C(FMT("{:.2f}", static_cast<double>(local_size) / g)).right_align()};
    if (config.max_size() != 0) {
      size_cells.emplace_back("/");
      size_cells.emplace_back(
        C(FMT("{:.2f}", static_cast<double>(config.max_size()) / g))
          .right_align());
      size_cells.emplace_back(percent(local_size, config.max_size()));
    }
    table.add_row(size_cells);

    if (verbosity > 0) {
      std::vector<C> files_cells{"  Files:", S(files_in_cache)};
      if (config.max_files() > 0) {
        files_cells.emplace_back("/");
        files_cells.emplace_back(config.max_files());
        files_cells.emplace_back(
          percent(S(files_in_cache), config.max_files()));
      }
      table.add_row(files_cells);
    }
    if (cleanups > 0 || verbosity > 1) {
      table.add_row({"  Cleanups:", cleanups});
    }
  }
  if (verbosity > 0 || (remote_hits + remote_misses) > 0) {
    add_ratio_row(table, "  Hits:", local_hits, local_hits + local_misses);
    add_ratio_row(table, "  Misses:", local_misses, local_hits + local_misses);
  }
  if (verbosity > 0) {
    table.add_row({"  Reads:", local_reads});
    table.add_row({"  Writes:", local_writes});
  }

  if (verbosity > 1
      || remote_hits + remote_misses + remote_errors + remote_timeouts > 0) {
    table.add_heading("Remote storage:");
    add_ratio_row(table, "  Hits:", remote_hits, remote_hits + remote_misses);
    add_ratio_row(
      table, "  Misses:", remote_misses, remote_hits + remote_misses);
    if (verbosity > 0) {
      table.add_row({"  Reads:", remote_reads});
      table.add_row({"  Writes:", remote_writes});
    }
    if (verbosity > 1 || remote_errors > 0) {
      table.add_row({"  Errors:", remote_errors});
    }
    if (verbosity > 1 || remote_timeouts > 0) {
      table.add_row({"  Timeouts:", remote_timeouts});
    }
  }

  return table.render();
}

std::string
Statistics::format_machine_readable(const util::TimePoint& last_updated) const
{
  std::vector<std::string> lines;

  lines.push_back(FMT("stats_updated_timestamp\t{}\n", last_updated.sec()));

  for (const auto& field : k_statistics_fields) {
    if (!(field.flags & FLAG_NEVER)) {
      lines.push_back(
        FMT("{}\t{}\n", field.id, m_counters.get(field.statistic)));
    }
  }

  std::sort(lines.begin(), lines.end());
  return util::join(lines, "");
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
