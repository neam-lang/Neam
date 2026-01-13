//
// Neam Standard Library - Set
//

#pragma once

#include <functional>
#include <initializer_list>
#include <unordered_set>

#include "neamc/stdlib/list.hpp"
#include "neamc/stdlib/option.hpp"

namespace neamc::stdlib
{
template <typename T, typename Hash = std::hash<T>, typename KeyEqual = std::equal_to<T>>
class Set
{
public:
  using value_type = T;
  using size_type = std::size_t;
  using hasher = Hash;
  using key_equal = KeyEqual;
  using iterator = typename std::unordered_set<T, Hash, KeyEqual>::iterator;
  using const_iterator = typename std::unordered_set<T, Hash, KeyEqual>::const_iterator;

  Set() = default;
  Set(std::initializer_list<T> init) : data_(init) {}

  template <typename InputIt>
  Set(InputIt first, InputIt last) : data_(first, last)
  {
  }

  bool is_empty() const noexcept { return data_.empty(); }
  size_type len() const noexcept { return data_.size(); }

  bool insert(T value)
  {
    return data_.insert(std::move(value)).second;
  }

  bool remove(const T& value)
  {
    return data_.erase(value) > 0;
  }

  void clear() noexcept { data_.clear(); }

  bool contains(const T& value) const
  {
    return data_.find(value) != data_.end();
  }

  Option<const T&> get(const T& value) const
  {
    auto it = data_.find(value);
    if (it == data_.end())
    {
      return None;
    }
    return Option<const T&>(*it);
  }

  iterator begin() noexcept { return data_.begin(); }
  const_iterator begin() const noexcept { return data_.begin(); }
  iterator end() noexcept { return data_.end(); }
  const_iterator end() const noexcept { return data_.end(); }

  Set<T> union_with(const Set& other) const
  {
    Set<T> out = *this;
    out.data_.insert(other.data_.begin(), other.data_.end());
    return out;
  }

  Set<T> intersection(const Set& other) const
  {
    Set<T> out;
    for (const auto& value : data_)
    {
      if (other.contains(value))
      {
        out.insert(value);
      }
    }
    return out;
  }

  Set<T> difference(const Set& other) const
  {
    Set<T> out;
    for (const auto& value : data_)
    {
      if (!other.contains(value))
      {
        out.insert(value);
      }
    }
    return out;
  }

  Set<T> symmetric_difference(const Set& other) const
  {
    Set<T> out;
    for (const auto& value : data_)
    {
      if (!other.contains(value))
      {
        out.insert(value);
      }
    }
    for (const auto& value : other.data_)
    {
      if (!contains(value))
      {
        out.insert(value);
      }
    }
    return out;
  }

  bool is_subset(const Set& other) const
  {
    for (const auto& value : data_)
    {
      if (!other.contains(value))
      {
        return false;
      }
    }
    return true;
  }

  bool is_superset(const Set& other) const
  {
    return other.is_subset(*this);
  }

  bool is_disjoint(const Set& other) const
  {
    for (const auto& value : data_)
    {
      if (other.contains(value))
      {
        return false;
      }
    }
    return true;
  }

  template <typename F>
  Set<std::invoke_result_t<F, T>> map(F&& func) const
  {
    using OutT = std::invoke_result_t<F, T>;
    Set<OutT> out;
    for (const auto& value : data_)
    {
      out.insert(func(value));
    }
    return out;
  }

  template <typename F>
  Set<T> filter(F&& predicate) const
  {
    Set<T> out;
    for (const auto& value : data_)
    {
      if (predicate(value))
      {
        out.insert(value);
      }
    }
    return out;
  }

  template <typename F>
  void for_each(F&& func)
  {
    for (auto& value : data_)
    {
      func(value);
    }
  }

  List<T> to_list() const
  {
    List<T> list;
    list.reserve(data_.size());
    for (const auto& value : data_)
    {
      list.push(value);
    }
    return list;
  }

private:
  std::unordered_set<T, Hash, KeyEqual> data_{};
};

template <typename T>
Set(std::initializer_list<T>) -> Set<T>;
}  // namespace neamc::stdlib
