//
// Neam Virtual Machine - Object system implementation
//

#include "neamc/vm/object.hpp"

#include <cstring>

#include "neamc/vm/memory.hpp"
#include "neamc/vm/table.hpp"
#include "neamc/vm/vm.hpp"

namespace neamc::vm
{
ObjString* allocate_string(char* chars, std::size_t length, uint32_t hash)
{
  auto* string = allocate_object<ObjString>(ObjType::OBJ_STRING);
  string->length = length;
  string->chars = chars;
  string->hash = hash;
  return string;
}

ObjString* take_string(char* chars, std::size_t length)
{
  const auto hash = hash_string(chars, length);
  if (current_vm)
  {
    if (auto* interned = current_vm->strings().find_string(chars, length, hash))
    {
      reallocate(chars, sizeof(char) * (length + 1), 0);
      return interned;
    }
  }
  auto* string = allocate_string(chars, length, hash);
  if (current_vm)
  {
    current_vm->push_root(Value::ObjVal(reinterpret_cast<Obj*>(string)));
    current_vm->strings().set(string, Value::Nil());
    current_vm->pop_root();
  }
  return string;
}

ObjString* copy_string(const char* chars, std::size_t length)
{
  const auto hash = hash_string(chars, length);
  if (current_vm)
  {
    if (auto* interned = current_vm->strings().find_string(chars, length, hash))
    {
      return interned;
    }
  }
  char* heap_chars = ALLOCATE(char, length + 1);
  std::memcpy(heap_chars, chars, length);
  heap_chars[length] = '\0';
  auto* string = allocate_string(heap_chars, length, hash);
  if (current_vm)
  {
    current_vm->push_root(Value::ObjVal(reinterpret_cast<Obj*>(string)));
    current_vm->strings().set(string, Value::Nil());
    current_vm->pop_root();
  }
  return string;
}

ObjFunction* new_function()
{
  auto* function = allocate_object<ObjFunction>(ObjType::OBJ_FUNCTION);
  function->arity = 0;
  function->name = nullptr;
  return function;
}

ObjNative* new_native(ObjString* name, int arity, NativeFn function)
{
  auto* native = allocate_object<ObjNative>(ObjType::OBJ_NATIVE);
  native->arity = arity;
  native->name = name;
  native->function = function;
  return native;
}

ObjList* new_list(std::vector<Value> items)
{
  auto* list = allocate_object<ObjList>(ObjType::OBJ_LIST);
  list->items = std::move(items);
  return list;
}

ObjMap* new_map(std::unordered_map<std::string, Value> entries)
{
  auto* map = allocate_object<ObjMap>(ObjType::OBJ_MAP);
  map->entries = std::move(entries);
  return map;
}

ObjSkill* new_skill(ObjString* name, ObjString* description, ObjMap* params, ObjFunction* impl)
{
  auto* skill = allocate_object<ObjSkill>(ObjType::OBJ_SKILL);
  skill->name = name;
  skill->description = description;
  skill->params = params;
  skill->impl = impl;
  return skill;
}

ObjContext* new_context()
{
  auto* context = allocate_object<ObjContext>(ObjType::OBJ_CONTEXT);
  return context;
}

ObjAgent* new_agent(ObjString* name, ObjString* provider, ObjString* model, ObjString* endpoint,
                    ObjString* api_key_env, ObjString* system, double temperature, ObjList* skills,
                    ObjContext* context)
{
  auto* agent = allocate_object<ObjAgent>(ObjType::OBJ_AGENT);
  agent->name = name;
  agent->provider = provider;
  agent->model = model;
  agent->endpoint = endpoint;
  agent->api_key_env = api_key_env;
  agent->system = system;
  agent->temperature = temperature;
  agent->skills = skills;
  agent->context = context;
  return agent;
}

ObjEnv* new_env()
{
  auto* env = allocate_object<ObjEnv>(ObjType::OBJ_ENV);
  return env;
}

uint32_t hash_string(const char* key, std::size_t length)
{
  constexpr uint32_t fnv_offset = 2166136261u;
  constexpr uint32_t fnv_prime = 16777619u;
  uint32_t hash = fnv_offset;
  for (std::size_t i = 0; i < length; ++i)
  {
    hash ^= static_cast<uint8_t>(key[i]);
    hash *= fnv_prime;
  }
  return hash;
}
}  // namespace neamc::vm
