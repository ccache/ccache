// Copyright (C) 2009-2026 Joel Rosdahl and other contributors
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

#include "hashutil.hpp"

#include <ccache/config.hpp>
#include <ccache/context.hpp>
#include <ccache/core/exceptions.hpp>
#include <ccache/execute.hpp>
#include <ccache/util/args.hpp>
#include <ccache/util/cpu.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/environment.hpp>
#include <ccache/util/exec.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/logging.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>
#include <ccache/util/time.hpp>

#ifdef INODE_CACHE_SUPPORTED
#  include <ccache/inodecache.hpp>
#endif

#ifdef HAVE_AVX2
#  include <immintrin.h>
#endif

#include <cctype>
#include <cstring>

namespace fs = util::filesystem;

namespace {

const char*
skip_horizontal_whitespace(const char* p, const char* end)
{
  while (p < end && (*p == ' ' || *p == '\t')) {
    ++p;
  }
  return p;
}

const char*
skip_line_continuation(const char* p, const char* end)
{
  if (p < end && end - p >= 2 && *p == '\\' && (p[1] == '\n' || p[1] == '\r')) {
    p += 2;
    if (p < end && p[-1] == '\r' && *p == '\n') {
      ++p;
    }
  }
  return p;
}

const char*
skip_whitespace_and_continuations(const char* p, const char* end)
{
  while (p < end) {
    const char* previous = p;
    p = skip_horizontal_whitespace(p, end);
    p = skip_line_continuation(p, end);
    if (p == previous) {
      break;
    }
  }
  return p;
}

// Pre-condition: str[pos - 1] == '_'
SourceCodeScan
check_for_temporal_macros(std::string_view str, size_t pos)
{
  if (pos + 7 > str.length()) {
    return SourceCodeScan::none;
  }

  SourceCodeScan found = SourceCodeScan::none;
  int macro_len = 7;
  if (memcmp(&str[pos], "_DATE__", 7) == 0) {
    found = SourceCodeScan::found_date;
  } else if (memcmp(&str[pos], "_TIME__", 7) == 0) {
    found = SourceCodeScan::found_time;
  } else if (pos + 12 <= str.length()
             && memcmp(&str[pos], "_TIMESTAMP__", 12) == 0) {
    found = SourceCodeScan::found_timestamp;
    macro_len = 12;
  } else {
    return SourceCodeScan::none;
  }

  // Check char before and after macro to verify that the found macro isn't part
  // of another identifier.
  if ((pos == 1 || (str[pos - 2] != '_' && !util::is_alnum(str[pos - 2])))
      && (pos + macro_len == str.length()
          || (str[pos + macro_len] != '_'
              && !util::is_alnum(str[pos + macro_len])))) {
    return found;
  }

  return SourceCodeScan::none;
}

SourceCodeScan
check_for_embed_directive(std::string_view str, size_t pos)
{
  static constexpr char embed_data[] = {'e', 'm', 'b', 'e', 'd'};
  static constexpr size_t embed_size = std::size(embed_data);

  if (str[pos] != '#') {
    return SourceCodeScan::none;
  }

  const char* const begin = str.data();
  const char* const end = begin + str.size();
  const char* p = begin + pos + 1;
  p = skip_whitespace_and_continuations(p, end);

  if (static_cast<size_t>(end - p) < embed_size
      || std::memcmp(p, embed_data, embed_size) != 0) {
    return SourceCodeScan::none;
  }

  const char* after_embed = p + embed_size;
  if (after_embed < end
      && (std::isalnum(static_cast<unsigned char>(*after_embed))
          || *after_embed == '_')) {
    return SourceCodeScan::none;
  }

  // The resource identifier may be a macro that expands to a quoted or angle
  // bracket delimited filename, so it cannot be reliably parsed here.
  return SourceCodeScan::found_embed;
}

SourceCodeScan
check_for_incbin_directive(std::string_view str, size_t pos)
{
  static constexpr char incbin_data[] = {'.', 'i', 'n', 'c', 'b', 'i', 'n'};
  static constexpr size_t incbin_size = std::size(incbin_data);

  if (pos + incbin_size > str.size()
      || std::memcmp(&str[pos], incbin_data, incbin_size) != 0) {
    return SourceCodeScan::none;
  }

  const char* const begin = str.data();
  const char* const end = begin + str.size();
  const char* p = begin + pos + incbin_size;
  p = skip_horizontal_whitespace(p, end);

  return p < end && (*p == '"' || (*p == '\\' && p + 1 < end && p[1] == '"'))
           ? SourceCodeScan::found_incbin
           : SourceCodeScan::none;
}

void
check_for_source_code_patterns_scalar(std::string_view str,
                                      size_t start,
                                      SourceCodeScanResult& result)
{
  for (size_t i = start; i < str.size(); ++i) {
    if (str[i] == '_') {
      result.insert(check_for_temporal_macros(str, i + 1));
    }
    if (!result.contains(SourceCodeScan::found_embed) && str[i] == '#') {
      result.insert(check_for_embed_directive(str, i));
    }
    if (!result.contains(SourceCodeScan::found_incbin) && str[i] == '.') {
      result.insert(check_for_incbin_directive(str, i));
    }
  }
}

SourceCodeScanResult
check_for_source_code_patterns(std::string_view str)
{
#ifdef HAVE_AVX2
  if (util::cpu_supports_avx2()) {
    return check_for_source_code_patterns_avx2(str);
  }
#endif

  SourceCodeScanResult result;
  check_for_source_code_patterns_scalar(str, 0, result);
  return result;
}

std::optional<SourceCodeScanResult>
do_hash_file(const Context& ctx,
             Hash::Digest& digest,
             const fs::path& path,
             size_t size_hint,
             bool scan_source)
{
#ifdef INODE_CACHE_SUPPORTED
  InodeCache::ContentType content_type =
    scan_source
      ? InodeCache::ContentType::checked_for_temporal_macros_and_directives
      : InodeCache::ContentType::raw;

  if (ctx.config.inode_cache()) {
    const auto result = ctx.inode_cache.get(path, content_type);
    if (result) {
      digest = result->second;
      return result->first;
    }
  }
#else
  (void)ctx;
#endif

  const auto data = util::read_file<util::Bytes>(path, size_hint);
  if (!data) {
    LOG("Failed to read {}: {}", path, data.error());
    return std::nullopt;
  }

  auto str = util::to_string_view(*data);
  Hash hash;
  hash.hash(str);
  digest = hash.digest();

  SourceCodeScanResult result;
  if (scan_source) {
    result = check_for_source_code_patterns(str);
  }
#ifdef INODE_CACHE_SUPPORTED
  ctx.inode_cache.put(path, content_type, digest, result);
#endif

  return result;
}

} // namespace

