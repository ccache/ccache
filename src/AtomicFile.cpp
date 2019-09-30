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
#include "Util.hpp"
#include "ccache.hpp"

#include <cassert>
#include <cerrno>
#include <fmt/core.h>
#include <unistd.h>

AtomicFile::AtomicFile(const std::string& path, Mode mode) : m_path(path)
{
  auto fd_and_path = Util::create_temp_fd(path + ".tmp");
  m_stream = fdopen(fd_and_path.first, mode == Mode::binary ? "w+b" : "w+");
  m_tmp_path = std::move(fd_and_path.second);
}

AtomicFile::~AtomicFile()
{
  if (m_stream) {
    // close() was not called so remove the lingering temporary file.
    fclose(m_stream);
    tmp_unlink(m_tmp_path.c_str());
  }
}

void
AtomicFile::write(const std::string& data)
{
  if (fwrite(data.data(), data.size(), 1, m_stream) != 1) {
    throw Error(
      fmt::format("failed to write data to {}: {}", m_path, strerror(errno)));
  }
}

void
AtomicFile::write(const std::vector<uint8_t>& data)
{
  if (fwrite(data.data(), data.size(), 1, m_stream) != 1) {
    throw Error(
      fmt::format("failed to write data to {}: {}", m_path, strerror(errno)));
  }
}

void
AtomicFile::close()
{
  assert(m_stream);
  fclose(m_stream);
  m_stream = nullptr;
  if (x_rename(m_tmp_path.c_str(), m_path.c_str()) != 0) {
    throw Error(fmt::format("failed to rename {} to {}", m_tmp_path, m_path));
  }
}
