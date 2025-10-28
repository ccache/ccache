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

namespace {

// Identifies which thread pool (if any) the current thread is executing a
// worker task for. Used to make enqueue() safe when called from worker threads
// by avoiding blocking on a full queue, which can otherwise deadlock if all
// workers try to enqueue while the queue is full.
thread_local ThreadPool* t_current_pool = nullptr;

void
execute_task(const std::function<void()>& task, const char* context)
{
  try {
    task();
  } catch (const std::exception& e) {
    LOG("Thread pool {} task failed: {}", context, e.what());
  } catch (...) {
    LOG("Thread pool {} task failed with unknown exception", context);
  }
}
} // namespace

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
  // Fast path for worker threads: avoid blocking on a full queue to prevent
  // deadlocks (all workers waiting inside enqueue() means no one can pop).
  std::function<void()> inline_task;
  {
    std::unique_lock<std::mutex> lock(m_mutex);

    if (m_shutting_down) {
      return;
    }

    // If called from a worker thread belonging to this pool and the queue is
    // full, execute the task inline instead of blocking.
    if (t_current_pool == this
        && m_task_queue.size() >= m_task_queue_max_size) {
      inline_task = std::move(function);
    } else {
      if (m_task_queue.size() >= m_task_queue_max_size) {
        m_producer_cv.wait(lock, [this] {
          return m_shutting_down || m_task_queue.size() < m_task_queue_max_size;
        });
      }
      if (m_shutting_down) {
        return;
      }
      m_task_queue.emplace(std::move(function));
      // Wake a worker to process the enqueued task.
      m_worker_cv.notify_one();
    }
  }

  if (inline_task) {
    execute_task(inline_task, "inline");
  }
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
  m_worker_cv.notify_all();
  m_producer_cv.notify_all();
  for (auto& thread : m_worker_threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }
}

void
ThreadPool::worker_thread_main()
{
  // Mark the current thread as a worker of this pool for the duration of its
  // lifetime to allow enqueue() to detect re-entrancy safely.
  t_current_pool = this;
  while (true) {
    std::function<void()> task;

    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_worker_cv.wait(
        lock, [this] { return m_shutting_down || !m_task_queue.empty(); });
      if (m_shutting_down && m_task_queue.empty()) {
        return;
      }
      task = std::move(m_task_queue.front());
      m_task_queue.pop();
      // Notify any threads blocked in enqueue() that space is now available.
      // Notifying while holding the lock ensures the waiting thread sees the
      // updated queue size immediately upon waking.
      m_producer_cv.notify_one();
    }

    execute_task(task, "worker");
  }
}

} // namespace util
