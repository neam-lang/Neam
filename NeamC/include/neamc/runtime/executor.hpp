// SPDX-License-Identifier: Apache-2.0
//
// Neam Runtime - Executor
//

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "neamc/runtime/future.hpp"
#include "neamc/runtime/result.hpp"
#include "neamc/runtime/thread_pool.hpp"

namespace neamc::runtime
{
enum class TaskPriority
{
  kLow = 0,
  kNormal = 1,
  kHigh = 2,
  kCritical = 3
};

enum class TaskState
{
  kPending,
  kRunning,
  kCompleted,
  kCancelled,
  kErrored
};

struct TaskMetrics
{
  uint64_t task_id{0};
  std::chrono::steady_clock::time_point queued_at{};
  std::chrono::steady_clock::time_point started_at{};
  std::chrono::steady_clock::time_point completed_at{};
  TaskState final_state{TaskState::kPending};
};

struct ExecutorConfig
{
  size_t thread_count{std::thread::hardware_concurrency()};
  size_t max_queue_size{10000};
  bool enable_work_stealing{true};
  bool enable_metrics{false};
  std::chrono::milliseconds idle_timeout{std::chrono::milliseconds{5000}};
};

class Executor
{
public:
  explicit Executor(ExecutorConfig config = {});
  ~Executor();

  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  template <typename T>
  Future<T> spawn(std::function<T()> work, TaskPriority priority = TaskPriority::kNormal)
  {
    auto task = std::make_shared<Task<T>>(std::move(work), priority);
    // Create a new future sharing the same state — we can't move task->future
    // because execute() later needs task->future.state_ to resolve/reject.
    Future<T> result;
    result.state_ = task->future.state_;
    if (!schedule_task(task))
    {
      task->future.reject(typename Future<T>::ErrorType{std::string("Task queue full")});
    }
    return result;
  }

  template <typename T>
  Future<T> spawn_blocking(std::function<T()> work)
  {
    return spawn(std::move(work), TaskPriority::kNormal);
  }

  template <typename T>
  Future<std::vector<T>> all(std::vector<Future<T>> futures)
  {
    auto result_future = Future<std::vector<T>>();
    auto shared_state = result_future.state_;
    auto results = std::make_shared<std::vector<std::optional<T>>>(futures.size());
    auto remaining = std::make_shared<std::atomic<size_t>>(futures.size());
    if (futures.empty())
    {
      result_future.resolve({});
      return result_future;
    }

    for (size_t i = 0; i < futures.size(); ++i)
    {
      futures[i].add_continuation([shared_state, results, remaining, i](typename Future<T>::ResultType result) {
        if (result.is_ok())
        {
          (*results)[i] = std::move(result).unwrap();
        }
        else
        {
          Future<std::vector<T>>::reject_shared(shared_state, std::move(result).unwrap_err());
          return;
        }
        if (remaining->fetch_sub(1) == 1)
        {
          std::vector<T> out;
          out.reserve(results->size());
          for (auto& item : *results)
          {
            if (item.has_value())
            {
              out.push_back(std::move(*item));
            }
          }
          Future<std::vector<T>>::resolve_shared(shared_state, std::move(out));
        }
      });
    }

    return result_future;
  }

  template <typename T>
  Future<T> race(std::vector<Future<T>> futures)
  {
    auto result_future = Future<T>();
    auto shared_state = result_future.state_;
    auto completed = std::make_shared<std::atomic<bool>>(false);
    for (auto& future : futures)
    {
      future.add_continuation([shared_state, completed](typename Future<T>::ResultType result) {
        bool expected = false;
        if (!completed->compare_exchange_strong(expected, true))
        {
          return;
        }
        if (result.is_ok())
        {
          Future<T>::resolve_shared(shared_state, std::move(result).unwrap());
        }
        else
        {
          Future<T>::reject_shared(shared_state, std::move(result).unwrap_err());
        }
      });
    }
    return result_future;
  }

