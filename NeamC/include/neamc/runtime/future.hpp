//
// Neam Runtime - Future
//

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "neamc/runtime/result.hpp"

namespace neamc::runtime
{
class Executor;

// Forward declaration of void specialization
template <typename T>
class Future;

template <>
class Future<void>;

struct TimeoutError
{
  std::chrono::milliseconds duration{0};
  std::string message{"timeout"};
};

struct CancellationError
{
  std::string reason;
};

template <typename T>
class Future
{
public:
  using ValueType = T;
  using ErrorType = std::variant<std::string, TimeoutError, CancellationError>;
  using ResultType = Result<T, ErrorType>;

  enum class State
  {
    kPending,
    kReady,
    kErrored,
    kCancelled
  };

  Future() : state_(std::make_shared<SharedState>()) {}
  explicit Future(T value) : state_(std::make_shared<SharedState>())
  {
    resolve(std::move(value));
  }
  explicit Future(ErrorType error) : state_(std::make_shared<SharedState>())
  {
    reject(std::move(error));
  }

  Future(Future&& other) noexcept : state_(std::move(other.state_)) {}
  Future& operator=(Future&& other) noexcept
  {
    if (this != &other)
    {
      state_ = std::move(other.state_);
    }
    return *this;
  }
  Future(const Future&) = delete;
  Future& operator=(const Future&) = delete;

  State state() const
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->state;
  }

  bool is_pending() const { return state() == State::kPending; }
  bool is_ready() const { return state() == State::kReady; }
  bool is_errored() const { return state() == State::kErrored; }
  bool is_cancelled() const { return state() == State::kCancelled; }

  ResultType wait()
  {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->cv.wait(lock, [this]() { return state_->state != State::kPending; });
    return result_locked();
  }

  ResultType wait_for(std::chrono::milliseconds timeout)
  {
    std::unique_lock<std::mutex> lock(state_->mutex);
    if (!state_->cv.wait_for(lock, timeout, [this]() { return state_->state != State::kPending; }))
    {
      return ResultType::Err(TimeoutError{timeout, "timeout"});
    }
    return result_locked();
  }

  std::optional<ResultType> try_get()
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->state == State::kPending)
    {
      return std::nullopt;
    }
    return result_locked();
  }

  template <typename F>
  auto then(F&& func) -> Future<std::invoke_result_t<F, T>>
  {
    using NextT = std::invoke_result_t<F, T>;
    auto next = Future<NextT>();
    add_continuation([next_state = next.state_, fn = std::forward<F>(func)](ResultType result) mutable {
      if (result.is_ok())
      {
        try
        {
          NextT value = fn(std::move(result).unwrap());
          Future<NextT>::resolve_shared(next_state, std::move(value));
        }
        catch (const std::exception& ex)
        {
          Future<NextT>::reject_shared(next_state, std::string(ex.what()));
        }
        catch (...)
        {
          Future<NextT>::reject_shared(next_state, std::string("Unknown error"));
        }
      }
      else
      {
        Future<NextT>::reject_shared(next_state, std::move(result).unwrap_err());
      }
    });
    return next;
  }

  template <typename F>
  auto map(F&& func) -> Future<std::invoke_result_t<F, T>>
  {
    return then(std::forward<F>(func));
  }

  template <typename F>
  auto flat_map(F&& func) -> std::invoke_result_t<F, T>;

  template <typename F>
  Future<T> on_error(F&& handler)
  {
    auto next = Future<T>();
    add_continuation([next_state = next.state_, fn = std::forward<F>(handler)](ResultType result) mutable {
      if (result.is_ok())
      {
        Future<T>::resolve_shared(next_state, std::move(result).unwrap());
      }
      else
      {
        fn(std::move(result).unwrap_err());
        Future<T>::reject_shared(next_state, std::move(result).unwrap_err());
      }
    });
    return next;
  }

  template <typename F>
  Future<T> recover(F&& recovery)
  {
    auto next = Future<T>();
    add_continuation([next_state = next.state_, fn = std::forward<F>(recovery)](ResultType result) mutable {
      if (result.is_ok())
      {
        Future<T>::resolve_shared(next_state, std::move(result).unwrap());
      }
      else
      {
        try
        {
          T value = fn(std::move(result).unwrap_err());
          Future<T>::resolve_shared(next_state, std::move(value));
        }
        catch (const std::exception& ex)
        {
          Future<T>::reject_shared(next_state, std::string(ex.what()));
        }
        catch (...)
        {
          Future<T>::reject_shared(next_state, std::string("Unknown error"));
        }
      }
    });
    return next;
  }

  void cancel(std::string reason = "")
  {
    std::vector<std::function<void(ResultType)>> continuations;
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->state != State::kPending)
      {
        return;
      }
      state_->cancellation_requested.store(true, std::memory_order_relaxed);
      state_->state = State::kCancelled;
      state_->error = CancellationError{std::move(reason)};
      continuations.swap(state_->continuations);
    }
    state_->cv.notify_all();
    for (auto& cont : continuations)
    {
      cont(ResultType::Err(*state_->error));
    }
  }

  bool is_cancellation_requested() const
  {
    return state_->cancellation_requested.load(std::memory_order_relaxed);
  }

  void resolve(T value)
  {
    resolve_shared(state_, std::move(value));
  }

  void reject(ErrorType error)
  {
    reject_shared(state_, std::move(error));
  }

  void add_continuation(std::function<void(ResultType)> cont)
  {
    bool run_now = false;
    std::optional<ResultType> result;
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (state_->state == State::kPending)
      {
        state_->continuations.push_back(std::move(cont));
        return;
      }
      run_now = true;
      result = result_locked();
    }
    if (run_now)
    {
      cont(std::move(*result));
    }
  }

