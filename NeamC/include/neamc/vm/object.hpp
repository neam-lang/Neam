//
// Neam Virtual Machine - Object system
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/knowledge.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm::async
{
template <typename T>
class Future;
}

namespace neamc::vm
{
class VirtualMachine;
struct Obj
{
  ObjType type;
  bool marked{false};
  Obj* next{nullptr};
};

enum class ObjType
{
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_LIST,
  OBJ_MAP,
  OBJ_SKILL,
  OBJ_AGENT,
  OBJ_CONTEXT,
  OBJ_ENV,
  OBJ_KNOWLEDGE,
  OBJ_OPTION,
  OBJ_FUTURE
};

struct ObjString : Obj
{
  std::size_t length{0};
  char* chars{nullptr};
  uint32_t hash{0};
};

struct ObjFunction : Obj
{
  int arity{0};
  Chunk chunk;
  ObjString* name{nullptr};
};

using NativeFn = Value (*)(VirtualMachine& vm, int argCount, Value* args);

struct ObjNative : Obj
{
  int arity{0};
  ObjString* name{nullptr};
  NativeFn function{nullptr};
};

struct ObjList : Obj
{
  std::vector<Value> items;
};

struct ObjMap : Obj
{
  std::unordered_map<std::string, Value> entries;
};

struct ObjSkill : Obj
{
  ObjString* name{nullptr};
  ObjString* description{nullptr};
  ObjMap* params{nullptr};
  std::vector<std::string> param_names;
  ObjFunction* impl{nullptr};
};

struct ObjContext : Obj
{
  struct Message
  {
    std::string role;
    std::string content;
  };
  std::vector<Message> history;
};

struct ObjAgent : Obj
{
  ObjString* name{nullptr};
  ObjString* provider{nullptr};
  ObjString* model{nullptr};
  ObjString* endpoint{nullptr};
  ObjString* api_key_env{nullptr};
  ObjString* system{nullptr};
  double temperature{0.0};
  ObjList* skills{nullptr};
  ObjList* connected_knowledge{nullptr};
  ObjContext* context{nullptr};
};

struct ObjEnv : Obj
{
  std::unordered_set<std::string> allowed_skills;
  std::unordered_map<std::string, std::string> config;
};

struct ObjKnowledge : Obj
{
  ObjString* name{nullptr};
  ObjString* vector_store{nullptr};
  ObjString* embedding_model{nullptr};
  std::size_t chunk_size{0};
  std::size_t chunk_overlap{0};
  std::vector<knowledge::Source> sources;
  knowledge::VectorStore store{8};
};

struct ObjOption : Obj
{
  bool has_value{false};
  Value value{Value::Nil()};
};

struct ObjFuture : Obj
{
  std::shared_ptr<async::Future<Value>> future;
};

ObjString* allocate_string(char* chars, std::size_t length, uint32_t hash);
ObjString* copy_string(const char* chars, std::size_t length);
ObjString* take_string(char* chars, std::size_t length);
ObjFunction* new_function();
ObjNative* new_native(ObjString* name, int arity, NativeFn function);
ObjList* new_list(std::vector<Value> items);
ObjMap* new_map(std::unordered_map<std::string, Value> entries);
ObjSkill* new_skill(ObjString* name, ObjString* description, ObjMap* params,
                    std::vector<std::string> param_names, ObjFunction* impl);
ObjContext* new_context();
ObjAgent* new_agent(ObjString* name, ObjString* provider, ObjString* model, ObjString* endpoint,
                    ObjString* api_key_env, ObjString* system, double temperature, ObjList* skills,
                    ObjList* connected_knowledge, ObjContext* context);
ObjEnv* new_env();
ObjKnowledge* new_knowledge(ObjString* name, ObjString* vector_store, ObjString* embedding_model,
                            std::size_t chunk_size, std::size_t chunk_overlap,
                            std::vector<knowledge::Source> sources);
ObjOption* new_option(bool has_value, Value value);
ObjFuture* new_future(std::shared_ptr<async::Future<Value>> future);

uint32_t hash_string(const char* key, std::size_t length);
}  // namespace neamc::vm
