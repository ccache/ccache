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

#pragma once

#include <ccache/util/noncopyable.hpp>

#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>

namespace util {

class LongLivedLockFileManager : util::NonCopyable
{
public:
  LongLivedLockFileManager() = default;
  ~LongLivedLockFileManager();

  void register_alive_file(const std::filesystem::path& path);
  void deregister_alive_file(const std::filesystem::path& path);

private:
#ifndef _WIN32
  std::thread m_thread;
  std::mutex m_mutex;
  std::condition_variable m_stop_condition;
  bool m_stop = false;
  std::set<std::filesystem::path> m_alive_files;

  void start_thread();
#endif
};

} // namespace util
