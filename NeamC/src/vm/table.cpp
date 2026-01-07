//
// Neam Virtual Machine - Hash table implementation
//

#include "neamc/vm/table.hpp"

#include <cstring>

#include "neamc/vm/memory.hpp"

namespace neamc::vm
{
namespace
{
constexpr double kMaxLoad = 0.75;
}  // namespace

void Table::clear()
{
  entries_.clear();
  count_ = 0;
}

namespace
{
bool strings_equal(const ObjString* a, const ObjString* b)
{
  return a->length == b->length && a->hash == b->hash &&
         std::memcmp(a->chars, b->chars, a->length) == 0;
}
}  // namespace

TableEntry* Table::find_entry(ObjString* key) const
{
  if (entries_.empty())
  {
    return nullptr;
  }
  std::size_t index = key->hash % entries_.size();
  TableEntry* tombstone = nullptr;

  while (true)
  {
    TableEntry* entry = const_cast<TableEntry*>(&entries_[index]);
    if (entry->key == nullptr)
    {
      if (entry->value.is_nil())
      {
        return tombstone != nullptr ? tombstone : entry;
      }
      else if (!tombstone)
      {
        tombstone = entry;
      }
    }
    else if (strings_equal(entry->key, key))
    {
      return entry;
    }
    index = (index + 1) % entries_.size();
  }
}

bool Table::get(ObjString* key, Value* out_value) const
{
  if (count_ == 0)
  {
    return false;
  }
  auto* entry = find_entry(key);
  if (!entry || !entry->key)
  {
    return false;
  }
  *out_value = entry->value;
  return true;
}

void Table::adjust_capacity(std::size_t capacity)
{
  std::vector<TableEntry> new_entries(capacity);
  for (auto& entry : new_entries)
  {
    entry.key = nullptr;
    entry.value = Value::Nil();
  }

  count_ = 0;
  for (const auto& entry : entries_)
  {
    if (entry.key == nullptr)
    {
      continue;
    }
    auto* dest = [&]() -> TableEntry* {
      std::size_t index = entry.key->hash % capacity;
      while (true)
      {
        TableEntry* candidate = &new_entries[index];
        if (candidate->key == nullptr)
        {
          return candidate;
        }
        index = (index + 1) % capacity;
      }
    }();
    dest->key = entry.key;
    dest->value = entry.value;
    ++count_;
  }

  entries_.swap(new_entries);
}

bool Table::set(ObjString* key, Value value)
{
  if (entries_.empty() || (count_ + 1) > static_cast<std::size_t>(entries_.size() * kMaxLoad))
  {
    const auto capacity = entries_.empty() ? 8 : entries_.size() * 2;
    adjust_capacity(capacity);
  }

  auto* entry = find_entry(key);
  const bool is_new_key = entry->key == nullptr;
  if (is_new_key && entry->value.is_nil())
  {
    ++count_;
  }
  entry->key = key;
  entry->value = value;
  return is_new_key;
}

bool Table::remove(ObjString* key)
{
  if (count_ == 0)
  {
    return false;
  }
  auto* entry = find_entry(key);
  if (!entry || entry->key == nullptr)
  {
    return false;
  }
  entry->key = nullptr;
  entry->value = Value::Bool(true);  // tombstone marker
  return true;
}

ObjString* Table::find_string(const char* chars, std::size_t length, uint32_t hash) const
{
  if (count_ == 0 || entries_.empty())
  {
    return nullptr;
  }
  std::size_t index = hash % entries_.size();
  while (true)
  {
    const auto& entry = entries_[index];
    if (entry.key == nullptr)
    {
      if (entry.value.is_nil())
      {
        return nullptr;
      }
    }
    else if (entry.key->length == length && entry.key->hash == hash &&
             std::memcmp(entry.key->chars, chars, length) == 0)
    {
      return entry.key;
    }
    index = (index + 1) % entries_.size();
  }
}

void mark_table(Table& table)
{
  for (auto& entry : table.entries_mut())
  {
    if (entry.key)
    {
      mark_object(reinterpret_cast<Obj*>(entry.key));
      mark_value(entry.value);
    }
  }
}

void table_remove_white(Table& table)
{
  for (auto& entry : table.entries_mut())
  {
    if (entry.key && !entry.key->marked)
    {
      entry.key = nullptr;
      entry.value = Value::Bool(true);  // tombstone marker
    }
  }
}
}  // namespace neamc::vm
