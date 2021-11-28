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

#include "util.hpp"

#include <Util.hpp>
#include <fmtmacros.hpp>

namespace storage {
namespace primary {

void
for_each_level_1_subdir(const std::string& cache_dir,
                        const SubdirVisitor& visitor,
                        const ProgressReceiver& progress_receiver)
{
  for (int i = 0; i <= 0xF; i++) {
    double progress = 1.0 * i / 16;
    progress_receiver(progress);
    std::string subdir_path = FMT("{}/{:x}", cache_dir, i);
    visitor(subdir_path, [&](double inner_progress) {
      progress_receiver(progress + inner_progress / 16);
    });
  }
  progress_receiver(1.0);
}

std::vector<CacheFile>
get_level_1_files(const std::string& dir,
                  const ProgressReceiver& progress_receiver)
{
  std::vector<CacheFile> files;

  if (!Stat::stat(dir)) {
    return files;
  }

  size_t level_2_directories = 0;

  Util::traverse(dir, [&](const std::string& path, bool is_dir) {
    auto name = Util::base_name(path);
    if (name == "CACHEDIR.TAG" || name == "stats" || name.starts_with(".nfs")) {
      return;
    }

    if (!is_dir) {
      files.emplace_back(path);
    } else if (path != dir
               && path.find('/', dir.size() + 1) == std::string::npos) {
      ++level_2_directories;
      progress_receiver(level_2_directories / 16.0);
    }
  });

  progress_receiver(1.0);
  return files;
}

} // namespace primary
} // namespace storage
