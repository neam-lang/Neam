// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Runtime type info implementation
//

#include "neamc/vm/runtime_type.hpp"

#include "neamc/vm/object.hpp"

namespace neamc::vm
{
namespace
{
RuntimeTypeInfo make_info(const std::string& name, std::size_t size)
{
  return RuntimeTypeInfo{name, size};
}
}

RuntimeTypeInfo runtime_type_of(const Value& value)
{
  switch (value.type)
  {
    case ValueType::Nil:
      return make_info("Nil", 0);
    case ValueType::Bool:
      return make_info("Bool", sizeof(bool));
    case ValueType::Number:
      return make_info("Number", sizeof(double));
    case ValueType::Obj:
      break;
  }

  if (value.is_string())
  {
    return make_info("String", sizeof(ObjString));
  }
  if (value.is_function())
  {
    return make_info("Function", sizeof(ObjFunction));
  }
  if (value.is_native())
  {
    return make_info("Native", sizeof(ObjNative));
  }
  if (value.is_list())
  {
    return make_info("List", sizeof(ObjList));
  }
  if (value.is_map())
  {
    return make_info("Map", sizeof(ObjMap));
  }
  if (value.is_skill())
  {
    return make_info("Skill", sizeof(ObjSkill));
  }
  if (value.is_agent())
  {
    return make_info("Agent", sizeof(ObjAgent));
  }
  if (value.is_context())
  {
    return make_info("Context", sizeof(ObjContext));
  }
  if (value.is_env())
  {
    return make_info("Env", sizeof(ObjEnv));
  }
  if (value.is_knowledge())
  {
    return make_info("Knowledge", sizeof(ObjKnowledge));
  }
  if (value.is_option())
  {
    return make_info("Option", sizeof(ObjOption));
  }
  if (value.is_future())
  {
    return make_info("Future", sizeof(ObjFuture));
  }
  return make_info("Unknown", 0);
}

std::string typeof_name(const Value& value)
{
  return runtime_type_of(value).name;
}
}  // namespace neamc::vm