#ifdef HAVE_AVX2
// The following is heavily inspired by
// <http://0x80.pl/articles/simd-strfind.html>.
#  ifndef _MSC_VER
__attribute__((target("avx2")))
#  endif
SourceCodeScanResult
check_for_source_code_patterns_avx2(std::string_view str)
{
  SourceCodeScanResult result;

  const __m256i underscore = _mm256_set1_epi8('_');
  const __m256i temporal_last = _mm256_set1_epi8('E');
  const __m256i hash_sign = _mm256_set1_epi8('#');
  const __m256i period = _mm256_set1_epi8('.');
  const __m256i incbin_last = _mm256_set1_epi8('n');

  size_t pos = 0;
  for (; pos + 32 <= str.length(); pos += 32) {
    const __m256i block =
      _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&str[pos]));

    uint32_t temporal_mask = 0;
    if (pos + 5 + 32 <= str.length()) {
      const __m256i block_last =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&str[pos + 5]));
      temporal_mask = _mm256_movemask_epi8(
        _mm256_and_si256(_mm256_cmpeq_epi8(underscore, block),
                         _mm256_cmpeq_epi8(temporal_last, block_last)));
    }

    uint32_t embed_mask = 0;
    if (!result.contains(SourceCodeScan::found_embed)) {
      embed_mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(hash_sign, block));
    }

    uint32_t incbin_mask = 0;
    if (!result.contains(SourceCodeScan::found_incbin)
        && pos + 6 + 32 <= str.length()) {
      const __m256i block_last =
        _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&str[pos + 6]));
      incbin_mask = _mm256_movemask_epi8(
        _mm256_and_si256(_mm256_cmpeq_epi8(period, block),
                         _mm256_cmpeq_epi8(incbin_last, block_last)));
    }

    while (temporal_mask != 0) {
#  ifndef _MSC_VER
      const auto start = pos + __builtin_ctz(temporal_mask) + 1;
#  else
      unsigned long index;
      _BitScanForward(&index, temporal_mask);
      const auto start = pos + index + 1;
#  endif
      temporal_mask &= temporal_mask - 1;
      result.insert(check_for_temporal_macros(str, start));
    }

    while (embed_mask != 0) {
#  ifndef _MSC_VER
      const auto start = pos + __builtin_ctz(embed_mask);
#  else
      unsigned long index;
      _BitScanForward(&index, embed_mask);
      const auto start = pos + index;
#  endif
      embed_mask &= embed_mask - 1;
      result.insert(check_for_embed_directive(str, start));
    }

    while (incbin_mask != 0) {
#  ifndef _MSC_VER
      const auto start = pos + __builtin_ctz(incbin_mask);
#  else
      unsigned long index;
      _BitScanForward(&index, incbin_mask);
      const auto start = pos + index;
#  endif
      incbin_mask &= incbin_mask - 1;
      result.insert(check_for_incbin_directive(str, start));
    }
  }

  const size_t fallback_start = pos >= 32 ? pos - 32 : 0;
  check_for_source_code_patterns_scalar(str, fallback_start, result);
  return result;
}
#endif

