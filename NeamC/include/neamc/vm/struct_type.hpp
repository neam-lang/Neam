//
// Neam Virtual Machine - Struct type system (v0.7.1)
//
// ObjStructDef  — type metadata (field names, mutability flag)
// ObjStruct     — instance (def pointer + field values vector)
// ObjImplTable  — method dispatch table (name -> ObjFunction*)
//

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "neamc/vm/object.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm
{
// Struct type definition — shared across all instances of the same type.
struct ObjStructDef : Obj
{
  std::string name;
  std::vector<std::string> field_names;
  bool is_mutable{false};

  // v0.7.1 Phase 5: Property observers per field
  std::unordered_map<std::string, ObjFunction*> will_set_handlers;
  std::unordered_map<std::string, ObjFunction*> did_set_handlers;
  std::unordered_map<std::string, ObjFunction*> guard_handlers;

  int field_index(const std::string& field_name) const;
};

// Struct instance — holds a pointer to its definition + field values.
struct ObjStruct : Obj
{
  ObjStructDef* def{nullptr};
  std::vector<Value> fields;

  Value get_field(const std::string& field_name) const;
  void set_field(const std::string& field_name, Value value);
};

// Method entry in an impl table.
struct MethodEntry
{
  ObjFunction* function{nullptr};
  bool is_static{false};
};

// Impl table — maps method names to compiled functions for a type.
struct ObjImplTable : Obj
{
  std::string type_name;
  std::unordered_map<std::string, MethodEntry> methods;
};

// Helpers
std::string struct_to_string(const ObjStruct* obj);
bool struct_equal(const ObjStruct* a, const ObjStruct* b);
uint32_t struct_hash(const ObjStruct* obj);
ObjStruct* struct_copy_with(ObjStruct* original,
                            const std::vector<std::pair<std::string, Value>>& overrides);

}  // namespace neamc::vm
