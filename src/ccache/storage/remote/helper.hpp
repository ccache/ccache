// Copyright (C) 2025-2026 Joel Rosdahl and other contributors
//
// See doc/authors.adoc for a complete list of contributors.
//
// This program is free software; you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation; either version 3 of the License or (at your option)
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

#include <ccache/storage/remote/remotestorage.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

namespace storage::remote {

class Helper : public RemoteStorage
{
public:
  Helper(const std::filesystem::path& helper_path,
         const std::filesystem::path& temp_dir,
         std::chrono::milliseconds data_timeout,
         std::chrono::milliseconds request_timeout,
         std::chrono::milliseconds idle_timeout);

  // Special case: crsh scheme
  Helper(std::chrono::milliseconds data_timeout,
         std::chrono::milliseconds request_timeout);

  std::unique_ptr<Backend> create_backend(
    const Url& url,
    const std::vector<Backend::Attribute>& attributes) const override;

private:
  std::filesystem::path m_helper_path; // empty -> connect to existing socket
  std::filesystem::path m_temp_dir;
  std::chrono::milliseconds m_data_timeout;
  std::chrono::milliseconds m_request_timeout;
  std::chrono::milliseconds m_idle_timeout;
};

} // namespace storage::remote
