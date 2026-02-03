// Copyright (C) 2025-2026 Joel Rosdahl and other contributors
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

#include <ccache/util/expected.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/ipcchannelclient.hpp>
#include <ccache/util/noncopyable.hpp>

#include <cstddef>

namespace util {

class UnixSocketClient : public IpcChannelClient, NonCopyable
{
public:
  ~UnixSocketClient();

  tl::expected<void, IpcError>
  connect(const std::string& endpoint,
          const std::chrono::milliseconds& timeout) override;

  tl::expected<void, IpcError>
  send(std::span<const uint8_t> data,
       const std::chrono::milliseconds& timeout) override;

  tl::expected<size_t, IpcError>
  receive(std::span<uint8_t> buffer,
          const std::chrono::milliseconds& timeout) override;

  void close() override;

private:
  void do_close();

  int m_fd = -1;
};

} // namespace util
