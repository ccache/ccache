// Copyright (C) 2021-2025 Joel Rosdahl and other contributors
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

#pragma once

#include <ccache/util/bytes.hpp>
#include <ccache/util/direntry.hpp>
#include <ccache/util/time.hpp>
#include <ccache/util/types.hpp>

#include <nonstd/span.hpp>
#include <tl/expected.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

namespace util {

// --- Interface ---

enum class WriteFileMode {
  unlink,    // unlink existing file before writing (break hard links)
  in_place,  // don't unlink before writing (don't break hard links)
  exclusive, // return error if the file already exists (O_EXCL)
};
enum class LogFailure { yes, no };
enum class ViaTmpFile { yes, no };

using TraverseDirectoryVisitor = std::function<void(const DirEntry& dir_entry)>;

// Copy a file from `src` to `dest`. If `via_tmp_file` is yes, `src` is copied
// to a temporary file and then renamed to dest.
tl::expected<void, std::string>
copy_file(const std::filesystem::path& src,
          const std::filesystem::path& dest,
          ViaTmpFile via_tmp_file = ViaTmpFile::no);

void create_cachedir_tag(const std::filesystem::path& dir);

// Extends file size of `fd` to at least `new_size` by calling posix_fallocate()
// if supported, otherwise by writing zeros last to the file.
//
// Note that existing holes are not filled in case posix_fallocate() is not
// supported.
tl::expected<void, std::string> fallocate(int fd, size_t new_size);

// Return how much a file of `size` bytes likely would take on disk.
uint64_t likely_size_on_disk(uint64_t size);

// Read data from `fd` until end of file and call `data_receiver` repeatedly
// with the read data. Returns an error if the underlying read(2) call returned
// -1.
tl::expected<void, std::string> read_fd(int fd, DataReceiver data_receiver);

// Read data from `fd`. Returns an error if the underlying read(2) call returned
// -1.
tl::expected<util::Bytes, std::string> read_fd(int fd);

// Return contents of file at  `path`.
//
// `T` should be `util::Bytes` or `std::vector<uint8_t>` for binary data and
// `std::string` for text data. If `T` is `std::string` and the content starts
// with a UTF-16 little-endian BOM on Windows then it will be converted to
// UTF-8.
//
// If `size_hint` is not 0 then it is assumed that `path` has this size (this
// saves system calls).
template<typename T>
tl::expected<T, std::string> read_file(const std::filesystem::path& path,
                                       size_t size_hint = 0);

// Return (at most) `count` bytes from `path` starting at position `pos`.
//
// `T` should be `util::Bytes` or `std::vector<uint8_t>` for binary data and
// `std::string` for text data. If `T` is `std::string` and the content starts
// with a UTF-16 little-endian BOM on Windows then it will be converted to
// UTF-8.
template<typename T>
tl::expected<T, std::string>
read_file_part(const std::filesystem::path& path, size_t pos, size_t count);

// Remove `path` (non-directory), NFS hazardous. Use only for files that will
// not exist on other systems.
//
// Returns whether the file was removed. A nonexistent `path` is considered
// successful.
tl::expected<bool, std::error_code>
remove(const std::filesystem::path& path,
       LogFailure log_failure = LogFailure::yes);

// Remove `path` (non-directory), NFS safe.
//
// Returns whether the file was removed. A nonexistent `path` is considered a
// successful.
tl::expected<bool, std::error_code>
remove_nfs_safe(const std::filesystem::path& path,
                LogFailure log_failure = LogFailure::yes);

// Set the FD_CLOEXEC on file descriptor `fd`. This is a NOP on Windows.
void set_cloexec_flag(int fd);

// Set atime/mtime of `path`. If `mtime` is std::nullopt, set to the current
// time. If `atime` is std::nullopt, set to what `mtime` specifies.
void set_timestamps(const std::filesystem::path& path,
                    std::optional<TimePoint> mtime = std::nullopt,
                    std::optional<TimePoint> atime = std::nullopt);

// Traverse `path` recursively in postorder (directory entries are visited
// before their parent directory).
tl::expected<void, std::string>
traverse_directory(const std::filesystem::path& directory,
                   const TraverseDirectoryVisitor& visitor);

// Write `size` bytes from binary `data` to `fd`.
tl::expected<void, std::string> write_fd(int fd, const void* data, size_t size);

// Write text `data` to `path`.
tl::expected<void, std::string>
write_file(const std::filesystem::path& path,
           std::string_view data,
           WriteFileMode mode = WriteFileMode::unlink);

// Write binary `data` to `path`.
tl::expected<void, std::string>
write_file(const std::filesystem::path& path,
           nonstd::span<const uint8_t> data,
           WriteFileMode mode = WriteFileMode::unlink);

// --- Inline implementations ---

inline uint64_t
likely_size_on_disk(uint64_t size)
{
  return (size + 4095) & ~4095;
}

} // namespace util
