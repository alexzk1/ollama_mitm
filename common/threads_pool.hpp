#pragma once

#include "runners.h" // IWYU pragma: keep

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace utility {

/// @brief Simple threads' pool.
class CThreadPool
{
  public:
    using stopper_t = runnerint_t;

    CThreadPool() = delete;
    CThreadPool(const CThreadPool &other) = delete;
    CThreadPool &operator=(const CThreadPool &other) = delete;
    // Move constructor and assignment operator are deleted
    CThreadPool(CThreadPool &&other) = delete;
    CThreadPool &operator=(CThreadPool &&other) = delete;

    /// @brief Constructs thread pool.
    /// @param num_threads defines how many threads pool should have. Defaults to CPUs count.
    explicit CThreadPool(const std::size_t num_threads = std::thread::hardware_concurrency())
    {
        threads_.reserve(num_threads);
        stop_.reserve(num_threads);

        // Creating worker threads
        for (std::size_t i = 0; i < num_threads; ++i)
        {
            auto stopper = std::make_shared<std::atomic<bool>>(false);
            stop_.push_back(stopper);

            threads_.emplace_back(
              [this, stopper = std::move(stopper)] {
                  while (!(*stopper))
                  {
                      runner_f_t nextTaskToExec;
                      {
                          std::unique_lock lock(queue_mutex_);
                          cv_.wait(lock, [this, &stopper] {
                              return !tasks_.empty() || *stopper;
                          });
                          if (*stopper)
                          {
                              break;
                          }
                          nextTaskToExec = std::move(tasks_.front());
                          tasks_.pop();
                      }
                      assert(nextTaskToExec && "Empty task to exec was not expected here.");
                      try
                      {
                          nextTaskToExec(stopper);
                      }
                      catch (...)
                      {
                          std::cerr
                            << "Exception occurred during task execution. Cought in ThreadPool."
                            << std::endl;
                      }
                  }
              });
        }
    }

    /// @brief Destroys thread pool. Ignores and drops all not-launched yet tasks. Launched
    /// tasks will be notified to stop and tasks should respect this query.
    ~CThreadPool()
    {
        // Lock the queue to update the stop flag safely
        for (auto &sp : stop_)
        {
            *sp = true;
        }

        // Notify all threads
        cv_.notify_all();

        // Joining all worker threads to ensure they have
        // completed their tasks
        for (auto &thread : threads_)
        {
            thread.join();
        }
    }

    /// @brief Enqueue task for execution by the thread pool.
    /// @param task is task to enqueue. It should accept one parameter - stopper.
    template <typename taCallable>
    void enqueue(taCallable &&task)
    {
        static_assert(std::is_assignable_v<runner_f_t, taCallable>,
                      "Callable should accepts runnerint_t as parameter");
        static_assert(std::is_invocable_v<taCallable, stopper_t>,
                      "Callable should be invocable with runnerint_t as parameter");
        {
            const std::unique_lock lock(queue_mutex_);
            tasks_.emplace(std::forward<taCallable>(task));
        }
        cv_.notify_one();
    }

  private:
    // Vector to store worker threads
    std::vector<std::thread> threads_;
    // Flag to indicate whether the thread pool should stop
    // or not
    std::vector<stopper_t> stop_;

    // Queue of tasks
    std::queue<runner_f_t> tasks_;

    // Mutex to synchronize access to shared data
    std::mutex queue_mutex_;

    // Condition variable to signal changes in the state of
    // the tasks queue
    std::condition_variable cv_;
};
} // namespace utility