SourceCodeScanResult
check_for_source_code_patterns_scalar(std::string_view str)
{
  SourceCodeScanResult result;
  check_for_source_code_patterns_scalar(str, 0, result);
  return result;
}

std::optional<Hash::Digest>
hash_source_code_file(Context& ctx, const fs::path& path, size_t size_hint)
{
  Hash::Digest digest;
  auto opt_result = do_hash_file(ctx, digest, path, size_hint, true);
  if (!opt_result) {
    return std::nullopt;
  }
  auto& result = *opt_result;

  if (result.contains(SourceCodeScan::found_embed)) {
    LOG("Found #em{}bed in {}", "", path);
  }
  if (result.contains(SourceCodeScan::found_incbin)) {
    std::string_view suffix;
    if (ctx.config.sloppiness().contains(core::Sloppy::incbin)) {
      result.erase(SourceCodeScan::found_incbin);
      suffix = " (ignored)";
    }
    LOG("Found .inc{}bin in {}", "", path);
  }
  if (result.contains(SourceCodeScan::found_time)) {
    std::string_view suffix;
    if (ctx.config.sloppiness().contains(core::Sloppy::time_macros)) {
      result.erase(SourceCodeScan::found_time);
      suffix = " (ignored)";
    }
    LOG("Found __TI{}ME__ in {}{}", "", path, suffix);
  }
  if (result.contains(SourceCodeScan::found_date)) {
    std::string_view suffix;
    if (ctx.config.sloppiness().contains(core::Sloppy::time_macros)) {
      result.erase(SourceCodeScan::found_date);
      suffix = " (ignored)";
    }
    LOG("Found __DA{}TE__ in {}{}", "", path, suffix);
  }
  if (result.contains(SourceCodeScan::found_timestamp)) {
    std::string_view suffix;
    if (ctx.config.sloppiness().contains(core::Sloppy::time_macros)) {
      result.erase(SourceCodeScan::found_timestamp);
      suffix = " (ignored)";
    }
    LOG("Found __TIME{}STAMP__ in {}{}", "", path, suffix);
  }

  if (result.contains(SourceCodeScan::found_time)
      || result.contains(SourceCodeScan::found_embed)
      || result.contains(SourceCodeScan::found_incbin)) {
    LOG("Disabling direct mode");
    ctx.config.set_direct_mode(false);
    return digest;
  }

  const bool contains_temporal_macro =
    result.contains(SourceCodeScan::found_time)
    || result.contains(SourceCodeScan::found_date)
    || result.contains(SourceCodeScan::found_timestamp);
  if (!contains_temporal_macro) {
    return digest;
  }

  if (result.contains(SourceCodeScan::found_time)) {
    // We don't know for sure that the program actually uses the time macro, but
    // we have to assume it anyway and hash the time stamp. However, that's not
    // very useful since the chance that we get a cache hit later the same
    // second should be quite slim... So, just signal back to the caller that
    // the macro has been found so that the direct mode can be disabled.
    return digest;
  }

  // Date or timestamp found. We now make sure that the digest changes if the
  // (potential) expansion of those macros changes by computing a new digest
  // comprising the file digest and time information that represents the macro
  // expansions.

  Hash hash;
  hash.hash(util::format_legacy_digest(digest));

  if (result.contains(SourceCodeScan::found_date)) {
    hash.hash_delimiter("date");
    auto now = util::localtime();
    if (!now) {
      return std::nullopt;
    }
    hash.hash(now->tm_year);
    hash.hash(now->tm_mon);
    hash.hash(now->tm_mday);

    // If the compiler has support for it, the expansion of the date macro will
    // change according to the value of SOURCE_DATE_EPOCH. Note: We have to hash
    // both SOURCE_DATE_EPOCH and the current date since we can't be sure that
    // the compiler honors SOURCE_DATE_EPOCH.
    const auto source_date_epoch = getenv("SOURCE_DATE_EPOCH");
    if (source_date_epoch) {
      hash.hash(source_date_epoch);
    }
  }

  if (result.contains(SourceCodeScan::found_timestamp)) {
    util::DirEntry dir_entry(path);
    if (!dir_entry.is_regular_file()) {
      return std::nullopt;
    }

    auto modified_time = util::localtime(dir_entry.mtime());
    if (!modified_time) {
      return std::nullopt;
    }
    hash.hash_delimiter("timestamp");
    char timestamp[26];
    (void)strftime(timestamp, sizeof(timestamp), "%c", &*modified_time);
    hash.hash(timestamp);
  }

  return hash.digest();
}

