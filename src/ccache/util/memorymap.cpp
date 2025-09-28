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

#include "memorymap.hpp"

#include <ccache/util/format.hpp>
#include <ccache/util/wincompat.hpp>

#include <cstring>

#ifndef _WIN32
#  include <sys/mman.h>
#endif

namespace util {

MemoryMap::~MemoryMap()
{
  unmap();
}

MemoryMap::MemoryMap(MemoryMap&& other) noexcept
{
  m_ptr = std::exchange(other.m_ptr, nullptr);
#ifndef _WIN32
  m_size = std::exchange(other.m_size, 0);
#else
  m_file_mapping_handle = std::exchange(other.m_file_mapping_handle, nullptr);
#endif
}

MemoryMap&
MemoryMap::operator=(MemoryMap&& other) noexcept
{
  unmap();
  m_ptr = std::exchange(other.m_ptr, nullptr);
#ifndef _WIN32
  m_size = std::exchange(other.m_size, 0);
#else
  m_file_mapping_handle = std::exchange(other.m_file_mapping_handle, nullptr);
#endif

  return *this;
}

void
MemoryMap::unmap()
{
  if (!m_ptr) {
    return;
  }

#ifndef _WIN32
  munmap(m_ptr, m_size);
  m_ptr = nullptr;
  m_size = 0;
#else
  UnmapViewOfFile(m_ptr);
  m_ptr = nullptr;
  CloseHandle(m_file_mapping_handle);
  m_file_mapping_handle = nullptr;
#endif
}

void*
MemoryMap::ptr()
{
  return m_ptr;
}

tl::expected<MemoryMap, std::string>
MemoryMap::map(int fd, size_t size)
{
#ifndef _WIN32
  const void* MMAP_FAILED =
    reinterpret_cast<void*>(-1); // NOLINT: Must cast here
  void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (p == MMAP_FAILED) {
    return tl::unexpected(strerror(errno));
  }

  MemoryMap map;
  map.m_ptr = p;
  map.m_size = size;
  return map;
#else
  HANDLE file_handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  if (file_handle == INVALID_HANDLE_VALUE) {
    return tl::unexpected(FMT("Can't get HANDLE from fd: {}", GetLastError()));
  }

  HANDLE file_mapping_handle =
    CreateFileMappingA(file_handle,
                       nullptr,
                       PAGE_READWRITE,
                       static_cast<uint64_t>(size) >> 32,
                       size & 0xffffffff,
                       nullptr);
  if (!file_mapping_handle) {
    return tl::unexpected(FMT("Can't create file mapping: {}", GetLastError()));
  }

  void* p = MapViewOfFile(file_mapping_handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
  if (!p) {
    std::string error = FMT("Can't map file: {}", GetLastError());
    CloseHandle(file_mapping_handle);
    return tl::unexpected(std::move(error));
  }

  MemoryMap map;
  map.m_ptr = p;
  map.m_file_mapping_handle = file_mapping_handle;
  return map;
#endif
}

} // namespace util
