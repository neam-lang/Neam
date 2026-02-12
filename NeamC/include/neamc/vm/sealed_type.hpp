//
// Neam Virtual Machine - Trait + Sealed type system (v0.7.1 Phase 2)
//
// ObjTraitDef   — trait metadata (required methods, default methods, supertraits)
// ObjSealedDef  — sealed type metadata (variant definitions)
// ObjVariant    — sealed variant instance (tag + field values)
//

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "neamc/vm/object.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm
{
// Trait method info — tracks required methods and optional default implementations.
struct TraitMethodInfo
{
  std::string name;
  std::vector<std::string> params;
  ObjFunction* default_impl{nullptr};  // nullptr = required (no default body)
};

// Trait type definition — shared across all implementors.
struct ObjTraitDef : Obj
{
  std::string name;
  std::vector<std::string> supertraits;
  std::vector<TraitMethodInfo> methods;
};

// Variant field info for sealed types.
struct VariantFieldInfo
{
  std::string name;
  uint16_t index{0};
};

// Variant info within a sealed type.
struct VariantInfo
{
  std::string name;
  std::vector<VariantFieldInfo> fields;
  uint16_t tag{0};
};

// Sealed type definition — contains all variant definitions.
struct ObjSealedDef : Obj
{
  std::string name;
  std::vector<VariantInfo> variants;
  std::unordered_map<std::string, uint16_t> variant_index;  // name -> tag
};

// Sealed variant instance — a specific variant with field values.
struct ObjVariant : Obj
{
  ObjSealedDef* sealed_def{nullptr};
  uint16_t tag{0};
  std::vector<Value> field_values;
};

// Factory functions
ObjTraitDef* new_trait_def(const std::string& name);
ObjSealedDef* new_sealed_def(const std::string& name);
ObjVariant* new_variant(ObjSealedDef* sealed_def, uint16_t tag, std::vector<Value> field_values);

// Helpers
std::string variant_to_string(const ObjVariant* v);
bool variant_equal(const ObjVariant* a, const ObjVariant* b);
uint32_t variant_hash(const ObjVariant* v);
std::string sealed_def_to_string(const ObjSealedDef* def);

}  // namespace neamc::vm