private:
  struct SharedState
  {
    std::atomic<State> state{State::kPending};
    std::optional<T> value;
    std::optional<ErrorType> error;
    std::vector<std::function<void(ResultType)>> continuations;
    std::mutex mutex;
    std::condition_variable cv;
    std::atomic<bool> cancellation_requested{false};
  };

  static void resolve_shared(const std::shared_ptr<SharedState>& state, T value)
  {
    std::vector<std::function<void(ResultType)>> continuations;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->state != State::kPending)
      {
        return;
      }
      state->value = std::move(value);
      state->state = State::kReady;
      continuations.swap(state->continuations);
    }
    state->cv.notify_all();
    for (auto& cont : continuations)
    {
      cont(ResultType::Ok(*state->value));
    }
  }

  static void reject_shared(const std::shared_ptr<SharedState>& state, ErrorType error)
  {
    std::vector<std::function<void(ResultType)>> continuations;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->state != State::kPending)
      {
        return;
      }
      state->error = std::move(error);
      state->state = State::kErrored;
      continuations.swap(state->continuations);
    }
    state->cv.notify_all();
    for (auto& cont : continuations)
    {
      cont(ResultType::Err(*state->error));
    }
  }

  ResultType result_locked() const
  {
    if (state_->state == State::kReady && state_->value.has_value())
    {
      return ResultType::Ok(*state_->value);
    }
    if (state_->error.has_value())
    {
      return ResultType::Err(*state_->error);
    }
    return ResultType::Err(std::string("Future not completed"));
  }

  std::shared_ptr<SharedState> state_;

  template <typename U>
  friend class Future;
  friend class Executor;
};

// Specialization for void
template <>
class Future<void>
{
public:
  using ValueType = void;
  using ErrorType = std::variant<std::string, TimeoutError, CancellationError>;
  using ResultType = Result<void, ErrorType>;

  Future() : state_(std::make_shared<SharedState>()) {}
  explicit Future(ErrorType error) : state_(std::make_shared<SharedState>())
  {
    reject(std::move(error));
  }

  Future(Future&& other) noexcept : state_(std::move(other.state_)) {}
  Future& operator=(Future&& other) noexcept
  {
    if (this != &other)
    {
      state_ = std::move(other.state_);
    }
    return *this;
  }
  Future(const Future&) = delete;
  Future& operator=(const Future&) = delete;

  bool is_pending() const
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return !state_->ready;
  }

  bool is_ready() const
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->ready && !state_->error.has_value();
  }

  ResultType wait()
  {
    std::unique_lock<std::mutex> lock(state_->mutex);
    state_->cv.wait(lock, [this]() { return state_->ready; });
    if (state_->error.has_value())
    {
      return ResultType::Err(*state_->error);
    }
    return ResultType::Ok();
  }

  template <typename F>
  auto then(F&& func) -> Future<std::invoke_result_t<F>>
  {
    using NextT = std::invoke_result_t<F>;
    auto next = Future<NextT>();
    add_continuation([next_state = next.state_, fn = std::forward<F>(func)](ResultType result) mutable {
      if (result.is_ok())
      {
        try
        {
          if constexpr (std::is_same_v<NextT, void>)
          {
            fn();
            Future<void>::resolve_shared(next_state);
          }
          else
          {
            Future<NextT>::resolve_shared(next_state, fn());
          }
        }
        catch (const std::exception& ex)
        {
          Future<NextT>::reject_shared(next_state, std::string(ex.what()));
        }
        catch (...)
        {
          Future<NextT>::reject_shared(next_state, std::string("Unknown error"));
        }
      }
      else
      {
        Future<NextT>::reject_shared(next_state, std::move(result).unwrap_err());
      }
    });
    return next;
  }

  void resolve()
  {
    resolve_shared(state_);
  }

  void reject(ErrorType error)
  {
    reject_shared(state_, std::move(error));
  }

