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

#include "util.hpp"

#include <Util.hpp>
#include <fmtmacros.hpp>
#include <util/string.hpp>

namespace storage::local {

void
for_each_cache_subdir(const std::string& cache_dir,
                      const ProgressReceiver& progress_receiver,
                      const SubdirVisitor& visitor)
{
  for (uint8_t i = 0; i < 16; ++i) {
    double progress = i / 16.0;
    progress_receiver(progress);
    std::string subdir_path = FMT("{}/{:x}", cache_dir, i);
    visitor(subdir_path, [&](double inner_progress) {
      progress_receiver(progress + inner_progress / 16);
    });
  }
  progress_receiver(1.0);
}

void
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

std::vector<Stat>
get_cache_dir_files(const std::string& dir)
{
  std::vector<Stat> files;

  if (!Stat::stat(dir)) {
    return files;
  }

  Util::traverse(dir, [&](const std::string& path, bool is_dir) {
    auto name = Util::base_name(path);
    if (name == "CACHEDIR.TAG" || name == "stats"
        || util::starts_with(name, ".nfs")) {
      return;
    }

    if (!is_dir) {
      files.emplace_back(Stat::lstat(path));
    }
  });

  return files;
}

} // namespace storage::local
