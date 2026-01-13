//
// Neam Standard Library - Option
//

#pragma once

#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

namespace neamc::stdlib
{
struct NoneType
{
  constexpr explicit NoneType(int) {}
};
inline constexpr NoneType None{0};

template <typename T>
class Option
{
public:
  using value_type = T;

  Option() noexcept : data_(std::nullopt) {}
  Option(NoneType) noexcept : data_(std::nullopt) {}
  Option(T value) : data_(std::move(value)) {}
  Option(const Option&) = default;
  Option(Option&&) noexcept = default;
  Option& operator=(const Option&) = default;
  Option& operator=(Option&&) noexcept = default;

  static Option Some(T value) { return Option(std::move(value)); }

  bool is_some() const noexcept { return data_.has_value(); }
  bool is_none() const noexcept { return !data_.has_value(); }
  explicit operator bool() const noexcept { return is_some(); }

  T& unwrap() &
  {
    if (is_none())
    {
      throw std::runtime_error("Called unwrap on None");
    }
    return *data_;
  }

  const T& unwrap() const&
  {
    if (is_none())
    {
      throw std::runtime_error("Called unwrap on None");
    }
    return *data_;
  }

  T unwrap() &&
  {
    if (is_none())
    {
      throw std::runtime_error("Called unwrap on None");
    }
    return std::move(*data_);
  }

  T unwrap_or(T default_value) const&
  {
    return is_some() ? *data_ : default_value;
  }

  T unwrap_or(T default_value) &&
  {
    return is_some() ? std::move(*data_) : default_value;
  }

  template <typename F>
  T unwrap_or_else(F&& f) const&
  {
    return is_some() ? *data_ : f();
  }

  T& expect(const char* msg) &
  {
    if (is_none())
    {
      throw std::runtime_error(msg);
    }
    return *data_;
  }

  template <typename F>
  auto map(F&& f) const& -> Option<std::invoke_result_t<F, const T&>>
  {
    using U = std::invoke_result_t<F, const T&>;
    if (is_none())
    {
      return Option<U>();
    }
    return Option<U>(f(*data_));
  }

  template <typename F>
  auto map(F&& f) && -> Option<std::invoke_result_t<F, T>>
  {
    using U = std::invoke_result_t<F, T>;
    if (is_none())
    {
      return Option<U>();
    }
    return Option<U>(f(std::move(*data_)));
  }

  template <typename F>
  auto and_then(F&& f) const& -> std::invoke_result_t<F, const T&>
  {
    if (is_none())
    {
      return std::invoke_result_t<F, const T&>();
    }
    return f(*data_);
  }

  template <typename F>
  auto and_then(F&& f) && -> std::invoke_result_t<F, T>
  {
    if (is_none())
    {
      return std::invoke_result_t<F, T>();
    }
    return f(std::move(*data_));
  }

  Option<T> or_else(Option<T> other) const&
  {
    return is_some() ? *this : other;
  }

  template <typename F>
  Option<T> or_else_with(F&& f) const&
  {
    return is_some() ? *this : f();
  }

  template <typename F>
  Option<T> filter(F&& predicate) const&
  {
    if (is_none() || !predicate(*data_))
    {
      return Option();
    }
    return *this;
  }

  bool operator==(const Option& other) const
  {
    if (is_none() && other.is_none())
    {
      return true;
    }
    if (is_none() || other.is_none())
    {
      return false;
    }
    return *data_ == *other.data_;
  }

  bool operator!=(const Option& other) const
  {
    return !(*this == other);
  }

  const T* begin() const { return is_some() ? &*data_ : nullptr; }
  const T* end() const { return is_some() ? &*data_ + 1 : nullptr; }
  T* begin() { return is_some() ? &*data_ : nullptr; }
  T* end() { return is_some() ? &*data_ + 1 : nullptr; }

private:
  std::optional<T> data_;
};

template <typename T>
Option<std::decay_t<T>> Some(T&& value)
{
  return Option<std::decay_t<T>>(std::forward<T>(value));
}
}  // namespace neamc::stdlib
