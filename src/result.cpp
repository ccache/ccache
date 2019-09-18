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

#include "result.hpp"

#include "Config.hpp"
#include "ccache.hpp"
#include "common_header.hpp"
#include "compression.hpp"
#include "hash.hpp"
#include "int_bytes_conversion.hpp"

// Result data format
// ==================
//
// Integers are big-endian.
//
// <result>               ::= <header> <body> <epilogue>
// <header>               ::= <magic> <version> <compr_type> <compr_level>
//                            <content_len>
// <magic>                ::= 4 bytes ("cCrS")
// <version>              ::= uint8_t
// <compr_type>           ::= <compr_none> | <compr_zstd>
// <compr_none>           ::= 0 (uint8_t)
// <compr_zstd>           ::= 1 (uint8_t)
// <compr_level>          ::= int8_t
// <content_len>          ::= uint64_t ; size of file if stored uncompressed
// <body>                 ::= <n_entries> <entry>* ; potentially compressed
// <n_entries>            ::= uint8_t
// <entry>                ::= <embedded_file_entry> | <raw_file_entry>
// <embedded_file_entry>  ::= <embedded_file_marker> <suffix_len> <suffix>
//                            <data_len> <data>
// <embedded_file_marker> ::= 0 (uint8_t)
// <suffix_len>           ::= uint8_t
// <suffix>               ::= suffix_len bytes
// <data_len>             ::= uint64_t
// <data>                 ::= data_len bytes
// <raw_file_entry>       ::= <raw_file_marker> <suffix_len> <suffix> <file_len>
// <raw_file_marker>      ::= 1 (uint8_t)
// <file_len>             ::= uint64_t
// <epilogue>             ::= <checksum>
// <checksum>             ::= uint64_t ; XXH64 of content bytes
//
// Sketch of concrete layout:
//
// <magic>                4 bytes
// <version>              1 byte
// <compr_type>           1 byte
// <compr_level>          1 byte
// <content_len>          8 bytes
// --- [potentially compressed from here] -------------------------------------
// <n_entries>            1 byte
// <embedded_file_marker> 1 byte
// <suffix_len>           1 byte
// <suffix>               suffix_len bytes
// <data_len>             8 bytes
// <data>                 data_len bytes
// ...
// <ref_marker>           1 byte
// <key_len>              1 byte
// <key>                  key_len bytes
// ...
// checksum               8 bytes
//
//
// Version history
// ===============
//
// 1: Introduced in ccache 4.0.

extern char* stats_file;

const char RESULT_MAGIC[4] = {'c', 'C', 'r', 'S'};

enum {
  // File data stored inside the result file.
  EMBEDDED_FILE_MARKER = 0,

  // File stored as-is in the file system.
  RAW_FILE_MARKER = 1
};

struct result_file
{
  char* suffix;
  char* path;
  uint64_t size;
};

struct result_files
{
  uint32_t n_files;
  struct result_file* files;
  uint64_t* sizes;
};

typedef bool (*read_entry_fn)(struct decompressor* decompressor,
                              struct decompr_state* decompr_state,
                              const char* result_path_in_cache,
                              uint32_t entry_number,
                              const struct result_files* list,
                              FILE* dump_stream);

typedef bool (*write_entry_fn)(struct compressor* compressor,
                               struct compr_state* compr_state,
                               const char* result_path_in_cache,
                               uint32_t entry_number,
                               const struct result_file* file);

struct result_files*
result_files_init(void)
{
  auto list = static_cast<result_files*>(x_malloc(sizeof(result_files)));
  list->n_files = 0;
  list->files = NULL;
  list->sizes = NULL;

  return list;
}

