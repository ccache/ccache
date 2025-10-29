// Copyright (C) 2025 Joel Rosdahl and other contributors
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

#include <ccache/util/threadpool.hpp>

#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

TEST_SUITE_BEGIN("util");

TEST_CASE("ThreadPool basic functionality")
{
  SUBCASE("single task execution")
  {
    util::ThreadPool pool(2);
    std::atomic<int> counter{0};

    pool.enqueue_detach([&] { ++counter; });
    pool.shut_down();

    CHECK(counter == 1);
  }

  SUBCASE("multiple tasks execution")
  {
    util::ThreadPool pool(2);
    std::atomic<int> counter{0};
    const int num_tasks = 10;

    for (int i = 0; i < num_tasks; ++i) {
      pool.enqueue_detach([&] { ++counter; });
    }
    pool.shut_down();

    CHECK(counter == num_tasks);
  }

  SUBCASE("task execution with multiple threads")
  {
    util::ThreadPool pool(4);
    std::atomic<int> counter{0};
    const int num_tasks = 100;

    for (int i = 0; i < num_tasks; ++i) {
      pool.enqueue_detach([&] { ++counter; });
    }
    pool.shut_down();

    CHECK(counter == num_tasks);
  }

  SUBCASE("minimum thread count is 1")
  {
    util::ThreadPool pool(0); // Should create at least 1 thread
    std::atomic<bool> executed{false};

    pool.enqueue_detach([&] { executed = true; });
    pool.shut_down();

    CHECK(executed);
  }
}

TEST_CASE("ThreadPool task queue limits")
{
  SUBCASE("respects maximum queue size")
  {
    const size_t max_queue_size = 5;
    util::ThreadPool pool(1, max_queue_size);
    std::atomic<int> counter{0};

    // Fill the queue and keep the worker busy.
    std::mutex mutex;
    std::condition_variable cv;
    bool first_task_can_finish = false;

    pool.enqueue_detach([&] {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&] { return first_task_can_finish; });
    });

    // Enqueue tasks to fill the queue.
    for (size_t i = 0; i < max_queue_size; ++i) {
      pool.enqueue_detach([&] { ++counter; });
    }

    // Try to enqueue one more task in a separate thread - it should block.
    std::atomic<bool> extra_task_enqueued{false};
    std::thread enqueue_thread([&] {
      pool.enqueue_detach([&] { ++counter; });
      extra_task_enqueued = true;
    });

    // Give the enqueue thread a chance to block (not ideal but necessary).
    std::this_thread::yield();
    std::this_thread::yield();
    CHECK(!extra_task_enqueued);

    // Allow tasks to complete.
    {
      std::lock_guard<std::mutex> lock(mutex);
      first_task_can_finish = true;
    }
    cv.notify_one();
    enqueue_thread.join();

    pool.shut_down();
    CHECK(counter == max_queue_size + 1);
    CHECK(extra_task_enqueued);
  }

  SUBCASE("unlimited queue size by default")
  {
    util::ThreadPool pool(1);
    std::atomic<int> counter{0};

    // Keep worker busy.
    std::mutex mutex;
    std::condition_variable cv;
    bool can_finish = false;

    pool.enqueue_detach([&] {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&] { return can_finish; });
    });

    // Enqueue many tasks - should not block.
    const int num_tasks = 1000;
    for (int i = 0; i < num_tasks; ++i) {
      pool.enqueue_detach([&] { ++counter; });
    }

    {
      std::lock_guard<std::mutex> lock(mutex);
      can_finish = true;
    }
    cv.notify_one();
    pool.shut_down();

    CHECK(counter == num_tasks);
  }
}

TEST_CASE("ThreadPool inline execution for worker threads")
{
  SUBCASE("worker thread can enqueue without deadlock when queue is full")
  {
    const size_t max_queue_size = 2;
    util::ThreadPool pool(1, max_queue_size);
    std::atomic<int> counter{0};
    const int num_enqueues = 7;

    // Use a future to wait for the outer task to complete.
    auto outer_task_future = pool.enqueue([&]() {
      // Fill the queue from within a worker thread. When the queue is full,
      // tasks will execute inline.
      for (int i = 0; i < num_enqueues; ++i) {
        pool.enqueue_detach([&] { ++counter; });
      }
    });

    // Wait for the outer task to complete.
    outer_task_future.get();

    pool.shut_down();

    // All tasks should have executed (some inline, some from queue). The exact
    // number executed should equal the number we enqueued.
    int final_count = counter.load();
    CHECK(final_count == num_enqueues);
  }
}

