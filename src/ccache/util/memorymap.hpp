// Copyright (C) 2024 ccache contributors
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

#include <ccache/util/noncopyable.hpp>

#include <tl/expected.hpp>

#include <cstddef>
#include <string>

namespace util {

class MemoryMap : util::NonCopyable
{
public:
  MemoryMap() = default;
  ~MemoryMap();

  MemoryMap(MemoryMap&& other) noexcept;
  MemoryMap& operator=(MemoryMap&& other) noexcept;

  void unmap();

  void* ptr();

  static tl::expected<MemoryMap, std::string> map(int fd, size_t size);

private:
  void* m_ptr = nullptr;
#ifndef _WIN32
  size_t m_size = 0; // munmap needs the size, not needed on Windows
#else
  void* m_file_mapping_handle =
    nullptr; // On Windows a handle on a file mapping is needed
#endif
};

} // namespace util