void
result_files_add(struct result_files* list,
                 const char* path,
                 const char* suffix)
{
  uint32_t n = list->n_files;
  list->files = static_cast<result_file*>(
    x_realloc(list->files, (n + 1) * sizeof(result_file)));
  list->sizes =
    static_cast<uint64_t*>(x_realloc(list->sizes, (n + 1) * sizeof(uint64_t)));
  struct result_file* f = &list->files[list->n_files];
  list->n_files++;

  struct stat st;
  x_stat(path, &st);

  f->suffix = x_strdup(suffix);
  f->path = x_strdup(path);
  f->size = st.st_size;
}

void
result_files_free(struct result_files* list)
{
  for (uint32_t i = 0; i < list->n_files; i++) {
    free(list->files[i].suffix);
    free(list->files[i].path);
  }
  free(list->files);
  list->files = NULL;
  free(list->sizes);
  list->sizes = NULL;

  free(list);
}

#define READ_BYTES(buf, length)                                                \
  do {                                                                         \
    if (!decompressor->read(decompr_state, buf, length)) {                     \
      goto out;                                                                \
    }                                                                          \
  } while (false)

#define READ_BYTE(var) READ_BYTES(&var, 1)

#define READ_UINT64(var)                                                       \
  do {                                                                         \
    char buf_[8];                                                              \
    READ_BYTES(buf_, sizeof(buf_));                                            \
    (var) = UINT64_FROM_BYTES(buf_);                                           \
  } while (false)

static bool
read_embedded_file_entry(struct decompressor* decompressor,
                         struct decompr_state* decompr_state,
                         const char* result_path_in_cache,
                         uint32_t entry_number,
                         const struct result_files* list,
                         FILE* dump_stream)
{
  (void)result_path_in_cache;
  bool found = false;
  bool success = false;
  FILE* subfile = NULL;

  uint8_t suffix_len;
  READ_BYTE(suffix_len);

  char suffix[256 + 1];
  READ_BYTES(suffix, suffix_len);
  suffix[suffix_len] = '\0';

  uint64_t file_len;
  READ_UINT64(file_len);

  if (dump_stream) {
    fprintf(dump_stream,
            "Embedded file #%u: %s (%" PRIu64 " bytes)\n",
            entry_number,
            suffix,
            file_len);
  } else {
    cc_log("Retrieving embedded file #%u %s (%llu bytes)",
           entry_number,
           suffix,
           (unsigned long long)file_len);

    for (uint32_t i = 0; i < list->n_files; i++) {
      if (str_eq(suffix, list->files[i].suffix)) {
        found = true;

        cc_log("Copying to %s", list->files[i].path);

        subfile = fopen(list->files[i].path, "wb");
        if (!subfile) {
          cc_log("Failed to open %s for writing", list->files[i].path);
          goto out;
        }
        char buf[READ_BUFFER_SIZE];
        size_t remain = file_len;
        while (remain > 0) {
          size_t n = std::min(remain, sizeof(buf));
          READ_BYTES(buf, n);
          if (fwrite(buf, 1, n, subfile) != n) {
            goto out;
          }
          remain -= n;
        }
        fclose(subfile);
        subfile = NULL;

        break;
      }
    }
  }

  if (!found) {
    // Discard the file data.
    char buf[READ_BUFFER_SIZE];
    size_t remain = file_len;
    while (remain > 0) {
      size_t n = std::min(remain, sizeof(buf));
      READ_BYTES(buf, n);
      remain -= n;
    }
  }

  success = true;

out:
  if (subfile) {
    fclose(subfile);
  }

  return success;
}

static char*
get_raw_file_path(const char* result_path_in_cache, uint32_t entry_number)
{
  return format("%.*s_%u.raw",
                (int)strlen(result_path_in_cache) - 7, // .result
                result_path_in_cache,
                entry_number);
}

static bool
copy_raw_file(const char* source, const char* dest, bool to_cache)
{
  if (g_config.file_clone()) {
    cc_log("Cloning %s to %s", source, dest);
    if (clone_file(source, dest, to_cache)) {
      return true;
    }
    cc_log("Failed to clone: %s", strerror(errno));
  }
  if (g_config.hard_link()) {
    x_try_unlink(dest);
    cc_log("Hard linking %s to %s", source, dest);
    int ret = link(source, dest);
    if (ret == 0) {
      return true;
    }
    cc_log("Failed to hard link: %s", strerror(errno));
  }

  cc_log("Copying %s to %s", source, dest);
  return copy_file(source, dest, to_cache);
}

