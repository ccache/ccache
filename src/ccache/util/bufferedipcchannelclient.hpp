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

#include <ccache/util/bytes.hpp>
#include <ccache/util/expected.hpp>
#include <ccache/util/ipcchannelclient.hpp>
#include <ccache/util/noncopyable.hpp>

#include <algorithm>
#include <vector>

namespace util {

template<typename Transport>
class BufferedIpcChannelClient : public IpcChannelClient, NonCopyable
{
public:
  template<typename... Args>
  explicit BufferedIpcChannelClient(Args&&... args)
    : m_transport(std::forward<Args>(args)...)
  {
  }

  tl::expected<void, IpcError>
  connect(const std::string& path,
          const std::chrono::milliseconds& timeout) override
  {
    return m_transport.connect(path, timeout);
  }

  tl::expected<void, IpcError>
  send(nonstd::span<const uint8_t> data,
       const std::chrono::milliseconds& timeout) override
  {
    return m_transport.send(data, timeout);
  }

  tl::expected<size_t, IpcError>
  receive(nonstd::span<uint8_t> buffer,
          const std::chrono::milliseconds& timeout) override
  {
    if (buffer.empty()) {
      return 0;
    }

    size_t total_read = 0;

    // First, serve any data from our internal buffer.
    if (!m_buffer.empty()) {
      const size_t n = std::min(buffer.size(), m_buffer.size());
      std::copy(m_buffer.begin(), m_buffer.begin() + n, buffer.data());
      m_buffer.erase(m_buffer.begin(), m_buffer.begin() + n);
      total_read += n;
      if (total_read == buffer.size()) {
        return total_read;
      }
    }

    const auto remaining_buffer = buffer.subspan(total_read);

    // For large reads, read directly into user buffer.
    if (remaining_buffer.size() >= k_buffer_size) {
      TRY_ASSIGN(size_t n, m_transport.receive(remaining_buffer, timeout));
      return total_read + n;
    }

    // For small reads, read into our internal buffer and serve from there.
    if (m_buffer.empty()) {
      m_buffer.resize(k_buffer_size);
      auto result = m_transport.receive(m_buffer, timeout);
      if (!result) {
        m_buffer.clear();
        return result;
      }

      // Resize buffer to actual amount read.
      m_buffer.resize(*result);
    }

    // Serve remaining data.
    const size_t n = std::min(remaining_buffer.size(), m_buffer.size());
    std::copy(m_buffer.begin(), m_buffer.begin() + n, remaining_buffer.data());
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + n);

    return total_read + n;
  }

  void
  close() override
  {
    m_buffer.clear();
    m_transport.close();
  }

private:
  static constexpr size_t k_buffer_size = 256;

  Transport m_transport;
  util::Bytes m_buffer;
};

} // namespace util
