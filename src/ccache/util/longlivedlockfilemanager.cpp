// Copyright (C) 2022-2024 Joel Rosdahl and other contributors
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

#include "longlivedlockfilemanager.hpp"

#include <ccache/util/file.hpp>
#include <ccache/util/filesystem.hpp>
#include <ccache/util/logging.hpp>

#include <chrono>

namespace fs = util::filesystem;

#ifndef _WIN32
std::chrono::milliseconds k_keep_alive_interval{500};
#endif

namespace util {

LongLivedLockFileManager::~LongLivedLockFileManager()
{
#ifndef _WIN32
  if (m_thread.joinable()) {
    LOG_RAW("Stopping keep-alive thread");
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_stop = true;
    }
    m_stop_condition.notify_one();
    m_thread.join();
    LOG_RAW("Stopped keep-alive thread");
  }
#endif
}

void
LongLivedLockFileManager::register_alive_file(
  [[maybe_unused]] const fs::path& path)
{
#ifndef _WIN32
  std::unique_lock<std::mutex> lock(m_mutex);
  if (!m_thread.joinable()) {
    start_thread();
  }
  m_alive_files.insert(path);
#endif
}

void
LongLivedLockFileManager::deregister_alive_file(
  [[maybe_unused]] const fs::path& path)
{
#ifndef _WIN32
  std::unique_lock<std::mutex> lock(m_mutex);
  m_alive_files.erase(path);
#endif
}

#ifndef _WIN32
void
LongLivedLockFileManager::start_thread()
{
  LOG_RAW("Starting keep-alive thread");
  m_thread = std::thread([&] {
    auto awake_time = std::chrono::steady_clock::now();
    while (true) {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_stop_condition.wait_until(lock, awake_time, [this] { return m_stop; });
      if (m_stop) {
        return;
      }
      for (const auto& alive_file : m_alive_files) {
        set_timestamps(alive_file);
      }
      awake_time += k_keep_alive_interval;
    }
  });
  LOG_RAW("Started keep-alive thread");
}
#endif

} // namespace util
