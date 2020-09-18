// Copyright (C) 2019-2020 Joel Rosdahl and other contributors
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

#include "TemporaryFile.hpp"
#include "Util.hpp"
#include "assertions.hpp"
#include "exceptions.hpp"

AtomicFile::AtomicFile(const std::string& path, Mode mode) : m_path(path)
{
  TemporaryFile tmp_file(path + ".tmp");
  m_stream = fdopen(tmp_file.fd.release(), mode == Mode::binary ? "w+b" : "w+");
  m_tmp_path = std::move(tmp_file.path);
}

AtomicFile::~AtomicFile()
{
  if (m_stream) {
    // commit() was not called so remove the lingering temporary file.
    fclose(m_stream);
    Util::unlink_tmp(m_tmp_path);
  }
}

void
AtomicFile::write(const std::string& data)
{
  if (fwrite(data.data(), data.size(), 1, m_stream) != 1) {
    throw Error("failed to write data to {}: {}", m_path, strerror(errno));
  }
}

void
AtomicFile::write(const std::vector<uint8_t>& data)
{
  if (fwrite(data.data(), data.size(), 1, m_stream) != 1) {
    throw Error("failed to write data to {}: {}", m_path, strerror(errno));
  }
}

void
AtomicFile::commit()
{
  ASSERT(m_stream);
  int result = fclose(m_stream);
  m_stream = nullptr;
  if (result == EOF) {
    Util::unlink_tmp(m_tmp_path);
    throw Error("failed to write data to {}: {}", m_path, strerror(errno));
  }
  Util::rename(m_tmp_path, m_path);
}