private:
  struct SharedState
  {
    bool ready{false};
    std::optional<ErrorType> error;
    std::vector<std::function<void(ResultType)>> continuations;
    std::mutex mutex;
    std::condition_variable cv;
  };

  static void resolve_shared(const std::shared_ptr<SharedState>& state)
  {
    std::vector<std::function<void(ResultType)>> continuations;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->ready)
      {
        return;
      }
      state->ready = true;
      continuations.swap(state->continuations);
    }
    state->cv.notify_all();
    for (auto& cont : continuations)
    {
      cont(ResultType::Ok());
    }
  }

  static void reject_shared(const std::shared_ptr<SharedState>& state, ErrorType error)
  {
    std::vector<std::function<void(ResultType)>> continuations;
    {
      std::lock_guard<std::mutex> lock(state->mutex);
      if (state->ready)
      {
        return;
      }
      state->ready = true;
      state->error = std::move(error);
      continuations.swap(state->continuations);
    }
    state->cv.notify_all();
    for (auto& cont : continuations)
    {
      cont(ResultType::Err(*state->error));
    }
  }

  void add_continuation(std::function<void(ResultType)> cont)
  {
    bool run_now = false;
    std::optional<ResultType> result;
    {
      std::lock_guard<std::mutex> lock(state_->mutex);
      if (!state_->ready)
      {
        state_->continuations.push_back(std::move(cont));
        return;
      }
      run_now = true;
      if (state_->error.has_value())
      {
        result = ResultType::Err(*state_->error);
      }
      else
      {
        result = ResultType::Ok();
      }
    }
    if (run_now)
    {
      cont(std::move(*result));
    }
  }

  std::shared_ptr<SharedState> state_;

  template <typename U>
  friend class Future;
  friend class Executor;
};

template <typename T>
Future<T> make_ready_future(T value)
{
  return Future<T>(std::move(value));
}

inline Future<void> make_ready_future()
{
  Future<void> future;
  future.resolve();
  return future;
}

template <typename E>
Future<void> make_error_future(E error)
{
  return Future<void>(typename Future<void>::ErrorType{std::move(error)});
}

// Out-of-line definition of Future<T>::flat_map (needs Future<void> to be complete)
template <typename T>
template <typename F>
auto Future<T>::flat_map(F&& func) -> std::invoke_result_t<F, T>
{
  using NextFuture = std::invoke_result_t<F, T>;
  auto next = NextFuture();
  add_continuation([next_state = next.state_, fn = std::forward<F>(func)](ResultType result) mutable {
    if (result.is_ok())
    {
      try
      {
        NextFuture produced = fn(std::move(result).unwrap());
        produced.add_continuation([next_state](typename NextFuture::ResultType inner_result) {
          if (inner_result.is_ok())
          {
            if constexpr (std::is_same_v<typename NextFuture::ValueType, void>)
            {
              inner_result.unwrap();
              Future<void>::resolve_shared(next_state);
            }
            else
            {
              Future<typename NextFuture::ValueType>::resolve_shared(next_state, std::move(inner_result).unwrap());
            }
          }
          else
          {
            Future<typename NextFuture::ValueType>::reject_shared(next_state, std::move(inner_result).unwrap_err());
          }
        });
      }
      catch (const std::exception& ex)
      {
        Future<typename NextFuture::ValueType>::reject_shared(next_state, std::string(ex.what()));
      }
      catch (...)
      {
        Future<typename NextFuture::ValueType>::reject_shared(next_state, std::string("Unknown error"));
      }
    }
    else
    {
      Future<typename NextFuture::ValueType>::reject_shared(next_state, std::move(result).unwrap_err());
    }
  });
  return next;
}

}  // namespace neamc::runtime
