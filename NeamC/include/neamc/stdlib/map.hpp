//
// Neam Standard Library - Map
//

#pragma once

#include <functional>
#include <initializer_list>
#include <unordered_map>
#include <utility>
#include <vector>

#include "neamc/stdlib/list.hpp"
#include "neamc/stdlib/option.hpp"

namespace neamc::stdlib
{
template <typename K, typename V, typename Hash = std::hash<K>, typename KeyEqual = std::equal_to<K>>
class Map
{
public:
  using key_type = K;
  using mapped_type = V;
  using value_type = std::pair<const K, V>;
  using size_type = std::size_t;
  using hasher = Hash;
  using key_equal = KeyEqual;
  using iterator = typename std::unordered_map<K, V, Hash, KeyEqual>::iterator;
  using const_iterator = typename std::unordered_map<K, V, Hash, KeyEqual>::const_iterator;

  Map() = default;
  Map(std::initializer_list<std::pair<K, V>> init)
  {
    for (const auto& item : init)
    {
      data_.insert(item);
    }
  }

  template <typename InputIt>
  Map(InputIt first, InputIt last) : data_(first, last)
  {
  }

  Option<V&> get(const K& key)
  {
    auto it = data_.find(key);
    if (it == data_.end())
    {
      return None;
    }
    return Option<V&>(it->second);
  }

  Option<const V&> get(const K& key) const
  {
    auto it = data_.find(key);
    if (it == data_.end())
    {
      return None;
    }
    return Option<const V&>(it->second);
  }

  V& operator[](const K& key) { return data_[key]; }
  V& operator[](K&& key) { return data_[std::move(key)]; }

  bool contains(const K& key) const { return data_.find(key) != data_.end(); }
  size_type count(const K& key) const { return data_.count(key); }

  bool is_empty() const noexcept { return data_.empty(); }
  size_type len() const noexcept { return data_.size(); }

  void insert(K key, V value)
  {
    data_[std::move(key)] = std::move(value);
  }

  void insert(std::pair<K, V> pair)
  {
    data_[std::move(pair.first)] = std::move(pair.second);
  }

  template <typename... Args>
  std::pair<iterator, bool> emplace(Args&&... args)
  {
    return data_.emplace(std::forward<Args>(args)...);
  }

  Option<V> remove(const K& key)
  {
    auto it = data_.find(key);
    if (it == data_.end())
    {
      return None;
    }
    V value = std::move(it->second);
    data_.erase(it);
    return Option<V>(std::move(value));
  }

  void clear() noexcept { data_.clear(); }

  class Entry
  {
  public:
    Entry(Map& map, const K& key) : map_(map), key_(key)
    {
      it_ = map_.data_.find(key_);
      exists_ = it_ != map_.data_.end();
    }

    V& or_insert(V default_value)
    {
      if (!exists_)
      {
        auto result = map_.data_.emplace(key_, std::move(default_value));
        it_ = result.first;
        exists_ = true;
      }
      return it_->second;
    }

    V& or_insert_with(std::function<V()> f)
    {
      if (!exists_)
      {
        auto result = map_.data_.emplace(key_, f());
        it_ = result.first;
        exists_ = true;
      }
      return it_->second;
    }

    template <typename F>
    Entry& and_modify(F&& f)
    {
      if (exists_)
      {
        f(it_->second);
      }
      return *this;
    }

  private:
    Map& map_;
    K key_;
    iterator it_{};
    bool exists_{false};
  };

  Entry entry(const K& key) { return Entry(*this, key); }

  iterator begin() noexcept { return data_.begin(); }
  const_iterator begin() const noexcept { return data_.begin(); }
  const_iterator cbegin() const noexcept { return data_.cbegin(); }
  iterator end() noexcept { return data_.end(); }
  const_iterator end() const noexcept { return data_.end(); }
  const_iterator cend() const noexcept { return data_.cend(); }

  List<K> keys() const
  {
    List<K> list;
    list.reserve(data_.size());
    for (const auto& [key, value] : data_)
    {
      list.push(key);
    }
    return list;
  }

  List<V> values() const
  {
    List<V> list;
    list.reserve(data_.size());
    for (const auto& [key, value] : data_)
    {
      list.push(value);
    }
    return list;
  }

  List<std::pair<K, V>> entries() const
  {
    List<std::pair<K, V>> list;
    list.reserve(data_.size());
    for (const auto& [key, value] : data_)
    {
      list.push({key, value});
    }
    return list;
  }

  template <typename F>
  Map<K, std::invoke_result_t<F, V>> map_values(F&& func) const
  {
    using OutV = std::invoke_result_t<F, V>;
    Map<K, OutV> out;
    for (const auto& [key, value] : data_)
    {
      out.insert(key, func(value));
    }
    return out;
  }

  template <typename F>
  Map<K, V> filter(F&& predicate) const
  {
    Map<K, V> out;
    for (const auto& [key, value] : data_)
    {
      if (predicate(key, value))
      {
        out.insert(key, value);
      }
    }
    return out;
  }

  template <typename F>
  void for_each(F&& func)
  {
    for (auto& [key, value] : data_)
    {
      func(key, value);
    }
  }

  void extend(const Map& other)
  {
    for (const auto& [key, value] : other.data_)
    {
      data_[key] = value;
    }
  }

  void extend(Map&& other)
  {
    for (auto& [key, value] : other.data_)
    {
      data_[std::move(key)] = std::move(value);
    }
    other.clear();
  }

  Map<K, V> merged_with(const Map& other) const
  {
    Map<K, V> out = *this;
    out.extend(other);
    return out;
  }

private:
  std::unordered_map<K, V, Hash, KeyEqual> data_{};
};

template <typename K, typename V>
Map(std::initializer_list<std::pair<K, V>>) -> Map<K, V>;
}  // namespace neamc::stdlib
