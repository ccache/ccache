// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
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

#include "statistics.hpp"

#include <ccache/config.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/texttable.hpp>
#include <ccache/util/time.hpp>

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

  Statistic statistic;
  const char* id;          // for --print-stats
  const char* description; // for --show-stats --verbose
  unsigned flags;          // bitmask of FLAG_* values
};

} // namespace

#define FIELD(id, ...)                                                         \
  {                                                                            \
    Statistic::id, #id, __VA_ARGS__                                            \
  }

const StatisticsField k_statistics_fields[] = {
  // Field "none" intentionally omitted.

  // Uncacheable compilation or linking by an Autoconf test.
  FIELD(autoconf_test, "Autoconf compile/link", FLAG_UNCACHEABLE),

  // Malformed compiler argument, e.g. missing a value for a compiler option
  // that requires an argument or failure to read a file specified by a compiler
  // option argument.
  FIELD(bad_compiler_arguments, "Bad compiler arguments", FLAG_UNCACHEABLE),

  // An input file could not be read or parsed (see the debug log for details).
  FIELD(bad_input_file, "Could not read or parse input file", FLAG_ERROR),

  // The output path specified with -o could not be written to.
  FIELD(bad_output_file, "Could not write to output file", FLAG_ERROR),

  // A cacheable call resulted in a miss.
  FIELD(cache_miss, nullptr),

  // Size in KiB of a subdirectory of the cache. This is only set for level 1
  // subdirectories.
  FIELD(cache_size_kibibyte, nullptr, FLAG_NOZERO),

  // The compiler was called for linking, not compiling. Ccache only supports
  // compilation of a single file, i.e. calling the compiler with the -c option
  // to produce a single object file from a single source file.
  FIELD(called_for_link, "Called for linking", FLAG_UNCACHEABLE),

  // The compiler was called for preprocessing, not compiling.
  FIELD(called_for_preprocessing, "Called for preprocessing", FLAG_UNCACHEABLE),

  // How many cleanups were performed, either manually or automatically. Only
  // cleanup operations that actually removed files are counted.
  FIELD(cleanups_performed, nullptr),

  // The compilation failed. No result stored in the cache.
  FIELD(compile_failed, "Compilation failed", FLAG_UNCACHEABLE),

  // A compiler check program specified by compiler_check/CCACHE_COMPILERCHECK
  // failed.
  FIELD(compiler_check_failed, "Compiler check failed", FLAG_ERROR),

  // One of the files expected to be produced by the compiler was missing after
  // compilation.
  FIELD(compiler_produced_no_output,
        "Compiler output file missing",
        FLAG_UNCACHEABLE),

  // The compiler's output file (typically an object file) was empty after
  // compilation.
  FIELD(compiler_produced_empty_output,
        "Compiler produced empty output",
        FLAG_UNCACHEABLE),

  // Compiler produced output. [This field is obsolete since ccache now supports
  // caching stdout output as well.]
  FIELD(compiler_produced_stdout, "Compiler produced stdout", FLAG_UNCACHEABLE),

  // The compiler to execute could not be found.
  FIELD(could_not_find_compiler, "Could not find compiler", FLAG_ERROR),

  // Preconditions for using C++ modules were not fulfilled.
  FIELD(could_not_use_modules, "Could not use modules", FLAG_UNCACHEABLE),

  // Preconditions for using precompiled headers were not fulfilled.
  FIELD(could_not_use_precompiled_header,
        "Could not use precompiled header",
        FLAG_UNCACHEABLE),

  // A cacheable call resulted in a hit when attempting direct mode lookup.
  FIELD(direct_cache_hit, nullptr),

  // A cacheable call resulted in a miss when attempting direct mode lookup.
  FIELD(direct_cache_miss, nullptr),

  // Ccache was disabled by a comment in the source code file.
  FIELD(disabled, "Ccache disabled", FLAG_UNCACHEABLE),

  // Failure reading a file specified by extra_files_to_hash/CCACHE_EXTRAFILES.
  FIELD(error_hashing_extra_file, "Error hashing extra file", FLAG_ERROR),

  // Number of files in a subdirectory of the cache. This is only set for level
  // 1 subdirectories.
  FIELD(files_in_cache, nullptr, FLAG_NOZERO),

  // Unexpected failure, e.g. due to problems reading/writing the cache.
  FIELD(internal_error, "Internal error", FLAG_ERROR),

  // A cacheable call resulted in a hit when attempting to look up a result from
  // local storage.
  FIELD(local_storage_hit, nullptr),

  // A cacheable call resulted in a miss when attempting to look up a result
  // from local storage.
  FIELD(local_storage_miss, nullptr),

  // A read from local storage found an entry (manifest or result file).
  FIELD(local_storage_read_hit, nullptr),

  // A read from local storage did not find an entry (manifest or result file).
  FIELD(local_storage_read_miss, nullptr),

  // An entry (manifest or result file) was written local storage.
  FIELD(local_storage_write, nullptr),

  // A file was unexpectedly missing from the cache. This only happens in rare
  // situations, e.g. if one ccache instance is about to get a file from the
  // cache while another instance removed the file as part of cache cleanup.
  FIELD(missing_cache_file, "Missing cache file", FLAG_ERROR),

  // An input file was modified during compilation.
  FIELD(
    modified_input_file, "Input file modified during compilation", FLAG_ERROR),

  // The compiler was called to compile multiple source files in one go. This is
  // not supported by ccache.
  FIELD(multiple_source_files, "Multiple source files", FLAG_UNCACHEABLE),

  // No input file was specified to the compiler.
  FIELD(no_input_file, "No input file", FLAG_UNCACHEABLE),

  // [Obsolete field used before ccache 3.2.]
  FIELD(obsolete_max_files, nullptr, FLAG_NOZERO | FLAG_NEVER),

  // [Obsolete field used before ccache 3.2.]
  FIELD(obsolete_max_size, nullptr, FLAG_NOZERO | FLAG_NEVER),

  // The compiler was instructed to write its output to standard output using
  // "-o -". This is not supported by ccache.
  FIELD(output_to_stdout, "Output to stdout", FLAG_UNCACHEABLE),

  // A cacheable call resulted in a hit when attempting preprocessed mode
  // lookup.
  FIELD(preprocessed_cache_hit, nullptr),

  // A cacheable call resulted in a miss when attempting preprocessed mode
  // lookup.
  FIELD(preprocessed_cache_miss, nullptr),

  // Preprocessing the source code using the compiler's -E option failed.
  FIELD(preprocessor_error, "Preprocessing failed", FLAG_UNCACHEABLE),

  // recache/CCACHE_RECACHE was used to overwrite an existing result.
  FIELD(recache, "Forced recache", FLAG_UNCACHEABLE),

  // Error when connecting to, reading from or writing to remote storage.
  FIELD(remote_storage_error, nullptr),

  // A cacheable call resulted in a hit when attempting to look up a result from
  // remote storage.
  FIELD(remote_storage_hit, nullptr),

  // A cacheable call resulted in a miss when attempting to look up a result
  // from remote storage.
  FIELD(remote_storage_miss, nullptr),

  // A read from remote storage found an entry (manifest or result file).
  FIELD(remote_storage_read_hit, nullptr),

  // A read from remote storage did not find an entry (manifest or result file).
  FIELD(remote_storage_read_miss, nullptr),

  // An entry (manifest or result file) was written remote storage.
  FIELD(remote_storage_write, nullptr),

  // Timeout when connecting to, reading from or writing to remote storage.
  FIELD(remote_storage_timeout, nullptr),

  // Last time statistics counters were zeroed.
  FIELD(stats_zeroed_timestamp, nullptr),

  // Code like the assembler .inc bin (without the space) directive was found.
  // This is not supported by ccache.
  FIELD(
    unsupported_code_directive, "Unsupported code directive", FLAG_UNCACHEABLE),

  // A compiler option not supported by ccache was found.
  FIELD(unsupported_compiler_option,
        "Unsupported compiler option",
        FLAG_UNCACHEABLE),

  // An environment variable not supported by ccache was set.
  FIELD(unsupported_environment_variable,
        "Unsupported environment variable",
        FLAG_UNCACHEABLE),

  // Source file (or an included header) has unsupported encoding. ccache
  // currently requires UTF-8-encoded source code for MSVC.
  FIELD(unsupported_source_encoding,
        "Unsupported source encoding",
        FLAG_UNCACHEABLE),

  // A source language e.g. specified with -x was unsupported by ccache.
  FIELD(unsupported_source_language,
        "Unsupported source language",
        FLAG_UNCACHEABLE),

  // subdir_files_base and subdir_size_kibibyte_base are intentionally omitted
  // since they are not interesting to show.
};

