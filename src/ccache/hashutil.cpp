// Copyright (C) 2009-2025 Joel Rosdahl and other contributors
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
#include <ccache/macroskip.hpp>
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

namespace fs = util::filesystem;

namespace {

// Pre-condition: str[pos - 1] == '_'
HashSourceCode
check_for_temporal_macros_helper(std::string_view str, size_t pos)
{
  if (pos + 7 > str.length()) {
    return HashSourceCode::ok;
  }

  HashSourceCode found = HashSourceCode::ok;
  int macro_len = 7;
  if (memcmp(&str[pos], "_DATE__", 7) == 0) {
    found = HashSourceCode::found_date;
  } else if (memcmp(&str[pos], "_TIME__", 7) == 0) {
    found = HashSourceCode::found_time;
  } else if (pos + 12 <= str.length()
             && memcmp(&str[pos], "_TIMESTAMP__", 12) == 0) {
    found = HashSourceCode::found_timestamp;
    macro_len = 12;
  } else {
    return HashSourceCode::ok;
  }

  // Check char before and after macro to verify that the found macro isn't part
  // of another identifier.
  if ((pos == 1 || (str[pos - 2] != '_' && !isalnum(str[pos - 2])))
      && (pos + macro_len == str.length()
          || (str[pos + macro_len] != '_' && !isalnum(str[pos + macro_len])))) {
    return found;
  }

  return HashSourceCode::ok;
}

HashSourceCodeResult
check_for_temporal_macros_bmh(std::string_view str, size_t start = 0)
{
  HashSourceCodeResult result;

  // We're using the Boyer-Moore-Horspool algorithm, which searches starting
  // from the *end* of the needle. Our needles are 8 characters long, so i
  // starts at 7.
  size_t i = start + 7;

  while (i < str.length()) {
    // Check whether the substring ending at str[i] has the form "_....E..". On
    // the assumption that 'E' is less common in source than '_', we check
    // str[i-2] first.
    if (str[i - 2] == 'E' && str[i - 7] == '_') {
      result.insert(check_for_temporal_macros_helper(str, i - 6));
    }

    // macro_skip tells us how far we can skip forward upon seeing str[i] at
    // the end of a substring.
    i += macro_skip[(uint8_t)str[i]];
  }

  return result;
}

#ifdef HAVE_AVX2
// The following algorithm, which uses AVX2 instructions to find __DATE__,
// __TIME__ and __TIMESTAMP__, is heavily inspired by
// <http://0x80.pl/articles/simd-strfind.html>.
#  ifndef _MSC_VER
__attribute__((target("avx2")))
#  endif
HashSourceCodeResult
check_for_temporal_macros_avx2(std::string_view str)
{
  HashSourceCodeResult result;

  // Set all 32 bytes in first and last to '_' and 'E' respectively.
  const __m256i first = _mm256_set1_epi8('_');
  const __m256i last = _mm256_set1_epi8('E');

  size_t pos = 0;
  for (; pos + 5 + 32 <= str.length(); pos += 32) {
    // Load 32 bytes from the current position in the input string, with
    // block_last being offset 5 bytes (i.e. the offset of 'E' in all three
    // macros).
    const __m256i block_first =
      _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&str[pos]));
    const __m256i block_last =
      _mm256_loadu_si256(reinterpret_cast<const __m256i*>(&str[pos + 5]));

    // For i in 0..31:
    //   eq_X[i] = 0xFF if X[i] == block_X[i] else 0
    const __m256i eq_first = _mm256_cmpeq_epi8(first, block_first);
    const __m256i eq_last = _mm256_cmpeq_epi8(last, block_last);

    // Set bit i in mask if byte i in both eq_first and eq_last has the most
    // significant bit set.
    uint32_t mask = _mm256_movemask_epi8(_mm256_and_si256(eq_first, eq_last));

    // A bit set in mask now indicates a possible location for a temporal macro.
    while (mask != 0) {
      // The start position + 1 (as we know the first char is _).
#  ifndef _MSC_VER
      const auto start = pos + __builtin_ctz(mask) + 1;
#  else
      unsigned long index;
      _BitScanForward(&index, mask);
      const auto start = pos + index + 1;
#  endif

      // Clear the least significant bit set.
      mask = mask & (mask - 1);

      result.insert(check_for_temporal_macros_helper(str, start));
    }
  }

  result.insert(check_for_temporal_macros_bmh(str, pos));

  return result;
}
#endif