static bool
read_raw_file_entry(struct decompressor* decompressor,
                    struct decompr_state* decompr_state,
                    const char* result_path_in_cache,
                    uint32_t entry_number,
                    const struct result_files* list,
                    FILE* dump_stream)
{
  bool success = false;
  char* raw_path = get_raw_file_path(result_path_in_cache, entry_number);

  uint8_t suffix_len;
  READ_BYTE(suffix_len);

  char suffix[256 + 1];
  READ_BYTES(suffix, suffix_len);
  suffix[suffix_len] = '\0';

  uint64_t file_len;
  READ_UINT64(file_len);

  if (dump_stream) {
    fprintf(dump_stream,
            "Raw file #%u: %s (%" PRIu64 " bytes)\n",
            entry_number,
            suffix,
            file_len);
  } else {
    cc_log("Retrieving raw file #%u %s (%llu bytes)",
           entry_number,
           suffix,
           (unsigned long long)file_len);

    struct stat st;
    if (x_stat(raw_path, &st) != 0) {
      goto out;
    }
    if ((uint64_t)st.st_size != file_len) {
      cc_log("Bad file size of %s (actual %llu bytes, expected %llu bytes)",
             raw_path,
             (unsigned long long)st.st_size,
             (unsigned long long)file_len);
      goto out;
    }

    for (uint32_t i = 0; i < list->n_files; i++) {
      if (str_eq(suffix, list->files[i].suffix)) {
        if (!copy_raw_file(raw_path, list->files[i].path, false)) {
          goto out;
        }
        // Update modification timestamp to save the file from LRU cleanup
        // (and, if hard-linked, to make the object file newer than the source
        // file).
        update_mtime(raw_path);
        break;
      }
    }
  }

  success = true;

out:
  free(raw_path);
  return success;
}

static bool
read_result(const char* path,
            struct result_files* list,
            FILE* dump_stream,
            char** errmsg)
{
  *errmsg = NULL;
  bool cache_miss = false;
  bool success = false;
  struct decompressor* decompressor = NULL;
  struct decompr_state* decompr_state = NULL;
  Checksum checksum;

  FILE* f = fopen(path, "rb");
  if (!f) {
    cache_miss = true;
    goto out;
  }

  struct common_header header;
  if (!common_header_initialize_for_reading(&header,
                                            f,
                                            RESULT_MAGIC,
                                            RESULT_VERSION,
                                            &decompressor,
                                            &decompr_state,
                                            &checksum,
                                            errmsg)) {
    goto out;
  }

  if (dump_stream) {
    common_header_dump(&header, dump_stream);
  }

  uint8_t n_entries;
  READ_BYTE(n_entries);

  uint32_t i;
  for (i = 0; i < n_entries; i++) {
    uint8_t marker;
    READ_BYTE(marker);

    read_entry_fn read_entry;

    switch (marker) {
    case EMBEDDED_FILE_MARKER:
      read_entry = read_embedded_file_entry;
      break;

    case RAW_FILE_MARKER:
      read_entry = read_raw_file_entry;
      break;

    default:
      *errmsg = format("Unknown entry type: %u", marker);
      goto out;
    }

    if (!read_entry(decompressor, decompr_state, path, i, list, dump_stream)) {
      goto out;
    }
  }

  if (i != n_entries) {
    *errmsg = format("Too few entries (read %u, expected %u)", i, n_entries);
    goto out;
  }

  {
    uint64_t actual_checksum = checksum.digest();
    uint64_t expected_checksum;
    READ_UINT64(expected_checksum);

    if (actual_checksum == expected_checksum) {
      success = true;
    } else {
      *errmsg = format("Incorrect checksum (actual %016llx, expected %016llx)",
                       (unsigned long long)actual_checksum,
                       (unsigned long long)expected_checksum);
    }
  }

out:
  if (decompressor && !decompressor->free(decompr_state)) {
    success = false;
  }
  if (f) {
    fclose(f);
  }
  if (!success && !cache_miss && !*errmsg) {
    *errmsg = x_strdup("Corrupt result");
  }
  return success;
}

