//
// Neam Virtual Machine - Value hashing implementation (v0.7.0)
//

#include "neamc/vm/value_hash.hpp"

#include <cstring>
#include <functional>
#include <stdexcept>

#include "neamc/vm/object.hpp"
#include "neamc/vm/struct_type.hpp"
#include "neamc/vm/sealed_type.hpp"

namespace neamc::vm
{
namespace
{
std::size_t hash_combine(std::size_t seed, std::size_t h)
{
  seed ^= h + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  return seed;
}
}  // namespace

std::size_t ValueHash::operator()(const Value& v) const
{
  switch (v.type)
  {
    case ValueType::Nil:
      return 0;
    case ValueType::Bool:
      return std::hash<bool>{}(v.as.boolean);
    case ValueType::Number:
      return std::hash<double>{}(v.as.number);
    case ValueType::Obj:
    {
      auto* obj = v.as.obj;
      if (obj->type == ObjType::OBJ_STRING)
      {
        auto* str = reinterpret_cast<ObjString*>(obj);
        return static_cast<std::size_t>(str->hash);
      }
      if (obj->type == ObjType::OBJ_TUPLE)
      {
        auto* tuple = reinterpret_cast<ObjTuple*>(obj);
        if (tuple->hash_computed)
        {
          return static_cast<std::size_t>(tuple->hash_cache);
        }
        std::size_t seed = 0x7070;
        ValueHash hasher;
        for (const auto& item : tuple->items)
        {
          seed = hash_combine(seed, hasher(item));
        }
        tuple->hash_cache = static_cast<uint32_t>(seed);
        tuple->hash_computed = true;
        return seed;
      }
      // v0.7.1: Struct hashing
      if (obj->type == ObjType::OBJ_STRUCT)
      {
        auto* s = reinterpret_cast<ObjStruct*>(obj);
        return static_cast<std::size_t>(struct_hash(s));
      }
      // v0.7.1 Phase 2: Variant hashing
      if (obj->type == ObjType::OBJ_VARIANT)
      {
        auto* v = reinterpret_cast<ObjVariant*>(obj);
        return static_cast<std::size_t>(variant_hash(v));
      }
      throw std::runtime_error("Unhashable type: cannot hash this object");
    }
  }
  return 0;
}

bool ValueEqual::operator()(const Value& a, const Value& b) const
{
  if (a.type != b.type)
  {
    return false;
  }
  switch (a.type)
  {
    case ValueType::Nil:
      return true;
    case ValueType::Bool:
      return a.as.boolean == b.as.boolean;
    case ValueType::Number:
      return a.as.number == b.as.number;
    case ValueType::Obj:
    {
      auto* obj_a = a.as.obj;
      auto* obj_b = b.as.obj;
      if (obj_a->type != obj_b->type)
      {
        return false;
      }
      if (obj_a->type == ObjType::OBJ_STRING)
      {
        auto* sa = reinterpret_cast<ObjString*>(obj_a);
        auto* sb = reinterpret_cast<ObjString*>(obj_b);
        return sa->length == sb->length && std::memcmp(sa->chars, sb->chars, sa->length) == 0;
      }
      if (obj_a->type == ObjType::OBJ_TUPLE)
      {
        auto* ta = reinterpret_cast<ObjTuple*>(obj_a);
        auto* tb = reinterpret_cast<ObjTuple*>(obj_b);
        if (ta->items.size() != tb->items.size())
        {
          return false;
        }
        ValueEqual eq;
        for (std::size_t i = 0; i < ta->items.size(); ++i)
        {
          if (!eq(ta->items[i], tb->items[i]))
          {
            return false;
          }
        }
        return true;
      }
      if (obj_a->type == ObjType::OBJ_SET)
      {
        auto* sa = reinterpret_cast<ObjSet*>(obj_a);
        auto* sb = reinterpret_cast<ObjSet*>(obj_b);
        if (sa->items.size() != sb->items.size())
        {
          return false;
        }
        for (const auto& item : sa->items)
        {
          if (sb->items.find(item) == sb->items.end())
          {
            return false;
          }
        }
        return true;
      }
      if (obj_a->type == ObjType::OBJ_LIST)
      {
        auto* la = reinterpret_cast<ObjList*>(obj_a);
        auto* lb = reinterpret_cast<ObjList*>(obj_b);
        if (la->items.size() != lb->items.size())
        {
          return false;
        }
        ValueEqual eq;
        for (std::size_t i = 0; i < la->items.size(); ++i)
        {
          if (!eq(la->items[i], lb->items[i]))
          {
            return false;
          }
        }
        return true;
      }
      // v0.7.1: Struct structural equality
      if (obj_a->type == ObjType::OBJ_STRUCT)
      {
        auto* sa = reinterpret_cast<ObjStruct*>(obj_a);
        auto* sb = reinterpret_cast<ObjStruct*>(obj_b);
        return struct_equal(sa, sb);
      }
      // v0.7.1 Phase 2: Variant structural equality
      if (obj_a->type == ObjType::OBJ_VARIANT)
      {
        auto* va = reinterpret_cast<ObjVariant*>(obj_a);
        auto* vb = reinterpret_cast<ObjVariant*>(obj_b);
        return variant_equal(va, vb);
      }
      return obj_a == obj_b;
    }
  }
  return false;
}

bool is_hashable(const Value& v)
{
  if (v.is_nil() || v.is_bool() || v.is_number())
  {
    return true;
  }
  if (v.is_string())
  {
    return true;
  }
  if (v.is_obj())
  {
    auto* obj = v.as.obj;
    if (obj->type == ObjType::OBJ_TUPLE)
    {
      auto* tuple = reinterpret_cast<ObjTuple*>(obj);
      for (const auto& item : tuple->items)
      {
        if (!is_hashable(item))
        {
          return false;
        }
      }
      return true;
    }
    // v0.7.1: Structs are hashable if all fields are hashable
    if (obj->type == ObjType::OBJ_STRUCT)
    {
      auto* s = reinterpret_cast<ObjStruct*>(obj);
      for (const auto& field : s->fields)
      {
        if (!is_hashable(field))
        {
          return false;
        }
      }
      return true;
    }
    // v0.7.1 Phase 2: Variants are hashable if all fields are hashable
    if (obj->type == ObjType::OBJ_VARIANT)
    {
      auto* v = reinterpret_cast<ObjVariant*>(obj);
      for (const auto& field : v->field_values)
      {
        if (!is_hashable(field))
        {
          return false;
        }
      }
      return true;
    }
  }
  return false;
}
}  // namespace neamc::vm