HashSourceCodeResult
do_hash_file(const Context& ctx,
             Hash::Digest& digest,
             const fs::path& path,
             size_t size_hint,
             bool check_temporal_macros)
{
#ifdef INODE_CACHE_SUPPORTED
  const InodeCache::ContentType content_type =
    check_temporal_macros ? InodeCache::ContentType::checked_for_temporal_macros
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
    return HashSourceCodeResult(HashSourceCode::error);
  }

  HashSourceCodeResult result;
  if (check_temporal_macros) {
    result.insert(check_for_temporal_macros(util::to_string_view(*data)));
  }

  Hash hash;
  hash.hash(*data);
  digest = hash.digest();

#ifdef INODE_CACHE_SUPPORTED
  ctx.inode_cache.put(path, content_type, digest, result);
#endif

  return result;
}

} // namespace

HashSourceCodeResult
check_for_temporal_macros(std::string_view str)
{
#ifdef HAVE_AVX2
  if (util::cpu_supports_avx2()) {
    return check_for_temporal_macros_avx2(str);
  }
#endif
  return check_for_temporal_macros_bmh(str);
}

HashSourceCodeResult
hash_source_code_file(const Context& ctx,
                      Hash::Digest& digest,
                      const fs::path& path,
                      size_t size_hint)
{
  const bool check_temporal_macros =
    !ctx.config.sloppiness().contains(core::Sloppy::time_macros);
  auto result =
    do_hash_file(ctx, digest, path, size_hint, check_temporal_macros);

  if (!check_temporal_macros || result.empty()
      || result.contains(HashSourceCode::error)) {
    return result;
  }

  if (result.contains(HashSourceCode::found_time)) {
    // We don't know for sure that the program actually uses the __TIME__ macro,
    // but we have to assume it anyway and hash the time stamp. However, that's
    // not very useful since the chance that we get a cache hit later the same
    // second should be quite slim... So, just signal back to the caller that
    // __TIME__ has been found so that the direct mode can be disabled.
    LOG("Found __TIME__ in {}", path);
    return result;
  }

  // __DATE__ or __TIMESTAMP__ found. We now make sure that the digest changes
  // if the (potential) expansion of those macros changes by computing a new
  // digest comprising the file digest and time information that represents the
  // macro expansions.

  Hash hash;
  hash.hash(util::format_digest(digest));

  if (result.contains(HashSourceCode::found_date)) {
    LOG("Found __DATE__ in {}", path);

    hash.hash_delimiter("date");
    auto now = util::localtime();
    if (!now) {
      result.insert(HashSourceCode::error);
      return result;
    }
    hash.hash(now->tm_year);
    hash.hash(now->tm_mon);
    hash.hash(now->tm_mday);

    // If the compiler has support for it, the expansion of __DATE__ will change
    // according to the value of SOURCE_DATE_EPOCH. Note: We have to hash both
    // SOURCE_DATE_EPOCH and the current date since we can't be sure that the
    // compiler honors SOURCE_DATE_EPOCH.
    const auto source_date_epoch = getenv("SOURCE_DATE_EPOCH");
    if (source_date_epoch) {
      hash.hash(source_date_epoch);
    }
  }

  if (result.contains(HashSourceCode::found_timestamp)) {
    LOG("Found __TIMESTAMP__ in {}", path);

    util::DirEntry dir_entry(path);
    if (!dir_entry.is_regular_file()) {
      result.insert(HashSourceCode::error);
      return result;
    }

    auto modified_time = util::localtime(dir_entry.mtime());
    if (!modified_time) {
      result.insert(HashSourceCode::error);
      return result;
    }
    hash.hash_delimiter("timestamp");
    char timestamp[26];
    (void)strftime(timestamp, sizeof(timestamp), "%c", &*modified_time);
    hash.hash(timestamp);
  }

  digest = hash.digest();
  return result;
}

bool
hash_binary_file(const Context& ctx,
                 Hash::Digest& digest,
                 const fs::path& path,
                 size_t size_hint)
{
  return do_hash_file(ctx, digest, path, size_hint, false).empty();
}

bool
hash_binary_file(const Context& ctx, Hash& hash, const fs::path& path)
{
  Hash::Digest digest;
  const bool success = hash_binary_file(ctx, digest, path);
  if (success) {
    hash.hash(util::format_digest(digest));
  }
  return success;
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