#define WRITE_BYTES(buf, length)                                               \
  do {                                                                         \
    if (!compressor->write(compr_state, buf, length)) {                        \
      goto error;                                                              \
    }                                                                          \
  } while (false)

#define WRITE_BYTE(var)                                                        \
  do {                                                                         \
    char ch_ = var;                                                            \
    WRITE_BYTES(&ch_, 1);                                                      \
  } while (false)

#define WRITE_UINT64(var)                                                      \
  do {                                                                         \
    char buf_[8];                                                              \
    BYTES_FROM_UINT64(buf_, (var));                                            \
    WRITE_BYTES(buf_, sizeof(buf_));                                           \
  } while (false)

static bool
write_embedded_file_entry(struct compressor* compressor,
                          struct compr_state* compr_state,
                          const char* result_path_in_cache,
                          uint32_t entry_number,
                          const struct result_file* file)
{
  (void)result_path_in_cache;
  bool success = false;
  size_t suffix_len;
  FILE* f;
  char buf[READ_BUFFER_SIZE];
  size_t remain;

  cc_log("Storing embedded file #%u %s (%llu bytes) from %s",
         entry_number,
         file->suffix,
         (unsigned long long)file->size,
         file->path);

  WRITE_BYTE(EMBEDDED_FILE_MARKER);
  suffix_len = strlen(file->suffix);
  WRITE_BYTE(suffix_len);
  WRITE_BYTES(file->suffix, suffix_len);
  WRITE_UINT64(file->size);

  f = fopen(file->path, "rb");
  if (!f) {
    cc_log("Failed to open %s for reading", file->path);
    goto error;
  }
  remain = file->size;
  while (remain > 0) {
    size_t n = std::min(remain, sizeof(buf));
    if (fread(buf, 1, n, f) != n) {
      goto error;
    }
    WRITE_BYTES(buf, n);
    remain -= n;
  }
  fclose(f);

  success = true;

error:
  return success;
}

static bool
write_raw_file_entry(struct compressor* compressor,
                     struct compr_state* compr_state,
                     const char* result_path_in_cache,
                     uint32_t entry_number,
                     const struct result_file* file)
{
  bool success = false;
  size_t suffix_len;
  char* raw_file;
  struct stat old_stat;
  bool old_existed;
  struct stat new_stat;
  bool new_exists;
  size_t old_size;
  size_t new_size;

  cc_log("Storing raw file #%u %s (%llu bytes) from %s",
         entry_number,
         file->suffix,
         (unsigned long long)file->size,
         file->path);

  WRITE_BYTE(RAW_FILE_MARKER);
  suffix_len = strlen(file->suffix);
  WRITE_BYTE(suffix_len);
  WRITE_BYTES(file->suffix, suffix_len);
  WRITE_UINT64(file->size);

  raw_file = get_raw_file_path(result_path_in_cache, entry_number);
  old_existed = stat(raw_file, &old_stat) == 0;
  success = copy_raw_file(file->path, raw_file, true);
  new_exists = stat(raw_file, &new_stat) == 0;
  free(raw_file);

  old_size = old_existed ? file_size(&old_stat) : 0;
  new_size = new_exists ? file_size(&new_stat) : 0;
  stats_update_size(stats_file,
                    new_size - old_size,
                    (new_exists ? 1 : 0) - (old_existed ? 1 : 0));

error:
  return success;
}

