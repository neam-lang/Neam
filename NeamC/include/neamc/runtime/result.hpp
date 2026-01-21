//
// Neam Runtime - Result
//

#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>

namespace neamc::runtime
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

  template <typename F>
  auto map(F&& func) const& -> Result<std::invoke_result_t<F, const T&>, E>
  {
    using U = std::invoke_result_t<F, const T&>;
    if (is_err())
    {
      return Result<U, E>::Err(std::get<1>(data_));
    }
    return Result<U, E>::Ok(func(std::get<0>(data_)));
  }

  template <typename F>
  auto map_err(F&& func) const& -> Result<T, std::invoke_result_t<F, const E&>>
  {
    using F2 = std::invoke_result_t<F, const E&>;
    if (is_ok())
    {
      return Result<T, F2>::Ok(std::get<0>(data_));
    }
    return Result<T, F2>::Err(func(std::get<1>(data_)));
  }

private:
  template <size_t I, typename U>
  Result(std::in_place_index_t<I> tag, U&& value) : data_(tag, std::forward<U>(value)) {}

  std::variant<T, E> data_;
};

// Specialization for void
template <typename E>
class Result<void, E>
{
public:
  static Result Ok()
  {
    Result out;
    out.is_ok_ = true;
    return out;
  }

  static Result Err(E error)
  {
    Result out;
    out.is_ok_ = false;
    out.error_ = std::move(error);
    return out;
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

  const E& unwrap_err() const&
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
  std::optional<E> error_{};
};
}  // namespace neamc::runtime