TEST_CASE("ThreadPool shutdown behavior")
{
  SUBCASE("shutdown waits for all tasks to complete")
  {
    util::ThreadPool pool(2);
    std::atomic<int> counter{0};
    const int num_tasks = 10;

    // Use a condition variable to ensure tasks take some time but without
    // arbitrary sleeps.
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<int> ready_count{0};

    for (int i = 0; i < num_tasks; ++i) {
      pool.enqueue_detach([&] {
        // Signal that this task is running.
        ready_count++;
        cv.notify_all();

        // Wait briefly to simulate work.
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait_for(lock, 10ms);

        ++counter;
      });
    }

    pool.shut_down();
    CHECK(counter == num_tasks);
  }

  SUBCASE("enqueue after shutdown does nothing")
  {
    util::ThreadPool pool(2);
    std::atomic<int> counter{0};

    pool.shut_down();

    pool.enqueue_detach([&] { ++counter; });

    // No need to wait - the enqueue after shutdown should be a no-op.
    CHECK(counter == 0);
  }

  SUBCASE("multiple shutdown calls are safe")
  {
    util::ThreadPool pool(2);
    std::atomic<int> counter{0};

    pool.enqueue_detach([&] { ++counter; });

    pool.shut_down();
    pool.shut_down(); // Should be safe to call multiple times
    pool.shut_down();

    CHECK(counter == 1);
  }

  SUBCASE("destructor calls shutdown")
  {
    std::atomic<int> counter{0};

    {
      util::ThreadPool pool(2);
      pool.enqueue_detach([&] { ++counter; });
      // Destructor should call shut_down().
    }

    CHECK(counter == 1);
  }
}

TEST_CASE("ThreadPool exception handling")
{
  SUBCASE("exception in task does not crash thread pool")
  {
    util::ThreadPool pool(2);
    std::atomic<int> counter{0};

    pool.enqueue_detach([] { throw std::runtime_error("Test exception"); });

    pool.enqueue_detach([&] { ++counter; });
    pool.enqueue_detach([&] { ++counter; });

    pool.shut_down();

    // Tasks after the exception should still execute.
    CHECK(counter == 2);
  }

  SUBCASE("multiple exceptions do not crash thread pool")
  {
    util::ThreadPool pool(2);
    std::atomic<int> counter{0};

    for (int i = 0; i < 5; ++i) {
      pool.enqueue_detach([] { throw std::runtime_error("Test exception"); });
      pool.enqueue_detach([&] { ++counter; });
    }

    pool.shut_down();

    CHECK(counter == 5);
  }

  SUBCASE("unknown exception does not crash thread pool")
  {
    util::ThreadPool pool(2);
    std::atomic<int> counter{0};

    pool.enqueue_detach([] { throw 42; });
    pool.enqueue_detach([&] { ++counter; });

    pool.shut_down();

    CHECK(counter == 1);
  }
}

TEST_CASE("ThreadPool concurrent access")
{
  SUBCASE("multiple threads can safely enqueue tasks")
  {
    util::ThreadPool pool(4);
    std::atomic<int> counter{0};
    const int num_producer_threads = 8;
    const int tasks_per_thread = 50;

    std::vector<std::thread> producer_threads;
    for (int i = 0; i < num_producer_threads; ++i) {
      producer_threads.emplace_back([&] {
        for (int j = 0; j < tasks_per_thread; ++j) {
          pool.enqueue_detach([&] { ++counter; });
        }
      });
    }

    for (auto& thread : producer_threads) {
      thread.join();
    }

    pool.shut_down();

    CHECK(counter == num_producer_threads * tasks_per_thread);
  }
}

TEST_CASE("ThreadPool task ordering")
{
  SUBCASE("tasks execute in FIFO order on single thread")
  {
    util::ThreadPool pool(1);
    std::vector<int> execution_order;
    std::mutex order_mutex;

    // Keep worker busy initially.
    std::mutex start_mutex;
    std::condition_variable start_cv;
    bool can_start = false;

    pool.enqueue_detach([&] {
      std::unique_lock<std::mutex> lock(start_mutex);
      start_cv.wait(lock, [&] { return can_start; });
    });

    // Enqueue tasks in order.
    const int num_tasks = 10;
    for (int i = 0; i < num_tasks; ++i) {
      pool.enqueue_detach([&, i] {
        std::lock_guard<std::mutex> lock(order_mutex);
        execution_order.push_back(i);
      });
    }

    {
      std::lock_guard<std::mutex> lock(start_mutex);
      can_start = true;
    }
    start_cv.notify_one();
    pool.shut_down();

    REQUIRE(execution_order.size() == num_tasks);
    for (int i = 0; i < num_tasks; ++i) {
      CHECK(execution_order[i] == i);
    }
  }
}

