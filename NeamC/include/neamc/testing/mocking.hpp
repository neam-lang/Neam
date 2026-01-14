//
// NeamC - Mocking utilities
//

#pragma once

#include <any>
#include <chrono>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace neamc::testing
{
struct MockCall
{
  std::string method_name;
  std::vector<std::any> arguments;
  std::chrono::steady_clock::time_point timestamp;
};

template <typename R, typename... Args>
class MockExpectation
{
public:
  MockExpectation& times(size_t n)
  {
    expected_calls_ = n;
    min_calls_ = n;
    max_calls_ = n;
    return *this;
  }

  MockExpectation& at_least(size_t n)
  {
    min_calls_ = n;
    return *this;
  }

  MockExpectation& at_most(size_t n)
  {
    max_calls_ = n;
    return *this;
  }

  MockExpectation& returns(R value)
  {
    return_value_ = std::move(value);
    return *this;
  }

  MockExpectation& returns_sequentially(std::vector<R> values)
  {
    return_sequence_ = std::move(values);
    return_sequence_index_ = 0;
    return *this;
  }

  MockExpectation& throws(std::exception_ptr ex)
  {
    exception_ = ex;
    return *this;
  }

  template <typename F>
  MockExpectation& invokes(F&& func)
  {
    custom_impl_ = std::forward<F>(func);
    return *this;
  }

  template <typename F>
  MockExpectation& with_args(F&& matcher)
  {
    arg_matcher_ = std::forward<F>(matcher);
    return *this;
  }

  bool matches(Args... args) const
  {
    if (!arg_matcher_)
    {
      return true;
    }
    return arg_matcher_(std::forward<Args>(args)...);
  }

  R invoke(Args... args)
  {
    if (exception_)
    {
      std::rethrow_exception(*exception_);
    }
    if (custom_impl_)
    {
      return custom_impl_(std::forward<Args>(args)...);
    }
    if (!return_sequence_.empty())
    {
      auto index = return_sequence_index_++ % return_sequence_.size();
      return return_sequence_[index];
    }
    if (return_value_.has_value())
    {
      return *return_value_;
    }
    return R{};
  }

  size_t expected_calls() const { return expected_calls_; }
  size_t min_calls() const { return min_calls_; }
  size_t max_calls() const { return max_calls_; }

private:
  size_t expected_calls_{1};
  size_t min_calls_{0};
  size_t max_calls_{std::numeric_limits<size_t>::max()};
  std::optional<R> return_value_;
  std::vector<R> return_sequence_;
  size_t return_sequence_index_{0};
  std::optional<std::exception_ptr> exception_;
  std::function<R(Args...)> custom_impl_;
  std::function<bool(Args...)> arg_matcher_;
};

namespace matchers
{

template <typename T>
auto any() -> std::function<bool(const T&)>
{
  return [](const T&) { return true; };
}

template <typename T>
auto eq(T expected) -> std::function<bool(const T&)>
{
  return [expected = std::move(expected)](const T& value) { return value == expected; };
}

template <typename T>
auto gt(T value) -> std::function<bool(const T&)>
{
  return [value = std::move(value)](const T& input) { return input > value; };
}

template <typename T>
auto lt(T value) -> std::function<bool(const T&)>
{
  return [value = std::move(value)](const T& input) { return input < value; };
}

inline auto contains(std::string substr) -> std::function<bool(const std::string&)>
{
  return [substr = std::move(substr)](const std::string& value) {
    return value.find(substr) != std::string::npos;
  };
}

inline auto matches(std::string regex) -> std::function<bool(const std::string&)>
{
  return [pattern = std::move(regex)](const std::string& value) {
    return value.find(pattern) != std::string::npos;
  };
}

}  // namespace matchers
}  // namespace neamc::testing
