//
// Neam Virtual Machine - Async Executor
//

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>
#include <string>

#include "neamc/vm/async/future.hpp"

namespace neamc::vm::async
{
struct TimeoutError
{
  int code{5000};
  std::string message{"timeout"};
};

template <typename T, typename E>
struct Result
{
  bool ok{false};
  std::optional<T> value{};
  std::optional<E> error{};

  static Result ok_value(T val)
  {
    Result out;
    out.ok = true;
    out.value = std::move(val);
    return out;
  }

  static Result error_value(E err)
  {
    Result out;
    out.ok = false;
    out.error = std::move(err);
    return out;
  }
};

enum class Priority
{
  Low = 0,
  Normal = 1,
  High = 2
};

class Executor
{
public:
  explicit Executor(std::size_t thread_count = 4) : stop_(false)
  {
    if (thread_count == 0)
    {
      thread_count = 1;
    }
    for (std::size_t i = 0; i < thread_count; ++i)
    {
      workers_.emplace_back([this]() { worker_loop(); });
    }
  }

  ~Executor()
  {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& thread : workers_)
    {
      if (thread.joinable())
      {
        thread.join();
      }
    }
  }

  Executor(const Executor&) = delete;
  Executor& operator=(const Executor&) = delete;

  template <typename Func>
  auto submit(Func&& func) -> Future<std::invoke_result_t<Func>>
  {
    return enqueue(std::forward<Func>(func), Priority::Normal);
  }

  template <typename Func>
  auto spawn(Func&& func, Priority priority = Priority::Normal)
      -> Future<std::invoke_result_t<Func>>
  {
    return enqueue(std::forward<Func>(func), priority);
  }

  template <typename... Futures>
  auto all(Futures&&... futures)
      -> Future<std::tuple<typename std::decay_t<Futures>::value_type...>>
  {
    using TupleT = std::tuple<typename std::decay_t<Futures>::value_type...>;
    auto state = std::make_shared<SharedState<TupleT>>();
    auto result_future = Future<TupleT>(state);
    auto remaining = std::make_shared<std::atomic<std::size_t>>(sizeof...(Futures));
    auto values = std::make_shared<TupleT>();

    attach_all(state, remaining, values, std::index_sequence_for<Futures...>{},
               std::forward<Futures>(futures)...);
    return result_future;
  }

  template <typename FutureT>
  auto race(std::vector<FutureT> futures) -> Future<typename FutureT::value_type>
  {
    using ValueT = typename FutureT::value_type;
    auto state = std::make_shared<SharedState<ValueT>>();
    auto result_future = Future<ValueT>(state);
    auto completed = std::make_shared<std::atomic<bool>>(false);

    for (auto& future : futures)
    {
      future.then([state, completed](ValueT value) mutable {
        bool expected = false;
        if (completed->compare_exchange_strong(expected, true))
        {
          {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->value = std::move(value);
            state->state = FutureState::kCompleted;
          }
          state->cv.notify_all();
        }
        return 0;
      });
    }
    return result_future;
  }

  template <typename T>
  Result<T, TimeoutError> with_timeout(Future<T>& future, std::chrono::milliseconds timeout)
  {
    auto value = future.wait_for(timeout);
    if (!value)
    {
      return Result<T, TimeoutError>::error_value(TimeoutError{});
    }
    return Result<T, TimeoutError>::ok_value(*value);
  }

  static Executor& global()
  {
    static Executor executor;
    return executor;
  }

private:
  template <typename TupleT, typename... Futures, std::size_t... Index>
  void attach_all(const std::shared_ptr<SharedState<TupleT>>& state,
                  const std::shared_ptr<std::atomic<std::size_t>>& remaining,
                  const std::shared_ptr<TupleT>& values, std::index_sequence<Index...>,
                  Futures&&... futures)
  {
    auto attach = [state, remaining, values](auto idx, auto future) {
      using ValueT = typename std::decay_t<decltype(future)>::value_type;
      future.then([state, remaining, values, idx](ValueT value) mutable {
        std::get<idx>(*values) = std::move(value);
        if (remaining->fetch_sub(1) == 1)
        {
          {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->value = std::move(*values);
            state->state = FutureState::kCompleted;
          }
          state->cv.notify_all();
        }
        return 0;
      });
    };
    (attach(std::integral_constant<std::size_t, Index>{}, std::forward<Futures>(futures)), ...);
  }

  struct Task
  {
    int priority{0};
    std::function<void()> fn;
    std::size_t order{0};
  };

  struct TaskCompare
  {
    bool operator()(const Task& lhs, const Task& rhs) const
    {
      if (lhs.priority == rhs.priority)
      {
        return lhs.order > rhs.order;
      }
      return lhs.priority < rhs.priority;
    }
  };

  template <typename Func>
  auto enqueue(Func&& func, Priority priority) -> Future<std::invoke_result_t<Func>>
  {
    using ResultT = std::invoke_result_t<Func>;
    auto state = std::make_shared<SharedState<ResultT>>();
    Future<ResultT> future(state);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      tasks_.push(Task{static_cast<int>(priority),
                       [state, fn = std::forward<Func>(func)]() mutable {
                         {
                           std::lock_guard<std::mutex> lock(state->mutex);
                           state->state = FutureState::kRunning;
                         }
                         try
                         {
                           ResultT value = fn();
                           {
                             std::lock_guard<std::mutex> lock(state->mutex);
                             state->value = std::move(value);
                             state->state = FutureState::kCompleted;
                           }
                         }
                         catch (...)
                         {
                           std::lock_guard<std::mutex> lock(state->mutex);
                           state->state = FutureState::kFailed;
                           state->error = std::current_exception();
                         }
                         state->cv.notify_all();
                         std::vector<std::function<void()>> continuations;
                         {
                           std::lock_guard<std::mutex> lock(state->mutex);
                           continuations.swap(state->continuations);
                         }
                         for (auto& cont : continuations)
                         {
                           cont();
                         }
                       },
                       order_++});
    }
    cv_.notify_one();
    return future;
  }

  void worker_loop()
  {
    while (true)
    {
      Task task;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this]() { return stop_ || !tasks_.empty(); });
        if (stop_ && tasks_.empty())
        {
          return;
        }
        task = tasks_.top();
        tasks_.pop();
      }
      if (task.fn)
      {
        task.fn();
      }
    }
  }

  std::mutex mutex_{};
  std::condition_variable cv_{};
  std::priority_queue<Task, std::vector<Task>, TaskCompare> tasks_{};
  std::vector<std::thread> workers_{};
  std::atomic<bool> stop_{false};
  std::atomic<std::size_t> order_{0};
};
}  // namespace neamc::vm::async
