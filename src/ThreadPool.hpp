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

#pragma once

#include "system.hpp"

#include <condition_variable>
#include <functional>
#include <limits>
#include <mutex>
#include <queue>
#include <thread>

class ThreadPool
{
public:
  explicit ThreadPool(
    size_t number_of_threads,
    size_t task_queue_max_size = std::numeric_limits<size_t>::max());
  ~ThreadPool();

  void enqueue(std::function<void()> function);
  void shut_down();

private:
  std::vector<std::thread> m_worker_threads;
  std::queue<std::function<void()>> m_task_queue;
  size_t m_task_queue_max_size;
  bool m_shutting_down = false;
  std::mutex m_mutex;
  std::condition_variable m_task_enqueued_or_shutting_down_condition;
  std::condition_variable m_task_popped_condition;

  void worker_thread_main();
};