  template <typename T>
  Future<Result<T, TimeoutError>> timeout(Future<T> future, std::chrono::milliseconds duration)
  {
    auto result_future = Future<Result<T, TimeoutError>>();
    auto shared_state = result_future.state_;
    auto timeout_flag = std::make_shared<std::atomic<bool>>(false);

    auto scheduled = schedule_task(std::make_shared<Task<void>>([shared_state, timeout_flag, duration]() {
      std::this_thread::sleep_for(duration);
      bool expected = false;
      if (timeout_flag->compare_exchange_strong(expected, true))
      {
        Future<Result<T, TimeoutError>>::resolve_shared(shared_state,
                                                      Result<T, TimeoutError>::Err(TimeoutError{duration, "timeout"}));
      }
    }, TaskPriority::kLow));
    if (!scheduled)
    {
      Future<Result<T, TimeoutError>>::resolve_shared(shared_state,
                                                      Result<T, TimeoutError>::Err(TimeoutError{duration, "timeout"}));
    }

    future.add_continuation([shared_state, timeout_flag](typename Future<T>::ResultType result) {
      bool expected = false;
      if (!timeout_flag->compare_exchange_strong(expected, true))
      {
        return;
      }
      if (result.is_ok())
      {
        Future<Result<T, TimeoutError>>::resolve_shared(shared_state,
                                                        Result<T, TimeoutError>::Ok(std::move(result).unwrap()));
      }
      else
      {
        Future<Result<T, TimeoutError>>::resolve_shared(shared_state,
                                                        Result<T, TimeoutError>::Err(TimeoutError{std::chrono::milliseconds{0}, "error"}));
      }
    });

    return result_future;
  }

  void shutdown();
  void shutdown_now();
  bool is_running() const;

  size_t pending_tasks() const;
  size_t active_workers() const;
  std::vector<TaskMetrics> get_metrics() const;

private:
  struct TaskBase
  {
    virtual ~TaskBase() = default;
    virtual void execute() = 0;
    virtual TaskPriority priority() const = 0;
    TaskMetrics metrics{};
  };

  template <typename T>
  struct Task : TaskBase
  {
    Task(std::function<T()> work, TaskPriority priority)
        : work(std::move(work)), priority_level(priority)
    {
      this->metrics.task_id = next_id.fetch_add(1, std::memory_order_relaxed);
      this->metrics.queued_at = std::chrono::steady_clock::now();
    }

    void execute() override
    {
      if (cancelled.load(std::memory_order_relaxed))
      {
        Future<T>::reject_shared(future.state_, typename Future<T>::ErrorType{CancellationError{"cancelled"}});
        this->metrics.final_state = TaskState::kCancelled;
        this->metrics.completed_at = std::chrono::steady_clock::now();
        return;
      }
      this->metrics.started_at = std::chrono::steady_clock::now();
      try
      {
        if constexpr (std::is_same_v<T, void>)
        {
          work();
          Future<void>::resolve_shared(future.state_);
        }
        else
        {
          Future<T>::resolve_shared(future.state_, work());
        }
        this->metrics.final_state = TaskState::kCompleted;
      }
      catch (const std::exception& ex)
      {
        Future<T>::reject_shared(future.state_, std::string(ex.what()));
        this->metrics.final_state = TaskState::kErrored;
      }
      catch (...)
      {
        Future<T>::reject_shared(future.state_, std::string("Unknown error"));
        this->metrics.final_state = TaskState::kErrored;
      }
      this->metrics.completed_at = std::chrono::steady_clock::now();
    }

    TaskPriority priority() const override { return priority_level; }

    std::function<T()> work;
    TaskPriority priority_level;
    std::atomic<bool> cancelled{false};
    Future<T> future{};
    static inline std::atomic<uint64_t> next_id{1};
  };

  bool schedule_task(const std::shared_ptr<TaskBase>& task);

  ExecutorConfig config_{};
  ThreadPool thread_pool_{};
  std::atomic<bool> running_{true};
  std::atomic<size_t> pending_{0};
  mutable std::mutex metrics_mutex_{};
  std::vector<TaskMetrics> metrics_{};
};

Executor& global_executor();
void set_global_executor(std::unique_ptr<Executor> executor);
}  // namespace neamc::runtime
