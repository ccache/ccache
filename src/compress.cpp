// Copyright (C) 2019 Joel Rosdahl and other contributors
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

#include "compress.hpp"

#include "ccache.hpp"
#include "common_header.hpp"
#include "manifest.hpp"
#include "result.hpp"

#include <string>

static bool
get_content_size(const std::string& path,
                 const char* magic,
                 uint8_t version,
                 size_t* size)
{
  char* errmsg;
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) {
    cc_log("Failed to open %s for reading: %s", path.c_str(), strerror(errno));
    return false;
  }
  struct common_header header;
  bool success = common_header_initialize_for_reading(
    &header, f, magic, version, NULL, NULL, NULL, &errmsg);
  fclose(f);
  if (success) {
    *size = header.content_size;
  }

  return success;
}

void
compress_stats(const Config& config,
               const Util::ProgressReceiver& progress_receiver)
{
  uint64_t on_disk_size = 0;
  uint64_t compr_size = 0;
  uint64_t compr_orig_size = 0;
  uint64_t incompr_size = 0;

  Util::for_each_level_1_subdir(
    config.cache_dir(),
    [&](const std::string& subdir,
        const Util::ProgressReceiver& sub_progress_receiver) {
      std::vector<std::shared_ptr<CacheFile>> files;
      Util::get_level_1_files(
        subdir,
        [&](double progress) { sub_progress_receiver(progress / 2); },
        files);

      for (size_t i = 0; i < files.size(); ++i) {
        const auto& file = files[i];

        on_disk_size += file_size(&file->stat());

        size_t content_size = 0;
        bool is_compressible;
        if (file->type() == CacheFile::Type::manifest) {
          is_compressible = get_content_size(
            file->path(), MANIFEST_MAGIC, MANIFEST_VERSION, &content_size);
        } else if (file->type() == CacheFile::Type::result) {
          is_compressible = get_content_size(
            file->path(), RESULT_MAGIC, RESULT_VERSION, &content_size);
        } else {
          is_compressible = false;
        }

        if (is_compressible) {
          compr_size += file->stat().st_size;
          compr_orig_size += content_size;
        } else {
          incompr_size += file->stat().st_size;
        }

        sub_progress_receiver(1.0 / 2 + 1.0 * i / files.size() / 2);
      }
    },
    progress_receiver);

  if (isatty(STDOUT_FILENO)) {
    printf("\n\n");
  }

  double ratio = compr_size > 0 ? ((double)compr_orig_size) / compr_size : 0.0;
  double savings = ratio > 0.0 ? 100.0 - (100.0 / ratio) : 0.0;

  char* on_disk_size_str = format_human_readable_size(on_disk_size);
  char* cache_size_str = format_human_readable_size(compr_size + incompr_size);
  char* compr_size_str = format_human_readable_size(compr_size);
  char* compr_orig_size_str = format_human_readable_size(compr_orig_size);
  char* incompr_size_str = format_human_readable_size(incompr_size);

  printf("Total data:            %8s (%s disk blocks)\n",
         cache_size_str,
         on_disk_size_str);
  printf("Compressible data:     %8s (%.1f%% of original size)\n",
         compr_size_str,
         100.0 - savings);
  printf("  - Original size:     %8s\n", compr_orig_size_str);
  printf(
    "  - Compression ratio: %5.3f x  (%.1f%% space savings)\n", ratio, savings);
  printf("Incompressible data:   %8s\n", incompr_size_str);

  free(incompr_size_str);
  free(compr_orig_size_str);
  free(compr_size_str);
  free(cache_size_str);
  free(on_disk_size_str);
}
