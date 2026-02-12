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
  if (get_current_vm())
  {
    if (auto* interned = get_current_vm()->strings().find_string(chars, length, hash))
    {
      reallocate(chars, sizeof(char) * (length + 1), 0);
      return interned;
    }
  }
  auto* string = allocate_string(chars, length, hash);
  if (get_current_vm())
  {
    get_current_vm()->push_root(Value::ObjVal(reinterpret_cast<Obj*>(string)));
    get_current_vm()->strings().set(string, Value::Nil());
    get_current_vm()->pop_root();
  }
  return string;
}

ObjString* copy_string(const char* chars, std::size_t length)
{
  const auto hash = hash_string(chars, length);
  if (get_current_vm())
  {
    if (auto* interned = get_current_vm()->strings().find_string(chars, length, hash))
    {
      return interned;
    }
  }
  char* heap_chars = ALLOCATE(char, length + 1);
  std::memcpy(heap_chars, chars, length);
  heap_chars[length] = '\0';
  auto* string = allocate_string(heap_chars, length, hash);
  if (get_current_vm())
  {
    get_current_vm()->push_root(Value::ObjVal(reinterpret_cast<Obj*>(string)));
    get_current_vm()->strings().set(string, Value::Nil());
    get_current_vm()->pop_root();
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

ObjSkill* new_skill(ObjString* name, ObjString* description, ObjMap* params,
                    std::vector<std::string> param_names, ObjFunction* impl)
{
  auto* skill = allocate_object<ObjSkill>(ObjType::OBJ_SKILL);
  skill->name = name;
  skill->description = description;
  skill->params = params;
  skill->param_names = std::move(param_names);
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
                    ObjList* connected_knowledge, ObjContext* context)
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
  agent->connected_knowledge = connected_knowledge;
  agent->context = context;
  return agent;
}

ObjEnv* new_env()
{
  auto* env = allocate_object<ObjEnv>(ObjType::OBJ_ENV);
  return env;
}

ObjKnowledge* new_knowledge(ObjString* name, ObjString* vector_store, ObjString* embedding_model,
                            std::size_t chunk_size, std::size_t chunk_overlap,
                            std::vector<knowledge::Source> sources,
                            RetrievalStrategy retrieval_strategy,
                            RetrievalStrategyOptions strategy_options)
{
  auto* knowledge = allocate_object<ObjKnowledge>(ObjType::OBJ_KNOWLEDGE);
  knowledge->name = name;
  knowledge->vector_store = vector_store;
  knowledge->embedding_model = embedding_model;
  knowledge->chunk_size = chunk_size;
  knowledge->chunk_overlap = chunk_overlap;
  knowledge->sources = std::move(sources);
  knowledge->retrieval_strategy = retrieval_strategy;
  knowledge->strategy_options = strategy_options;
  return knowledge;
}

ObjOption* new_option(bool has_value, Value value)
{
  auto* option = allocate_object<ObjOption>(ObjType::OBJ_OPTION);
  option->has_value = has_value;
  option->value = std::move(value);
  return option;
}

ObjFuture* new_future(std::shared_ptr<async::Future<Value>> future)
{
  auto* obj_future = allocate_object<ObjFuture>(ObjType::OBJ_FUTURE);
  obj_future->future = std::move(future);
  return obj_future;
}

ObjRange* new_range(int64_t start, int64_t end, int64_t step)
{
  auto* range = allocate_object<ObjRange>(ObjType::OBJ_RANGE);
  range->start = start;
  range->end = end;
  range->step = step;
  return range;
}

ObjIterState* new_iter_state()
{
  auto* iter = allocate_object<ObjIterState>(ObjType::OBJ_ITER_STATE);
  return iter;
}

ObjSet* new_set(std::unordered_set<Value, ValueHash, ValueEqual> items)
{
  auto* set = allocate_object<ObjSet>(ObjType::OBJ_SET);
  set->items = std::move(items);
  return set;
}

ObjTuple* new_tuple(std::vector<Value> items)
{
  auto* tuple = allocate_object<ObjTuple>(ObjType::OBJ_TUPLE);
  tuple->items = std::move(items);
  tuple->hash_cache = 0;
  tuple->hash_computed = false;
  return tuple;
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
