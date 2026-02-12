//
// Neam Virtual Machine - Trait + Sealed type system implementation (v0.7.1 Phase 2)
//

#include "neamc/vm/sealed_type.hpp"

#include <cstdlib>
#include <stdexcept>

#include "neamc/vm/memory.hpp"
#include "neamc/vm/value_hash.hpp"

namespace neamc::vm
{
// ---- Factory functions ----

ObjTraitDef* new_trait_def(const std::string& name)
{
  auto* def = static_cast<ObjTraitDef*>(
      reallocate(nullptr, 0, sizeof(ObjTraitDef)));
  new (def) ObjTraitDef();
  def->type = ObjType::OBJ_TRAIT_DEF;
  def->next = objects;
  objects = def;
  def->name = name;
  return def;
}

ObjSealedDef* new_sealed_def(const std::string& name)
{
  auto* def = static_cast<ObjSealedDef*>(
      reallocate(nullptr, 0, sizeof(ObjSealedDef)));
  new (def) ObjSealedDef();
  def->type = ObjType::OBJ_SEALED_DEF;
  def->next = objects;
  objects = def;
  def->name = name;
  return def;
}

ObjVariant* new_variant(ObjSealedDef* sealed_def, uint16_t tag,
                         std::vector<Value> field_values)
{
  auto* v = static_cast<ObjVariant*>(
      reallocate(nullptr, 0, sizeof(ObjVariant)));
  new (v) ObjVariant();
  v->type = ObjType::OBJ_VARIANT;
  v->next = objects;
  objects = v;
  v->sealed_def = sealed_def;
  v->tag = tag;
  v->field_values = std::move(field_values);
  return v;
}

// ---- Helpers ----

std::string variant_to_string(const ObjVariant* v)
{
  const auto& info = v->sealed_def->variants[v->tag];
  std::string result = v->sealed_def->name + "." + info.name;
  if (!info.fields.empty())
  {
    result += "(";
    for (std::size_t i = 0; i < info.fields.size(); ++i)
    {
      if (i > 0) result += ", ";
      result += info.fields[i].name + ": " + value_to_string(v->field_values[i]);
    }
    result += ")";
  }
  return result;
}

bool variant_equal(const ObjVariant* a, const ObjVariant* b)
{
  if (a->sealed_def != b->sealed_def) return false;
  if (a->tag != b->tag) return false;
  if (a->field_values.size() != b->field_values.size()) return false;
  ValueEqual eq;
  for (std::size_t i = 0; i < a->field_values.size(); ++i)
  {
    if (!eq(a->field_values[i], b->field_values[i])) return false;
  }
  return true;
}

uint32_t variant_hash(const ObjVariant* v)
{
  ValueHash hasher;
  uint32_t h = hash_string(v->sealed_def->name.c_str(), v->sealed_def->name.size());
  h ^= static_cast<uint32_t>(v->tag) * 2654435761u;
  for (const auto& field : v->field_values)
  {
    h ^= static_cast<uint32_t>(hasher(field)) + 0x9e3779b9u + (h << 6) + (h >> 2);
  }
  return h;
}

std::string sealed_def_to_string(const ObjSealedDef* def)
{
  return "sealed " + def->name;
}

}  // namespace neamc::vm
