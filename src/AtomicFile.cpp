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

#include "AtomicFile.hpp"

#include "Error.hpp"
#include "ccache.hpp"

#include <cerrno>
#include <fmt/core.h>
#include <unistd.h>

AtomicFile::AtomicFile(const std::string& path, Mode mode) : m_path(path)
{
  char* tmp_path = x_strdup(path.c_str());
  int fd = create_tmp_fd(&tmp_path);
  m_tmp_path = tmp_path;
  m_stream.open(tmp_path,
                mode == Mode::Binary ? std::ios::out | std::ios::binary
                                     : std::ios::out);
  free(tmp_path);
  ::close(fd);

  if (!m_stream) {
    throw Error(fmt::format("failed to create {}: {}", path, strerror(errno)));
  }
}

AtomicFile::~AtomicFile()
{
  if (m_stream.is_open()) {
    // close() was not called so remove the lingering temporary file.
    m_stream.close();
    tmp_unlink(m_tmp_path.c_str());
  }
}

void
AtomicFile::write(const std::string& data)
{
  m_stream.write(data.data(), data.size());
  if (!m_stream) {
    throw Error(
      fmt::format("failed to write data to {}: {}", m_path, strerror(errno)));
  }
}

void
AtomicFile::write(const std::vector<uint8_t>& data)
{
  m_stream.write(reinterpret_cast<const char*>(data.data()), data.size());
  if (!m_stream) {
    throw Error(
      fmt::format("failed to write data to {}: {}", m_path, strerror(errno)));
  }
}

void
AtomicFile::close()
{
  m_stream.close();
  if (x_rename(m_tmp_path.c_str(), m_path.c_str()) != 0) {
    throw Error(fmt::format("failed to rename {} to {}", m_tmp_path, m_path));
  }
}
