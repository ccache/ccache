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

#pragma once

#include <util/types.hpp>

#include <third_party/nonstd/expected.hpp>
#include <third_party/nonstd/span.hpp>

#include <ctime>
#include <optional>
#include <string>

namespace util {

// --- Interface ---

void create_cachedir_tag(const std::string& dir);

// Read data from `fd` until end of file and call `data_receiver` with the read
// data. Returns an error if the underlying read(2) call returned -1.
nonstd::expected<void, std::string> read_fd(int fd, DataReceiver data_receiver);

// Return data from `path`, where `T` is `std::string` for text data and
// `util::Blob` for binary data. If `T` is `std::string` and the content starts
// with a UTF-16 little-endian BOM on Windows then it will be converted to
// UTF-8. If `size_hint` is not 0 then it is assumed that `path` has this size
// (this saves system calls).
template<typename T>
nonstd::expected<T, std::string> read_file(const std::string& path,
                                           size_t size_hint = 0);

// Set atime/mtime of `path`. If `mtime` is std::nullopt, set to the current
// time. If `atime` is std::nullopt, set to what `mtime` specifies.
void set_timestamps(const std::string& path,
                    std::optional<timespec> mtime = std::nullopt,
                    std::optional<timespec> atime = std::nullopt);

// Write `size` bytes from `data` to `fd`. Returns errno on error.
nonstd::expected<void, std::string>
write_fd(int fd, const void* data, size_t size);

// Write text `data` to `path`.
nonstd::expected<void, std::string> write_file(const std::string& path,
                                               const std::string& data);

// Write binary `data` to `path`.
nonstd::expected<void, std::string>
write_file(const std::string& path, nonstd::span<const uint8_t> data);

} // namespace util