static_assert(std::size(k_statistics_fields)
              == static_cast<size_t>(Statistic::END)
                   - (/*none*/ 1 + /*subdir files*/ 16 + /*subdir size*/ 16));

static std::string
format_timestamp(const util::TimePoint& value)
{
  if (util::sec(value) == 0) {
    return "never";
  } else {
    const auto tm = util::localtime(value);
    char buffer[100] = "?";
    if (tm) {
      (void)strftime(buffer, sizeof(buffer), "%c", &*tm);
    }
    return buffer;
  }
}

static std::string
percent(const uint64_t nominator, const uint64_t denominator)
{
  if (denominator == 0) {
    return "";
  }

  std::string result = FMT("({:5.2f}%)",
                           (100.0 * static_cast<double>(nominator))
                             / static_cast<double>(denominator));
  if (result.length() <= 8) {
    return result;
  } else {
    return FMT("({:5.1f}%)",
               (100.0 * static_cast<double>(nominator))
                 / static_cast<double>(denominator));
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
    table.add_row(
      {"Cache directory:", C(util::pstr(config.cache_dir())).colspan(4)});
    table.add_row(
      {"Config file:", C(util::pstr(config.config_path())).colspan(4)});
    table.add_row({"System config file:",
                   C(util::pstr(config.system_config_path())).colspan(4)});
    table.add_row(
      {"Stats updated:", C(format_timestamp(last_updated)).colspan(4)});
    if (verbosity > 1) {
      const util::TimePoint last_zeroed(
        std::chrono::seconds(S(stats_zeroed_timestamp)));
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

  const char* size_unit =
    config.size_unit_prefix_type() == util::SizeUnitPrefixType::binary ? "GiB"
                                                                       : "GB";
  const uint64_t size_divider =
    config.size_unit_prefix_type() == util::SizeUnitPrefixType::binary
      ? 1024 * 1024 * 1024
      : 1000 * 1000 * 1000;
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

  if (!from_log || verbosity > 0 || (local_hits + local_misses) > 0) {
    table.add_heading("Local storage:");
  }
  if (!from_log) {
    std::vector<C> size_cells{FMT("  Cache size ({}):", size_unit),
                              C(FMT("{:.1f}",
                                    static_cast<double>(local_size)
                                      / static_cast<double>(size_divider)))
                                .right_align()};
    if (config.max_size() != 0) {
      size_cells.emplace_back("/");
      size_cells.emplace_back(C(FMT("{:.1f}",
                                    static_cast<double>(config.max_size())
                                      / static_cast<double>(size_divider)))
                                .right_align());
      size_cells.emplace_back(percent(local_size, config.max_size()));
    }
    table.add_row(size_cells);

    if (verbosity > 0 || config.max_files() > 0) {
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
  if (verbosity > 0 || (local_hits + local_misses) > 0) {
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

std::vector<std::pair<std::string, uint64_t>>
Statistics::prepare_statistics_entries(
  const Config& config, const util::TimePoint& last_updated) const
{
  std::vector<std::pair<std::string, uint64_t>> result;

  for (const auto& field : k_statistics_fields) {
    if (!(field.flags & FLAG_NEVER)) {
      result.emplace_back(field.id, m_counters.get(field.statistic));
    }
  }

  result.emplace_back("max_cache_size_kibibyte", config.max_size() / 1024);
  result.emplace_back("max_files_in_cache", config.max_files());
  result.emplace_back("stats_updated_timestamp", util::sec(last_updated));

  std::sort(result.begin(), result.end());
  return result;
}

std::string
Statistics::format_machine_readable(const Config& config,
                                    const util::TimePoint& last_updated,
                                    StatisticsFormat format) const
{
  std::string result;
  const auto fields = prepare_statistics_entries(config, last_updated);

  switch (format) {
  case StatisticsFormat::Json:
    result = "{";
    for (const auto& [id, value] : fields) {
      result.append(FMT("\n  \"{}\": {},", id, value));
    }
    result.resize(result.length() - 1); // Remove trailing comma
    result += "\n}\n";
    break;
  case StatisticsFormat::Tab:
    for (const auto& [id, value] : fields) {
      result += FMT("{}\t{}\n", id, value);
    }
    break;
  default:
    ASSERT(false);
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
