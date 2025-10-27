// Copyright (C) 2019-2025 Joel Rosdahl and other contributors
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

#include "threadpool.hpp"

#include <ccache/util/logging.hpp>

#include <algorithm>

namespace util {

ThreadPool::ThreadPool(size_t number_of_threads, size_t task_queue_max_size)
  : m_task_queue_max_size(task_queue_max_size)
{
  size_t actual_threads = std::max<size_t>(1, number_of_threads);
  m_worker_threads.reserve(actual_threads);
  for (size_t i = 0; i < actual_threads; ++i) {
    m_worker_threads.emplace_back(&ThreadPool::worker_thread_main, this);
  }
}

ThreadPool::~ThreadPool() noexcept
{
  shut_down();
}

void
ThreadPool::enqueue(std::function<void()> function)
{
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (!m_shutting_down && m_task_queue.size() >= m_task_queue_max_size) {
      m_task_popped_condition.wait(lock, [this] {
        return m_shutting_down || m_task_queue.size() < m_task_queue_max_size;
      });
    }
    if (m_shutting_down) {
      return;
    }
    m_task_queue.emplace(std::move(function));
  }
  m_task_enqueued_or_shutting_down_condition.notify_one();
}

void
ThreadPool::shut_down() noexcept
{
  {
    std::unique_lock<std::mutex> lock(m_mutex);
    if (m_shutting_down) {
      // Already called shut_down.
      return;
    }
    m_shutting_down = true;
  }
  m_task_enqueued_or_shutting_down_condition.notify_all();
  m_task_popped_condition.notify_all();
  for (auto& thread : m_worker_threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

void
ThreadPool::worker_thread_main()
{
  while (true) {
    std::function<void()> task;

    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_task_enqueued_or_shutting_down_condition.wait(
        lock, [this] { return m_shutting_down || !m_task_queue.empty(); });
      if (m_shutting_down && m_task_queue.empty()) {
        return;
      }
      task = std::move(m_task_queue.front());
      m_task_queue.pop();
    }

    m_task_popped_condition.notify_one();
    try {
      task();
    } catch (const std::exception& e) {
      LOG("Thread pool task failed: {}", e.what());
    } catch (...) {
      LOG_RAW("Thread pool task failed with unknown exception");
    }
  }
}

} // namespace util
