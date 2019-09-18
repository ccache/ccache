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

#include "common_header.hpp"

#include "ccache.hpp"
#include "int_bytes_conversion.hpp"

bool
common_header_initialize_for_writing(struct common_header* header,
                                     FILE* output,
                                     const char magic[4],
                                     uint8_t version,
                                     uint8_t compression_type,
                                     int8_t compression_level,
                                     uint64_t content_size,
                                     Checksum& checksum,
                                     struct compressor** compressor,
                                     struct compr_state** compr_state)
{
  memcpy(header->magic, magic, 4);
  header->version = version;
  header->compression_type = compression_type;
  header->compression_level = compression_level;
  header->content_size = content_size;

  *compressor = compressor_from_type(header->compression_type);
  assert(*compressor);
  *compr_state =
    (*compressor)->init(output, header->compression_level, &checksum);
  if (!*compr_state) {
    cc_log("Failed to initialize compressor");
    return false;
  }
  header->compression_level =
    (*compressor)->get_actual_compression_level(*compr_state);

  uint8_t header_bytes[COMMON_HEADER_SIZE];
  memcpy(header_bytes, header->magic, 4);
  header_bytes[4] = header->version;
  header_bytes[5] = header->compression_type;
  header_bytes[6] = header->compression_level;
  BYTES_FROM_UINT64(header_bytes + 7, header->content_size);
  if (fwrite(header_bytes, sizeof(header_bytes), 1, output) != 1) {
    cc_log("Failed to write common file header");
    return false;
  }
  checksum.update(header_bytes, sizeof(header_bytes));
  return true;
}

bool
common_header_initialize_for_reading(struct common_header* header,
                                     FILE* input,
                                     const char expected_magic[4],
                                     uint8_t expected_version,
                                     struct decompressor** decompressor,
                                     struct decompr_state** decompr_state,
                                     Checksum* checksum,
                                     char** errmsg)
{
  uint8_t header_bytes[COMMON_HEADER_SIZE];
  if (fread(header_bytes, sizeof(header_bytes), 1, input) != 1) {
    *errmsg = format("Failed to read common header");
    return false;
  }

  memcpy(header->magic, header_bytes, 4);
  header->version = header_bytes[4];
  header->compression_type = header_bytes[5];
  header->compression_level = header_bytes[6];
  header->content_size = UINT64_FROM_BYTES(header_bytes + 7);

  if (memcmp(header->magic, expected_magic, sizeof(header->magic)) != 0) {
    *errmsg = format("Bad magic value 0x%x%x%x%x",
                     header->magic[0],
                     header->magic[1],
                     header->magic[2],
                     header->magic[3]);
    return false;
  }

  if (header->version != expected_version) {
    *errmsg = format("Unknown version (actual %u, expected %u)",
                     header->version,
                     expected_version);
    return false;
  }

  if (header->compression_type == COMPR_TYPE_NONE) {
    // Since we have the size available, let's use it as a super primitive
    // consistency check for the non-compressed case. (A real checksum is used
    // for compressed data.)
    struct stat st;
    if (x_fstat(fileno(input), &st) != 0) {
      return false;
    }
    if ((uint64_t)st.st_size != header->content_size) {
      *errmsg = format(
        "Bad uncompressed file size (actual %llu bytes, expected %llu bytes)",
        (unsigned long long)st.st_size,
        (unsigned long long)header->content_size);
      return false;
    }
  }

  if (!decompressor) {
    return true;
  }

  *decompressor = decompressor_from_type(header->compression_type);
  if (!*decompressor) {
    *errmsg = format("Unknown compression type: %u", header->compression_type);
    return false;
  }

  if (checksum) {
    checksum->update(header_bytes, sizeof(header_bytes));
  }

  *decompr_state = (*decompressor)->init(input, checksum);
  if (!*decompr_state) {
    *errmsg = x_strdup("Failed to initialize decompressor");
    return false;
  }

  return true;
}

void
common_header_dump(const struct common_header* header, FILE* f)
{
  fprintf(f,
          "Magic: %c%c%c%c\n",
          header->magic[0],
          header->magic[1],
          header->magic[2],
          header->magic[3]);
  fprintf(f, "Version: %u\n", header->version);
  fprintf(f,
          "Compression type: %s\n",
          compression_type_to_string(header->compression_type));
  fprintf(f, "Compression level: %d\n", header->compression_level);
  fprintf(f, "Content size: %" PRIu64 "\n", header->content_size);
}