TEST_CASE("ThreadPool enqueue")
{
  SUBCASE("simple return value")
  {
    util::ThreadPool pool(2);

    auto future = pool.enqueue([]() { return 42; });

    CHECK(future.get() == 42);
    pool.shut_down();
  }

  SUBCASE("function with arguments")
  {
    util::ThreadPool pool(2);

    auto future = pool.enqueue([](int a, int b) { return a + b; }, 10, 20);

    CHECK(future.get() == 30);
    pool.shut_down();
  }

  SUBCASE("string return type")
  {
    util::ThreadPool pool(2);

    auto future = pool.enqueue([]() { return std::string("Hello, World!"); });

    CHECK(future.get() == "Hello, World!");
    pool.shut_down();
  }

  SUBCASE("multiple futures")
  {
    util::ThreadPool pool(4);

    auto future1 = pool.enqueue([]() { return 1; });
    auto future2 = pool.enqueue([]() { return 2; });
    auto future3 = pool.enqueue([]() { return 3; });
    auto future4 = pool.enqueue([]() { return 4; });

    CHECK(future1.get() == 1);
    CHECK(future2.get() == 2);
    CHECK(future3.get() == 3);
    CHECK(future4.get() == 4);

    pool.shut_down();
  }

  SUBCASE("future with computation")
  {
    util::ThreadPool pool(2);

    auto future = pool.enqueue([]() {
      int sum = 0;
      for (int i = 1; i <= 100; ++i) {
        sum += i;
      }
      return sum;
    });

    CHECK(future.get() == 5050);
    pool.shut_down();
  }

  SUBCASE("future blocks until result is ready")
  {
    util::ThreadPool pool(1);

    // Use a condition variable to control when the task completes.
    std::mutex mutex;
    std::condition_variable cv;
    bool can_finish = false;

    auto future = pool.enqueue([&]() {
      std::unique_lock<std::mutex> lock(mutex);
      cv.wait(lock, [&] { return can_finish; });
      return 123;
    });

    // Verify that the future is not immediately ready.
    CHECK(future.wait_for(std::chrono::milliseconds(0))
          == std::future_status::timeout);

    // Allow the task to complete.
    {
      std::lock_guard<std::mutex> lock(mutex);
      can_finish = true;
    }
    cv.notify_one();

    // Now future.get() should succeed.
    int result = future.get();
    CHECK(result == 123);

    pool.shut_down();
  }

  SUBCASE("exception in future task")
  {
    util::ThreadPool pool(2);

    auto future =
      pool.enqueue([]() -> int { throw std::runtime_error("Task failed"); });

    CHECK_THROWS_AS(future.get(), std::runtime_error);

    pool.shut_down();
  }

  SUBCASE("multiple futures with different types")
  {
    util::ThreadPool pool(3);

    auto int_future = pool.enqueue([]() { return 42; });
    auto str_future = pool.enqueue([]() { return std::string("test"); });
    auto double_future = pool.enqueue([]() { return 3.14; });

    CHECK(int_future.get() == 42);
    CHECK(str_future.get() == "test");
    CHECK(double_future.get() == doctest::Approx(3.14));

    pool.shut_down();
  }

  SUBCASE("future with captured variables")
  {
    util::ThreadPool pool(2);

    int x = 10;
    int y = 20;

    auto future = pool.enqueue([x, y]() { return x * y; });

    CHECK(future.get() == 200);
    pool.shut_down();
  }

  SUBCASE("void return type")
  {
    util::ThreadPool pool(2);
    std::atomic<bool> executed{false};

    auto future = pool.enqueue([&]() { executed = true; });

    future.get(); // Should work even with void return
    CHECK(executed);

    pool.shut_down();
  }

  SUBCASE("future remains valid after shutdown")
  {
    util::ThreadPool pool(1);

    auto future = pool.enqueue([]() { return 99; });

    pool.shut_down();

    // Should still be able to get the result after shutdown.
    CHECK(future.get() == 99);
  }

  SUBCASE("parallel computation with futures")
  {
    util::ThreadPool pool(4);

    std::vector<std::future<int>> futures;
    const int num_tasks = 10;

    for (int i = 0; i < num_tasks; ++i) {
      futures.push_back(pool.enqueue([i]() { return i * i; }));
    }

    for (int i = 0; i < num_tasks; ++i) {
      CHECK(futures[i].get() == i * i);
    }

    pool.shut_down();
  }

  SUBCASE("future with reference capture")
  {
    util::ThreadPool pool(2);
    std::vector<int> data = {1, 2, 3, 4, 5};

    auto future = pool.enqueue([&]() {
      int sum = 0;
      for (int val : data) {
        sum += val;
      }
      return sum;
    });

    CHECK(future.get() == 15);
    pool.shut_down();
  }
}

TEST_SUITE_END();
