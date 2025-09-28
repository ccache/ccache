// Copyright (C) 2021-2024 Joel Rosdahl and other contributors
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

#include "util.hpp"

#include <ccache/core/exceptions.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/format.hpp>
#include <ccache/util/path.hpp>
#include <ccache/util/string.hpp>

namespace fs = util::filesystem;

using util::DirEntry;

namespace storage::local {

void
for_each_cache_subdir(const SubdirVisitor& visitor)
{
  for (uint8_t i = 0; i < 16; ++i) {
    visitor(i);
  }
}

void
for_each_cache_subdir(const ProgressReceiver& progress_receiver,
                      const SubdirProgressVisitor& visitor)
{
  for (uint8_t i = 0; i < 16; ++i) {
    double progress = i / 16.0;
    progress_receiver(progress);
    visitor(i, [&](double inner_progress) {
      progress_receiver(progress + inner_progress / 16);
    });
  }
  progress_receiver(1.0);
}

void
for_each_level_1_and_2_stats_file(
  const fs::path& cache_dir,
  const std::function<void(const fs::path& path)> function)
{
  for (size_t level_1 = 0; level_1 <= 0xF; ++level_1) {
    function(FMT("{}/{:x}/stats", cache_dir, level_1));
    for (size_t level_2 = 0; level_2 <= 0xF; ++level_2) {
      function(FMT("{}/{:x}/{:x}/stats", cache_dir, level_1, level_2));
    }
  }
}

std::vector<DirEntry>
get_cache_dir_files(const fs::path& dir)
{
  std::vector<DirEntry> files;

  if (!DirEntry(dir).is_directory()) {
    return files;
  }
  util::throw_on_error<core::Error>(
    util::traverse_directory(dir, [&](const auto& de) {
      std::string name = util::pstr(de.path().filename());
      if (name == "CACHEDIR.TAG" || name == "stats"
          || util::starts_with(name, ".nfs")) {
        return;
      }

      if (!de.is_directory()) {
        files.emplace_back(de);
      }
    }));

  return files;
}

} // namespace storage::local
