//
// Neam Standard Library - Result
//

#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <variant>

#include "neamc/stdlib/option.hpp"

namespace neamc::stdlib
{
template <typename T, typename E>
class Result
{
public:
  using ok_type = T;
  using err_type = E;

  static Result Ok(T value)
  {
    return Result(std::in_place_index<0>, std::move(value));
  }

  static Result Err(E error)
  {
    return Result(std::in_place_index<1>, std::move(error));
  }

  bool is_ok() const noexcept { return data_.index() == 0; }
  bool is_err() const noexcept { return data_.index() == 1; }
  explicit operator bool() const noexcept { return is_ok(); }

  T& unwrap() &
  {
    if (is_err())
    {
      throw std::runtime_error("Called unwrap on Err");
    }
    return std::get<0>(data_);
  }

  const T& unwrap() const&
  {
    if (is_err())
    {
      throw std::runtime_error("Called unwrap on Err");
    }
    return std::get<0>(data_);
  }

  T unwrap() &&
  {
    if (is_err())
    {
      throw std::runtime_error("Called unwrap on Err");
    }
    return std::move(std::get<0>(data_));
  }

  E& unwrap_err() &
  {
    if (is_ok())
    {
      throw std::runtime_error("Called unwrap_err on Ok");
    }
    return std::get<1>(data_);
  }

  const E& unwrap_err() const&
  {
    if (is_ok())
    {
      throw std::runtime_error("Called unwrap_err on Ok");
    }
    return std::get<1>(data_);
  }

  T unwrap_or(T default_value) const&
  {
    return is_ok() ? std::get<0>(data_) : default_value;
  }

  template <typename F>
  T unwrap_or_else(F&& f) const&
  {
    return is_ok() ? std::get<0>(data_) : f(std::get<1>(data_));
  }

  T& expect(const char* msg) &
  {
    if (is_err())
    {
      throw std::runtime_error(msg);
    }
    return std::get<0>(data_);
  }

  template <typename F>
  auto map(F&& f) const& -> Result<std::invoke_result_t<F, const T&>, E>
  {
    using U = std::invoke_result_t<F, const T&>;
    if (is_err())
    {
      return Result<U, E>::Err(std::get<1>(data_));
    }
    return Result<U, E>::Ok(f(std::get<0>(data_)));
  }

  template <typename F>
  auto map_err(F&& f) const& -> Result<T, std::invoke_result_t<F, const E&>>
  {
    using F2 = std::invoke_result_t<F, const E&>;
    if (is_ok())
    {
      return Result<T, F2>::Ok(std::get<0>(data_));
    }
    return Result<T, F2>::Err(f(std::get<1>(data_)));
  }

  template <typename F>
  auto and_then(F&& f) const& -> std::invoke_result_t<F, const T&>
  {
    if (is_err())
    {
      return std::invoke_result_t<F, const T&>::Err(std::get<1>(data_));
    }
    return f(std::get<0>(data_));
  }

  template <typename F>
  auto or_else(F&& f) const& -> std::invoke_result_t<F, const E&>
  {
    if (is_ok())
    {
      return std::invoke_result_t<F, const E&>::Ok(std::get<0>(data_));
    }
    return f(std::get<1>(data_));
  }

  Option<T> ok() const&
  {
    if (is_err())
    {
      return None;
    }
    return Some(std::get<0>(data_));
  }

  Option<E> err() const&
  {
    if (is_ok())
    {
      return None;
    }
    return Some(std::get<1>(data_));
  }

  bool operator==(const Result& other) const
  {
    return data_ == other.data_;
  }

private:
  template <size_t I, typename U>
  Result(std::in_place_index_t<I> tag, U&& value) : data_(tag, std::forward<U>(value)) {}

  std::variant<T, E> data_;
};

template <typename E>
class Result<void, E>
{
public:
  static Result Ok()
  {
    Result r;
    r.is_ok_ = true;
    return r;
  }

  static Result Err(E error)
  {
    Result r;
    r.is_ok_ = false;
    r.error_ = std::move(error);
    return r;
  }

  bool is_ok() const noexcept { return is_ok_; }
  bool is_err() const noexcept { return !is_ok_; }

  void unwrap() const
  {
    if (is_err())
    {
      throw std::runtime_error("Called unwrap on Err");
    }
  }

  E& unwrap_err() &
  {
    if (is_ok())
    {
      throw std::runtime_error("Called unwrap_err on Ok");
    }
    return *error_;
  }

private:
  Result() = default;
  bool is_ok_{false};
  std::optional<E> error_;
};
}  // namespace neamc::stdlib
