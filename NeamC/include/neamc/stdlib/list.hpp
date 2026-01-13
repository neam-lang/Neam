//
// Neam Standard Library - List
//

#pragma once

#include <algorithm>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

#include "neamc/stdlib/option.hpp"

namespace neamc::stdlib
{
template <typename T>
class List
{
public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using pointer = T*;
  using const_pointer = const T*;
  using iterator = typename std::vector<T>::iterator;
  using const_iterator = typename std::vector<T>::const_iterator;
  using reverse_iterator = typename std::vector<T>::reverse_iterator;
  using const_reverse_iterator = typename std::vector<T>::const_reverse_iterator;

  List() = default;
  explicit List(size_type count) : data_(count) {}
  List(size_type count, const T& value) : data_(count, value) {}
  List(std::initializer_list<T> init) : data_(init) {}

  template <typename InputIt>
  List(InputIt first, InputIt last) : data_(first, last) {}

  List(const List& other) = default;
  List(List&& other) noexcept = default;
  List& operator=(const List& other) = default;
  List& operator=(List&& other) noexcept = default;
  ~List() = default;

  Option<reference> get(size_type index)
  {
    if (index >= data_.size())
    {
      return None;
    }
    return Option<reference>(data_[index]);
  }

  Option<const_reference> get(size_type index) const
  {
    if (index >= data_.size())
    {
      return None;
    }
    return Option<const_reference>(data_[index]);
  }

  reference operator[](size_type index) { return data_.at(index); }
  const_reference operator[](size_type index) const { return data_.at(index); }

  Option<reference> first()
  {
    if (data_.empty())
    {
      return None;
    }
    return Option<reference>(data_.front());
  }

  Option<const_reference> first() const
  {
    if (data_.empty())
    {
      return None;
    }
    return Option<const_reference>(data_.front());
  }

  Option<reference> last()
  {
    if (data_.empty())
    {
      return None;
    }
    return Option<reference>(data_.back());
  }

  Option<const_reference> last() const
  {
    if (data_.empty())
    {
      return None;
    }
    return Option<const_reference>(data_.back());
  }

  bool is_empty() const noexcept { return data_.empty(); }
  size_type len() const noexcept { return data_.size(); }
  size_type capacity() const noexcept { return data_.capacity(); }
  void reserve(size_type new_cap) { data_.reserve(new_cap); }
  void shrink_to_fit() { data_.shrink_to_fit(); }

  void clear() noexcept { data_.clear(); }

  void push(T value) { data_.push_back(std::move(value)); }
  void push(T&& value) { data_.push_back(std::move(value)); }

  Option<T> pop()
  {
    if (data_.empty())
    {
      return None;
    }
    T value = std::move(data_.back());
    data_.pop_back();
    return Option<T>(std::move(value));
  }

  void insert(size_type index, T value)
  {
    if (index > data_.size())
    {
      throw std::out_of_range("List insert index out of range");
    }
    data_.insert(data_.begin() + static_cast<difference_type>(index), std::move(value));
  }

  Option<T> remove(size_type index)
  {
    if (index >= data_.size())
    {
      return None;
    }
    T value = std::move(data_[index]);
    data_.erase(data_.begin() + static_cast<difference_type>(index));
    return Option<T>(std::move(value));
  }

  void swap(size_type i, size_type j)
  {
    if (i >= data_.size() || j >= data_.size())
    {
      throw std::out_of_range("List swap index out of range");
    }
    std::swap(data_[i], data_[j]);
  }

  void reverse() { std::reverse(data_.begin(), data_.end()); }

  iterator begin() noexcept { return data_.begin(); }
  const_iterator begin() const noexcept { return data_.begin(); }
  const_iterator cbegin() const noexcept { return data_.cbegin(); }
  iterator end() noexcept { return data_.end(); }
  const_iterator end() const noexcept { return data_.end(); }
  const_iterator cend() const noexcept { return data_.cend(); }
  reverse_iterator rbegin() noexcept { return data_.rbegin(); }
  reverse_iterator rend() noexcept { return data_.rend(); }

