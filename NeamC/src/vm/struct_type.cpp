//
// Neam Virtual Machine - Struct type system implementation (v0.7.1)
//

#include "neamc/vm/struct_type.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

#include "neamc/vm/memory.hpp"
#include "neamc/vm/value_hash.hpp"

namespace neamc::vm
{
// ---- ObjStructDef ----

int ObjStructDef::field_index(const std::string& field_name) const
{
  for (int i = 0; i < static_cast<int>(field_names.size()); ++i)
  {
    if (field_names[static_cast<std::size_t>(i)] == field_name)
    {
      return i;
    }
  }
  return -1;
}

// ---- ObjStruct ----

Value ObjStruct::get_field(const std::string& field_name) const
{
  int idx = def->field_index(field_name);
  if (idx < 0)
  {
    throw std::runtime_error(
        "Field '" + field_name + "' not found on struct '" + def->name + "'");
  }
  return fields[static_cast<std::size_t>(idx)];
}

void ObjStruct::set_field(const std::string& field_name, Value value)
{
  if (!def->is_mutable)
  {
    throw std::runtime_error(
        "Cannot mutate field '" + field_name + "' on immutable struct '" + def->name +
        "'. Use 'mut struct' to allow mutation.");
  }
  int idx = def->field_index(field_name);
  if (idx < 0)
  {
    throw std::runtime_error(
        "Field '" + field_name + "' not found on struct '" + def->name + "'");
  }
  fields[static_cast<std::size_t>(idx)] = value;
}

// ---- Factory functions ----

ObjStructDef* new_struct_def(const std::string& name,
                             const std::vector<std::string>& field_names,
                             bool is_mutable)
{
  auto* def = static_cast<ObjStructDef*>(
      reallocate(nullptr, 0, sizeof(ObjStructDef)));
  new (def) ObjStructDef();
  def->type = ObjType::OBJ_STRUCT_DEF;
  link_object(def);
  def->name = name;
  def->field_names = field_names;
  def->is_mutable = is_mutable;
  return def;
}

ObjStruct* new_struct(ObjStructDef* def, std::vector<Value> field_values)
{
  auto* obj = static_cast<ObjStruct*>(
      reallocate(nullptr, 0, sizeof(ObjStruct)));
  new (obj) ObjStruct();
  obj->type = ObjType::OBJ_STRUCT;
  link_object(obj);
  obj->def = def;
  obj->fields = std::move(field_values);
  return obj;
}

ObjImplTable* new_impl_table()
{
  auto* table = static_cast<ObjImplTable*>(
      reallocate(nullptr, 0, sizeof(ObjImplTable)));
  new (table) ObjImplTable();
  table->type = ObjType::OBJ_IMPL_TABLE;
  link_object(table);
  return table;
}

// ---- Helpers ----

std::string struct_to_string(const ObjStruct* obj)
{
  std::string result = obj->def->name + "(";
  for (std::size_t i = 0; i < obj->def->field_names.size(); ++i)
  {
    if (i > 0) result += ", ";
    result += obj->def->field_names[i] + ": " + value_to_string(obj->fields[i]);
  }
  result += ")";
  return result;
}

bool struct_equal(const ObjStruct* a, const ObjStruct* b)
{
  if (a->def != b->def) return false;
  if (a->fields.size() != b->fields.size()) return false;
  ValueEqual eq;
  for (std::size_t i = 0; i < a->fields.size(); ++i)
  {
    if (!eq(a->fields[i], b->fields[i])) return false;
  }
  return true;
}

uint32_t struct_hash(const ObjStruct* obj)
{
  ValueHash hasher;
  uint32_t h = hash_string(obj->def->name.c_str(), obj->def->name.size());
  for (const auto& field : obj->fields)
  {
    h ^= static_cast<uint32_t>(hasher(field)) + 0x9e3779b9u + (h << 6) + (h >> 2);
  }
  return h;
}

ObjStruct* struct_copy_with(ObjStruct* original,
                            const std::vector<std::pair<std::string, Value>>& overrides)
{
  std::vector<Value> new_fields = original->fields;
  for (const auto& [name, value] : overrides)
  {
    int idx = original->def->field_index(name);
    if (idx < 0)
    {
      throw std::runtime_error(
          "Field '" + name + "' not found on struct '" + original->def->name + "'");
    }
    new_fields[static_cast<std::size_t>(idx)] = value;
  }
  return new_struct(original->def, std::move(new_fields));
}

}  // namespace neamc::vm
