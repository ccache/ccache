// Copyright (C) 2019-2023 Joel Rosdahl and other contributors
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

#include <assertions.hpp>
#include <core/AtomicFile.hpp>
#include <core/exceptions.hpp>
#include <fmtmacros.hpp>
#include <util/TemporaryFile.hpp>
#include <util/expected.hpp>
#include <util/file.hpp>
#include <util/filesystem.hpp>

namespace fs = util::filesystem;

namespace core {

AtomicFile::AtomicFile(const fs::path& path, Mode mode) : m_path(path)
{
  auto tmp_file =
    util::value_or_throw<core::Fatal>(util::TemporaryFile::create(path));
  m_stream = fdopen(tmp_file.fd.release(), mode == Mode::binary ? "w+b" : "w+");
  m_tmp_path = std::move(tmp_file.path);
}

AtomicFile::~AtomicFile()
{
  if (m_stream) {
    // commit() was not called so remove the lingering temporary file.
    fclose(m_stream);
    util::remove(m_tmp_path);
  }
}

void
AtomicFile::write(std::string_view data)
{
  if (fwrite(data.data(), data.size(), 1, m_stream) != 1) {
    throw core::Error(
      FMT("failed to write data to {}: {}", m_path, strerror(errno)));
  }
}

void
AtomicFile::write(nonstd::span<const uint8_t> data)
{
  if (fwrite(data.data(), data.size(), 1, m_stream) != 1) {
    throw core::Error(
      FMT("failed to write data to {}: {}", m_path, strerror(errno)));
  }
}

void
AtomicFile::flush()
{
  if (fflush(m_stream) != 0) {
    throw core::Error(
      FMT("failed to flush data to {}: {}", m_path, strerror(errno)));
  }
}

void
AtomicFile::commit()
{
  ASSERT(m_stream);
  int retcode = fclose(m_stream);
  m_stream = nullptr;
  if (retcode == EOF) {
    util::remove(m_tmp_path);
    throw core::Error(
      FMT("failed to write data to {}: {}", m_path, strerror(errno)));
  }
  const auto result = fs::rename(m_tmp_path, m_path);
  if (!result) {
    throw core::Error(FMT("failed to rename {} to {}: {}",
                          m_tmp_path,
                          m_path,
                          result.error().message()));
  }
}

} // namespace core
