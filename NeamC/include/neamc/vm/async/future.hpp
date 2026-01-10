//
// Neam Virtual Machine - Async Future
//

#pragma once

#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace neamc::vm::async
{
class Executor;

enum class FutureState
{
  kPending,
  kRunning,
  kCompleted,
  kCancelled,
  kFailed
};

template <typename T>
struct SharedState
{
  mutable std::mutex mutex{};
  std::condition_variable cv{};
  FutureState state{FutureState::kPending};
  std::optional<T> value{};
  std::exception_ptr error{};
  std::vector<std::function<void()>> continuations{};
};

template <typename T>
class Future
{
public:
  using value_type = T;
  Future() = default;
  explicit Future(std::shared_ptr<SharedState<T>> state) : state_(std::move(state)) {}

  bool is_ready() const
  {
    if (!state_)
    {
      return false;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->state == FutureState::kCompleted || state_->state == FutureState::kCancelled ||
           state_->state == FutureState::kFailed;
  }

  T wait()
  {
    ensure_state();
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->cv.wait(lock, [this]() {
      return state_->state == FutureState::kCompleted ||
             state_->state == FutureState::kCancelled || state_->state == FutureState::kFailed;
    });
    if (state_->state == FutureState::kFailed && state_->error)
    {
      std::rethrow_exception(state_->error);
    }
    if (!state_->value.has_value())
    {
      throw std::runtime_error("Future has no value");
    }
    return *state_->value;
  }

  std::optional<T> wait_for(std::chrono::milliseconds timeout)
  {
    ensure_state();
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (!state_->cv.wait_for(lock, timeout, [this]() {
          return state_->state == FutureState::kCompleted ||
                 state_->state == FutureState::kCancelled || state_->state == FutureState::kFailed;
        }))
    {
      return std::nullopt;
    }
    if (state_->state == FutureState::kFailed && state_->error)
    {
      std::rethrow_exception(state_->error);
    }
    return state_->value;
  }

  std::optional<T> try_get() const
  {
    if (!state_)
    {
      return std::nullopt;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->state != FutureState::kCompleted)
    {
      return std::nullopt;
    }
    return state_->value;
  }

  template <typename Func>
  auto then(Func&& func) -> Future<std::invoke_result_t<Func, T>>
  {
    using ResultT = std::invoke_result_t<Func, T>;
    auto next_state = std::make_shared<SharedState<ResultT>>();
    auto next_future = Future<ResultT>(next_state);
    ensure_state();

    auto continuation = [state = state_, next_state, fn = std::forward<Func>(func)]() mutable {
      std::optional<T> value;
      std::exception_ptr error;
      {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (state->state == FutureState::kFailed)
        {
          error = state->error;
        }
        else if (state->state == FutureState::kCompleted)
        {
          value = state->value;
        }
        else
        {
          error = std::make_exception_ptr(std::runtime_error("Future not completed"));
        }
      }

      {
        std::lock_guard<std::mutex> next_lock(next_state->mutex);
        if (error)
        {
          next_state->state = FutureState::kFailed;
          next_state->error = error;
        }
        else if (!value.has_value())
        {
          next_state->state = FutureState::kFailed;
          next_state->error =
              std::make_exception_ptr(std::runtime_error("Future missing value"));
        }
        else
        {
          try
          {
            next_state->value = fn(*value);
            next_state->state = FutureState::kCompleted;
          }
          catch (...)
          {
            next_state->state = FutureState::kFailed;
            next_state->error = std::current_exception();
          }
        }
      }
      next_state->cv.notify_all();
    };

    bool run_now = false;
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->state == FutureState::kCompleted || state_->state == FutureState::kFailed ||
          state_->state == FutureState::kCancelled)
      {
        run_now = true;
      }
      else
      {
        state_->continuations.push_back(std::move(continuation));
      }
    }
    if (run_now)
    {
      continuation();
    }
    return next_future;
  }

  void cancel()
  {
    if (!state_)
    {
      return;
    }
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->state == FutureState::kCompleted || state_->state == FutureState::kFailed)
      {
        return;
      }
      state_->state = FutureState::kCancelled;
    }
    state_->cv.notify_all();
  }

private:
  void ensure_state() const
  {
    if (!state_)
    {
      throw std::runtime_error("Future has no shared state");
    }
  }

  std::shared_ptr<SharedState<T>> state_{};

  friend class Executor;
};

template <typename T>
Future<T> make_ready_future(T value)
{
  auto state = std::make_shared<SharedState<T>>();
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->value = std::move(value);
    state->state = FutureState::kCompleted;
  }
  state->cv.notify_all();
  return Future<T>(state);
}
}  // namespace neamc::vm::async
