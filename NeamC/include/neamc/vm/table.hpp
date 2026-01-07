//
// Neam Virtual Machine - Open addressing hash table
//

#pragma once

#include <cstddef>
#include <vector>

#include "neamc/vm/object.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm
{
struct TableEntry
{
  ObjString* key{nullptr};
  Value value{Value::Nil()};
};

class Table
{
public:
  bool get(ObjString* key, Value* out_value) const;
  bool set(ObjString* key, Value value);
  bool remove(ObjString* key);
  ObjString* find_string(const char* chars, std::size_t length, uint32_t hash) const;
  void clear();

  const std::vector<TableEntry>& entries() const { return entries_; }
  std::vector<TableEntry>& entries_mut() { return entries_; }
  std::size_t count() const { return count_; }

private:
  TableEntry* find_entry(ObjString* key) const;
  void adjust_capacity(std::size_t capacity);

  std::vector<TableEntry> entries_{};
  std::size_t count_{0};
};

void mark_table(Table& table);
void table_remove_white(Table& table);
}  // namespace neamc::vm
