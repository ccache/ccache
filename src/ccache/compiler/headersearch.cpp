// Copyright (C) 2026 Joel Rosdahl and other contributors
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

#include "headersearch.hpp"

#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <set>
#include <unordered_map>

namespace fs = std::filesystem;

namespace compiler {

namespace {

// See gcc/incpath.cc in GCC and lib/Lex/InitHeaderSearch.cpp in Clang.
const std::string_view k_quote_marker = "#include \"...\" search starts here:";
const std::string_view k_angle_marker = "#include <...> search starts here:";
const std::string_view k_end_marker = "End of search list.";
const std::string_view k_embed_marker = "#embed <...> search starts here:";
const std::string_view k_embed_end_marker = "End of #embed search list.";
const std::string_view k_nonexistent_prefix =
  "ignoring nonexistent directory \"";
const std::string_view k_duplicate_prefix = "ignoring duplicate directory \"";
// Printed on the line after "ignoring duplicate directory" when a user
// directory duplicates a built-in system directory.
const std::string_view k_duplicate_reason =
  "  as it is a non-system directory that duplicates a system directory";
// Directories in the search list are printed with a single space of
// indentation.
const std::string_view k_search_dir_prefix = " ";
const std::string_view k_framework_suffix = " (framework directory)";
const std::string_view k_headermap_suffix = " (headermap)";

enum class Section : uint8_t { outside, quote, angle, embed };

std::string_view
strip_line_ending(std::string_view line)
{
  while (line.ends_with('\n') || line.ends_with('\r')) {
    line.remove_suffix(1);
  }
  return line;
}

// Return false if `dir` can't be represented as a path.
bool
add_dir(Dirs& dirs, std::string_view dir)
{
  if (dir.empty()) {
    return true;
  }
  try {
    dirs.emplace_back(std::string(dir));
    return true;
  } catch (const fs::filesystem_error&) {
    return false;
  }
}

} // namespace

HeaderSearchOutput
parse_header_search_output(std::string_view stderr_data)
{
  HeaderSearchOutput output;
  output.remaining_stderr.reserve(stderr_data.size());

  HeaderSearchPaths paths;
  bool valid_paths = true;
  Section section = Section::outside;
  bool previous_was_duplicate = false;

  size_t pos = 0;
  while (pos < stderr_data.size()) {
    const size_t newline = stderr_data.find('\n', pos);
    const size_t line_end =
      newline == std::string_view::npos ? stderr_data.size() : newline + 1;
    const std::string_view raw_line = stderr_data.substr(pos, line_end - pos);
    const std::string_view line = strip_line_ending(raw_line);
    pos = line_end;

    const bool follows_duplicate = previous_was_duplicate;
    previous_was_duplicate = line.starts_with(k_duplicate_prefix);

    bool report_line = true;
    if (line == k_quote_marker) {
      section = Section::quote;
    } else if (line == k_angle_marker) {
      section = Section::angle;
    } else if (line == k_embed_marker) {
      section = Section::embed;
    } else if (line == k_end_marker || line == k_embed_end_marker) {
      section = Section::outside;
    } else if (section != Section::outside
               && line.starts_with(k_search_dir_prefix)) {
      std::string_view dir = line.substr(k_search_dir_prefix.size());
      if (section != Section::embed && !dir.ends_with(k_headermap_suffix)) {
        if (dir.ends_with(k_framework_suffix)) {
          dir.remove_suffix(k_framework_suffix.size());
        }
        auto& dirs =
          section == Section::quote ? paths.quote_dirs : paths.angle_dirs;
        valid_paths = add_dir(dirs, dir) && valid_paths;
      }
    } else {
      // Directories in the search list are indented; anything else means that
      // the list has ended (also if the end marker is missing).
      section = Section::outside;
      if (line.starts_with(k_nonexistent_prefix) && line.ends_with('"')
          && line.size() > k_nonexistent_prefix.size()) {
        const std::string_view dir =
          line.substr(k_nonexistent_prefix.size(),
                      line.size() - k_nonexistent_prefix.size() - 1);
        valid_paths = add_dir(paths.nonexistent_dirs, dir) && valid_paths;
      } else {
        report_line = previous_was_duplicate
                      || (follows_duplicate && line == k_duplicate_reason);
      }
    }

    if (!report_line) {
      output.remaining_stderr.append(raw_line);
    }
  }

  // Only trust the report if every directory in it decoded; otherwise fall
  // back to plain direct mode for this compilation.
  if (valid_paths) {
    output.paths = std::move(paths);
  }

  return output;
}

HasIncludeOperands
find_has_include_operands(std::string_view source)
{
  static constexpr std::string_view has_include = "__has_include";
  static constexpr std::string_view next = "_next";
  static constexpr std::string_view space = " \t\\\r\n";

  auto is_identifier_char = [](char c) {
    return c == '_' || util::is_alnum(c);
  };

  HasIncludeOperands operands;
  for (size_t pos = source.find(has_include); pos != std::string_view::npos;
       pos = source.find(has_include, pos + 1)) {
    if (pos > 0 && is_identifier_char(source[pos - 1])) {
      continue;
    }
    size_t p = pos + has_include.size();
    if (source.substr(p, next.size()) == next) {
      p += next.size();
    }
    if (p < source.size() && is_identifier_char(source[p])) {
      continue;
    }
    // Only __has_include followed by "(" is a lookup; "#ifdef __has_include"
    // and "defined(__has_include)" are not.
    p = source.find_first_not_of(space, p);
    if (p == std::string_view::npos || source[p] != '(') {
      continue;
    }
    p = source.find_first_not_of(space, p + 1);
    if (p == std::string_view::npos || (source[p] != '<' && source[p] != '"')) {
      operands.macro_operand = true;
      continue;
    }
    const bool quoted = source[p] == '"';
    const size_t end = source.find(quoted ? '"' : '>', p + 1);
    if (end == std::string_view::npos || end > source.find('\n', p + 1)) {
      continue;
    }
    operands.literals.push_back(
      {std::string(source.substr(p + 1, end - p - 1)), quoted});
  }
  return operands;
}

namespace {

using StatFn = std::function<PathKind(const std::filesystem::path&)>;
using CanonicalFn =
  std::function<std::filesystem::path(const std::filesystem::path&)>;

// A search directory in three forms:
//
// - as_printed: exactly as the preprocessor printed it (possibly relative).
//   Recorded in the manifest so a relative directory tracks the compiler's
//   actual lookup and survives the tree being moved or a symlink repointed.
// - absolute: as_printed resolved against cwd and normalized, but with symlinks
//   left intact. Used to match an included header against this directory when
//   the header was printed with symlinks intact (clang, and gcc for -I/quoted
//   includes).
// - canonical: absolute with symlinks resolved. Used to match when the header
//   was printed with symlinks resolved (gcc reports system headers found via a
//   symlinked search directory by their real path, which does not lexically
//   start with the directory as printed) and to tell two directories that
//   resolve to the same location apart. Only directories are canonicalized,
//   never the headers: resolving a header that is itself a symlink would point
//   it outside its search directory, and there are far more headers than
//   directories.
struct Dir
{
  fs::path as_printed;
  fs::path absolute;
  fs::path canonical;
};

// Resolves and caches the path forms needed for shadow-path computation.
// `stat` and `canonical` are the injected filesystem operations.
class PathResolver
{
public:
  PathResolver(fs::path cwd, StatFn stat, CanonicalFn canonical)
    : m_cwd(std::move(cwd)),
      m_stat(std::move(stat)),
      m_canonical(std::move(canonical))
  {
  }