  template <typename F>
  List<std::invoke_result_t<F, T>> map(F&& func) const
  {
    using U = std::invoke_result_t<F, T>;
    List<U> output;
    output.reserve(data_.size());
    for (const auto& value : data_)
    {
      output.push(func(value));
    }
    return output;
  }

  template <typename F>
  List<T> filter(F&& predicate) const
  {
    List<T> output;
    for (const auto& value : data_)
    {
      if (predicate(value))
      {
        output.push(value);
      }
    }
    return output;
  }

  template <typename F, typename U>
  U fold(U init, F&& func) const
  {
    U acc = init;
    for (const auto& value : data_)
    {
      acc = func(std::move(acc), value);
    }
    return acc;
  }

  template <typename F>
  Option<reference> find(F&& predicate)
  {
    for (auto& value : data_)
    {
      if (predicate(value))
      {
        return Option<reference>(value);
      }
    }
    return None;
  }

  template <typename F>
  Option<size_type> position(F&& predicate) const
  {
    for (size_type i = 0; i < data_.size(); ++i)
    {
      if (predicate(data_[i]))
      {
        return Option<size_type>(i);
      }
    }
    return None;
  }

  template <typename F>
  bool any(F&& predicate) const
  {
    for (const auto& value : data_)
    {
      if (predicate(value))
      {
        return true;
      }
    }
    return false;
  }

  template <typename F>
  bool all(F&& predicate) const
  {
    for (const auto& value : data_)
    {
      if (!predicate(value))
      {
        return false;
      }
    }
    return true;
  }

  template <typename F>
  void for_each(F&& func)
  {
    for (auto& value : data_)
    {
      func(value);
    }
  }

  List<T> take(size_type n) const
  {
    if (n >= data_.size())
    {
      return *this;
    }
    return List<T>(data_.begin(), data_.begin() + static_cast<difference_type>(n));
  }

  List<T> skip(size_type n) const
  {
    if (n >= data_.size())
    {
      return List<T>();
    }
    return List<T>(data_.begin() + static_cast<difference_type>(n), data_.end());
  }

  List<T> slice(size_type start, size_type end) const
  {
    if (start > end || end > data_.size())
    {
      throw std::out_of_range("List slice out of range");
    }
    return List<T>(data_.begin() + static_cast<difference_type>(start),
                   data_.begin() + static_cast<difference_type>(end));
  }

  void sort() { std::sort(data_.begin(), data_.end()); }

  template <typename Compare>
  void sort_by(Compare comp)
  {
    std::sort(data_.begin(), data_.end(), comp);
  }

  template <typename F>
  void sort_by_key(F&& key_func)
  {
    std::sort(data_.begin(), data_.end(), [&](const T& a, const T& b) {
      return key_func(a) < key_func(b);
    });
  }

  Option<size_type> binary_search(const T& value) const
  {
    auto it = std::lower_bound(data_.begin(), data_.end(), value);
    if (it != data_.end() && *it == value)
    {
      return Option<size_type>(static_cast<size_type>(std::distance(data_.begin(), it)));
    }
    return None;
  }

  bool contains(const T& value) const
  {
    return std::find(data_.begin(), data_.end(), value) != data_.end();
  }

  std::vector<T> to_vec() && { return std::move(data_); }
  const std::vector<T>& as_vec() const { return data_; }

  bool operator==(const List& other) const { return data_ == other.data_; }
  bool operator!=(const List& other) const { return data_ != other.data_; }

private:
  std::vector<T> data_{};
};

template <typename T>
List(std::initializer_list<T>) -> List<T>;

template <typename T>
List<T> list_of(std::initializer_list<T> init)
{
  return List<T>(init);
}

template <typename T, typename... Args>
List<T> list_from(Args&&... args)
{
  List<T> list;
  list.reserve(sizeof...(Args));
  (list.push(std::forward<Args>(args)), ...);
  return list;
}
}  // namespace neamc::stdlib