std::optional<Hash::Digest>
hash_binary_file(const Context& ctx, const fs::path& path, size_t size_hint)
{
  Hash::Digest digest;
  if (!do_hash_file(ctx, digest, path, size_hint, false)) {
    return std::nullopt;
  }
  return digest;
}

bool
hash_binary_file(const Context& ctx, Hash& hash, const fs::path& path)
{
  const auto result = hash_binary_file(ctx, path);
  if (result) {
    hash.hash(util::format_legacy_digest(*result));
  }
  return result.has_value();
}

bool
hash_command_output(Hash& hash,
                    const std::string& command,
                    const std::string& compiler)
{
  util::Args args = util::Args::from_string(command);
#ifdef _WIN32
  // CreateProcess does not search PATH.
  auto full_path =
    find_executable_in_path(args[0], util::getenv_path_list("PATH")).string();
  if (!full_path.empty()) {
    args[0] = full_path;
  }
#endif

  for (size_t i = 0; i < args.size(); i++) {
    if (args[i] == "%compiler%") {
      args[i] = compiler;
    }
  }

  auto result = util::exec_to_string(args);
  if (!result) {
    LOG("Error executing compiler check command: {}", result.error());
    return false;
  }

  hash.hash(*result);
  return true;
}

bool
hash_multicommand_output(Hash& hash,
                         const std::string& command,
                         const std::string& compiler)
{
  for (const std::string& cmd : util::split_into_strings(command, ";")) {
    if (!hash_command_output(hash, cmd, compiler)) {
      return false;
    }
  }
  return true;
}