  fs::path
  absolute(const fs::path& path) const
  {
    return util::lexically_normal(path.is_absolute() ? path : m_cwd / path);
  }

  PathKind
  kind(const fs::path& path)
  {
    const std::string key = util::pstr(path).str();
    auto it = m_stat_cache.find(key);
    if (it == m_stat_cache.end()) {
      it = m_stat_cache.emplace(key, m_stat(path)).first;
    }
    return it->second;
  }

  bool
  exists(const fs::path& path)
  {
    return kind(path) != PathKind::missing;
  }

  const Dir&
  dir_for(const fs::path& dir)
  {
    const std::string key = util::pstr(dir).str();
    auto it = m_dir_cache.find(key);
    if (it == m_dir_cache.end()) {
      const fs::path absolute_dir = absolute(dir);
      it = m_dir_cache
             .emplace(key,
                      Dir{.as_printed = util::lexically_normal(dir),
                          .absolute = absolute_dir,
                          .canonical =
                            util::lexically_normal(m_canonical(absolute_dir))})
             .first;
    }
    return it->second;
  }

private:
  fs::path m_cwd;
  StatFn m_stat;
  CanonicalFn m_canonical;
  std::unordered_map<std::string, PathKind> m_stat_cache;
  std::unordered_map<std::string, Dir> m_dir_cache;
};

// Like util::path_starts_with but without normalizing on Windows, so that a
// ".." component in the printed include path is matched literally. `prefix` is
// expected to be normalized (no trailing separator).
bool
starts_with_literally(const fs::path& path, const fs::path& prefix)
{
  return std::mismatch(path.begin(),
                       path.end(),
                       prefix.begin(),
                       prefix.end(),
                       util::path_components_equal_case_aware)
           .second
         == prefix.end();
}

// Return `file` relative to `dir` if it's inside `dir`. The path as printed is
// tried before the normalized one so that ".." in #include "../foo.h" keeps the
// association with the search directory, and the canonical directory is tried
// since GCC prints system header paths with symlinks resolved.
std::optional<fs::path>
relative_to(const Dir& dir, const fs::path& printed, const fs::path& normalized)
{
  for (const fs::path* file : {&printed, &normalized}) {
    for (const fs::path* d : {&dir.absolute, &dir.canonical}) {
      if (*file != *d && starts_with_literally(*file, *d)) {
        return file->lexically_relative(*d);
      }
    }
  }
  return std::nullopt;
}

// Record the first missing component of `relative` below `dir` in `result`:
// nothing below a missing directory can appear without the directory appearing
// first.
void
add_shadow_path(std::set<std::string>& result,
                PathResolver& resolver,
                const Dir& dir,
                const fs::path& relative)
{
  fs::path candidate = dir.as_printed;
  fs::path absolute_candidate = dir.absolute;
  for (const auto& component : relative) {
    candidate /= component;
    absolute_candidate /= component;
    if (!resolver.exists(util::lexically_normal(absolute_candidate))) {
      result.insert(util::pstr(util::lexically_normal(candidate)).str());
      return;
    }
  }
}

} // namespace

ShadowPaths
find_shadow_paths(const HeaderSearchPaths& paths,
                  const fs::path& cwd,
                  const std::vector<IncludedFile>& included_files,
                  const std::vector<HasIncludeProbe>& probes,
                  const StatFn& stat,
                  const CanonicalFn& canonical)
{
  PathResolver resolver(cwd, stat, canonical);

  std::vector<Dir> dirs;
  for (const auto* list : {&paths.quote_dirs, &paths.angle_dirs}) {
    for (const auto& dir : *list) {
      dirs.push_back(resolver.dir_for(dir));
    }
  }
  const size_t quote_dir_count = paths.quote_dirs.size();

  std::set<std::string> result;
  std::set<std::string> probed_files;

  // Clang also reports files given to -I as nonexistent directories.
  for (const auto& dir : paths.nonexistent_dirs) {
    if (!resolver.exists(resolver.absolute(dir))) {
      result.insert(util::pstr(util::lexically_normal(dir)).str());
    }
  }

  for (const auto& file : included_files) {
    const fs::path printed =
      file.path.is_absolute() ? file.path : cwd / file.path;
    const fs::path normalized = util::lexically_normal(printed);
    for (size_t i = 0; i < dirs.size(); ++i) {
      const auto relative = relative_to(dirs[i], printed, normalized);
      if (!relative) {
        continue;
      }
      // GCC uses foo.h.gch in a directory before foo.h in later ones, but also
      // foo.h in an earlier directory before foo.h.gch in a later one.
      std::vector<fs::path> spellings = {*relative};
      const auto extension = relative->extension();
      if (extension == ".gch" || extension == ".pch" || extension == ".pth") {
        spellings.push_back(relative->parent_path() / relative->stem());
      }
      for (const auto& spelling : spellings) {
        for (const auto& dir : file.includer_dirs) {
          add_shadow_path(result, resolver, resolver.dir_for(dir), spelling);
        }
        for (size_t j = 0; j < i; ++j) {
          if (dirs[j].canonical != dirs[i].canonical) {
            add_shadow_path(result, resolver, dirs[j], spelling);
          }
        }
      }
    }
  }

  // A probe searches the same directories as an include of the spelling from
  // the probing file: the file's directory and the quote directories (for
  // "...") and the angle directories. Directories before the first one where
  // the spelling exists become shadow paths.
  for (const auto& probe : probes) {
    const fs::path spelling(probe.spelling);
    std::vector<const Dir*> chain;
    if (probe.quoted) {
      const fs::path dir = probe.includer.parent_path();
      chain.push_back(&resolver.dir_for(dir.empty() ? fs::path(".") : dir));
      for (size_t i = 0; i < quote_dir_count; ++i) {
        chain.push_back(&dirs[i]);
      }
    }
    for (size_t i = quote_dir_count; i < dirs.size(); ++i) {
      chain.push_back(&dirs[i]);
    }
    for (const Dir* dir : chain) {
      if (resolver.kind(util::lexically_normal(dir->absolute / spelling))
          == PathKind::file) {
        probed_files.insert(
          util::pstr(util::lexically_normal(dir->as_printed / spelling)).str());
        break;
      }
      add_shadow_path(result, resolver, *dir, spelling);
    }
  }

  ShadowPaths shadow_paths;
  shadow_paths.paths.assign(result.begin(), result.end());
  shadow_paths.probed_files.assign(probed_files.begin(), probed_files.end());
  return shadow_paths;
}

} // namespace compiler