static bool
should_store_raw_file(const char* suffix)
{
  if (!g_config.file_clone() && !g_config.hard_link()) {
    return false;
  }

  // - Don't store stderr outputs as raw files since they:
  //   1. Never are large.
  //   2. Will end up in a temporary file anyway.
  //
  // - Don't store .d files since they:
  //   1. Never are large.
  //   2. Compress well.
  //   3. Cause trouble for automake if hard-linked (see ccache issue 378).
  //
  // Note that .d files can't be stored as raw files for the file_clone case
  // since the hard link mode happily will try to use them if they exist. This
  // could be fixed by letting read_raw_file_entry refuse to hard link .d
  // files, but it's easier to simply always store them embedded. This will
  // also save i-nodes in the cache.
  return !str_eq(suffix, RESULT_STDERR_NAME) && !str_eq(suffix, ".d");
}

static bool
write_result(const struct result_files* list,
             struct compressor* compressor,
             struct compr_state* compr_state,
             Checksum& checksum,
             const char* result_path_in_cache)
{
  WRITE_BYTE(list->n_files);

  for (uint32_t i = 0; i < list->n_files; i++) {
    write_entry_fn write_entry = should_store_raw_file(list->files[i].suffix)
                                   ? write_raw_file_entry
                                   : write_embedded_file_entry;
    if (!write_entry(
          compressor, compr_state, result_path_in_cache, i, &list->files[i])) {
      goto error;
    }
  }

  WRITE_UINT64(checksum.digest());

  return true;

error:
  cc_log("Error writing to result file");
  return false;
}

bool
result_get(const char* path, struct result_files* list)
{
  cc_log("Getting result %s", path);

  char* errmsg;
  bool success = read_result(path, list, NULL, &errmsg);
  if (success) {
    // Update modification timestamp to save files from LRU cleanup.
    update_mtime(path);
  } else if (errmsg) {
    cc_log("Error: %s", errmsg);
    free(errmsg);
  } else {
    cc_log("No such result file");
  }
  return success;
}

bool
result_put(const char* path, struct result_files* list)
{
  bool ret = false;
  Checksum checksum;
  bool ok;
  uint64_t content_size;

  char* tmp_file = format("%s.tmp", path);
  int fd = create_tmp_fd(&tmp_file);
  FILE* f = fdopen(fd, "wb");
  if (!f) {
    cc_log("Failed to fdopen %s", tmp_file);
    goto out;
  }

  content_size = COMMON_HEADER_SIZE;
  content_size += 1; // n_entries
  for (uint32_t i = 0; i < list->n_files; i++) {
    content_size += 1;                             // embedded_file_marker
    content_size += 1;                             // suffix_len
    content_size += strlen(list->files[i].suffix); // suffix
    content_size += 8;                             // data_len
    content_size += list->files[i].size;           // data
  }
  content_size += 8; // checksum

  struct common_header header;
  struct compressor* compressor;
  struct compr_state* compr_state;
  if (!common_header_initialize_for_writing(&header,
                                            f,
                                            RESULT_MAGIC,
                                            RESULT_VERSION,
                                            compression_type_from_config(),
                                            compression_level_from_config(),
                                            content_size,
                                            checksum,
                                            &compressor,
                                            &compr_state)) {
    goto out;
  }

  ok = write_result(list, compressor, compr_state, checksum, path)
       && compressor->free(compr_state);
  if (!ok) {
    cc_log("Failed to write result file");
    goto out;
  }

  fclose(f);
  f = NULL;
  if (x_rename(tmp_file, path) == 0) {
    ret = true;
  } else {
    cc_log("Failed to rename %s to %s", tmp_file, path);
  }

out:
  free(tmp_file);
  if (f) {
    fclose(f);
  }
  return ret;
}

bool
result_dump(const char* path, FILE* stream)
{
  assert(stream);

  char* errmsg;
  bool success = read_result(path, NULL, stream, &errmsg);
  if (errmsg) {
    fprintf(stream, "Error: %s\n", errmsg);
    free(errmsg);
  }
  return success;
}
