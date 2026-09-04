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
const std::string_view k_duplicate_reason =
  "  as it is a non-system directory that duplicates a system directory";
const std::string_view k_clang_version_prefix = "clang -cc1 version ";
const std::string_view k_framework_suffix = " (framework directory)";
const std::string_view k_headermap_suffix = " (headermap)";

enum class Section { outside, quote, angle, embed };

std::string_view
strip_line_ending(std::string_view line)
{
  if (line.ends_with('\n')) {
    line.remove_suffix(1);
  }
  if (line.ends_with('\r')) {
    line.remove_suffix(1);
  }
  return line;
}

// Return false if `dir` can't be represented as a path.
bool
add_dir(std::vector<fs::path>& dirs, std::string_view dir)
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
  bool found_search_list = false;
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
      found_search_list = true;
    } else if (line == k_angle_marker) {
      section = Section::angle;
      found_search_list = true;
    } else if (line == k_embed_marker) {
      section = Section::embed;
    } else if (line == k_end_marker || line == k_embed_end_marker) {
      section = Section::outside;
    } else if (section != Section::outside && line.starts_with(' ')) {
      std::string_view dir = line.substr(1);
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
                      || (follows_duplicate && line == k_duplicate_reason)
                      || line.starts_with(k_clang_version_prefix);
      }
    }

    if (!report_line) {
      output.remaining_stderr.append(raw_line);
    }
  }

  if (found_search_list && valid_paths) {
    output.paths = std::move(paths);
  }

  return output;
}

std::vector<fs::path>
find_shadow_paths(const HeaderSearchPaths& paths,
                  const fs::path& cwd,
                  const std::vector<IncludedFile>& included_files,
                  const std::function<bool(const fs::path&)>& exists,
                  const std::function<fs::path(const fs::path&)>& canonical)
{
  auto absolute = [&](const fs::path& path) {
    return util::lexically_normal(path.is_absolute() ? path : cwd / path);
  };

  std::unordered_map<std::string, bool> exists_cache;
  auto cached_exists = [&](const fs::path& path) {
    const std::string key = util::pstr(path).str();
    auto it = exists_cache.find(key);
    if (it == exists_cache.end()) {
      it = exists_cache.emplace(key, exists(path)).first;
    }
    return it->second;
  };

  // A directory as printed by the preprocessor (used for the result), as an
  // absolute path and in canonical form (used for matching).
  struct Dir
  {
    fs::path as_printed;
    fs::path absolute;
    fs::path canonical;
  };
  auto make_dir = [&](const fs::path& dir) {
    const fs::path absolute_dir = absolute(dir);
    return Dir{util::lexically_normal(dir),
               absolute_dir,
               util::lexically_normal(canonical(absolute_dir))};
  };

  std::vector<Dir> dirs;
  for (const auto* list : {&paths.quote_dirs, &paths.angle_dirs}) {
    for (const auto& dir : *list) {
      dirs.push_back(make_dir(dir));
    }
  }

  std::unordered_map<std::string, Dir> includer_dirs;
  auto includer_dir = [&](const fs::path& dir) -> const Dir& {
    const std::string key = util::pstr(dir).str();
    auto it = includer_dirs.find(key);
    if (it == includer_dirs.end()) {
      it = includer_dirs.emplace(key, make_dir(dir)).first;
    }
    return it->second;
  };

  std::set<std::string> result;
  for (const auto& dir : paths.nonexistent_dirs) {
    // Clang also reports files given to -I as nonexistent directories.
    if (!cached_exists(absolute(dir))) {
      result.insert(util::pstr(util::lexically_normal(dir)).str());
    }
  }

  // Record the first missing component of `relative` below `dir`: nothing
  // below a missing directory can appear without the directory appearing
  // first.
  auto add_shadow_path = [&](const Dir& dir, const fs::path& relative) {
    fs::path candidate = dir.as_printed;
    fs::path absolute_candidate = dir.absolute;
    bool has_dot_components = false;
    for (const auto& component : relative) {
      candidate /= component;
      absolute_candidate /= component;
      has_dot_components |= component == "." || component == "..";
      const bool exists = cached_exists(
        has_dot_components ? util::lexically_normal(absolute_candidate)
                           : absolute_candidate);
      if (!exists) {
        result.insert(util::pstr(util::lexically_normal(candidate)).str());
        return;
      }
    }
  };

  for (const auto& file : included_files) {
    // Match the path as printed before the normalized path so that ".." in
    // #include "../foo.h" keeps the association with the search directory.
    const fs::path printed_file =
      file.path.is_absolute() ? file.path : cwd / file.path;
    const fs::path normalized_file = util::lexically_normal(printed_file);
    for (size_t i = 0; i < dirs.size(); ++i) {
      std::optional<fs::path> relative;
      for (const fs::path* f : {&printed_file, &normalized_file}) {
        for (const fs::path* d : {&dirs[i].absolute, &dirs[i].canonical}) {
          if (*f != *d && util::path_starts_with(*f, *d)) {
            relative = f->lexically_relative(*d);
            break;
          }
        }
        if (relative) {
          break;
        }
      }
      if (!relative) {
        continue;
      }
      // GCC uses foo.h.gch in a directory before foo.h in later ones, but also
      // foo.h in an earlier directory before foo.h.gch in a later one.
      std::vector<fs::path> relatives = {*relative};
      const auto extension = relative->extension();
      if (extension == ".gch" || extension == ".pch" || extension == ".pth") {
        relatives.push_back(relative->parent_path() / relative->stem());
      }
      for (const auto& rel : relatives) {
        for (const auto& dir : file.includer_dirs) {
          add_shadow_path(includer_dir(dir), rel);
        }
        for (size_t j = 0; j < i; ++j) {
          if (dirs[j].canonical != dirs[i].canonical) {
            add_shadow_path(dirs[j], rel);
          }
        }
      }
    }
  }

  std::vector<fs::path> paths_result;
  paths_result.reserve(result.size());
  for (const auto& path : result) {
    paths_result.emplace_back(path);
  }
  return paths_result;
}

} // namespace compiler
