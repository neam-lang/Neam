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
#include "neamc/vm/external_skill.hpp"
#include "neamc/vm/knowledge.hpp"
#include "neamc/vm/value.hpp"
#include "neamc/vm/value_hash.hpp"

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
  OBJ_FUTURE,
  // v0.7.0: Data types
  OBJ_RANGE,
  OBJ_ITER_STATE,
  OBJ_SET,
  OBJ_TUPLE,
  // v0.7.1: OOP
  OBJ_STRUCT_DEF,
  OBJ_STRUCT,
  OBJ_IMPL_TABLE,
  // v0.7.1 Phase 2: trait + sealed
  OBJ_TRAIT_DEF,
  OBJ_SEALED_DEF,
  OBJ_VARIANT,
  // v0.8: Agent type system
  OBJ_CLAW_AGENT,
  OBJ_FORGE_AGENT,
  OBJ_CHANNEL,
  OBJ_WORKSPACE,
  OBJ_LOOP_CONTEXT
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
  ObjFunction* impl{nullptr};              // Non-null for local skills
  ExternalSkillConfig* external{nullptr};   // Non-null for external skills (v0.6.7)
  bool sensitive{false};                    // v0.6.9 D10: requires human confirmation
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

// Retrieval strategy enum for the VM
enum class RetrievalStrategy
{
  kBasic = 0,
  kMMR = 1,
  kHybrid = 2,
  kHyDE = 3,
  kSelfRAG = 4,
  kCRAG = 5,
  kAgentic = 6,
  kGraphRAG = 7
};

// Strategy options for advanced RAG
struct RetrievalStrategyOptions
{
  std::size_t top_k{4};
  double relevance_threshold{0.5};
  double mmr_lambda{0.5};
  std::size_t num_hypothetical{1};
  bool enable_relevance_check{true};
  bool enable_support_check{true};
  bool enable_web_fallback{false};
  bool enable_query_decomposition{true};
  std::size_t max_corrections{2};
  std::size_t max_iterations{5};
  bool enable_reflection{true};
  std::size_t search_depth{2};
  bool include_communities{true};
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
  RetrievalStrategy retrieval_strategy{RetrievalStrategy::kBasic};
  RetrievalStrategyOptions strategy_options;
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

// v0.7.0: Range type — represents a lazy integer sequence
struct ObjRange : Obj
{
  int64_t start{0};
  int64_t end{0};
  int64_t step{1};
};

// v0.7.0: Iterator state — internal loop iterator
struct ObjIterState : Obj
{
  Value source{Value::Nil()};
  int64_t position{0};
  int64_t end{0};
  int64_t step{1};
  std::vector<Value> snapshot;
};

// v0.7.0: Set type — unordered collection of unique hashable values
struct ObjSet : Obj
{
  std::unordered_set<Value, ValueHash, ValueEqual> items;
};

// v0.7.0: Tuple type — immutable fixed-size collection
struct ObjTuple : Obj
{
  std::vector<Value> items;
  uint32_t hash_cache{0};
  bool hash_computed{false};
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
                            std::vector<knowledge::Source> sources,
                            RetrievalStrategy retrieval_strategy = RetrievalStrategy::kBasic,
                            RetrievalStrategyOptions strategy_options = {});
ObjOption* new_option(bool has_value, Value value);
ObjFuture* new_future(std::shared_ptr<async::Future<Value>> future);
// v0.7.0: New type factories
ObjRange* new_range(int64_t start, int64_t end, int64_t step);
ObjIterState* new_iter_state();
ObjSet* new_set(std::unordered_set<Value, ValueHash, ValueEqual> items);
ObjTuple* new_tuple(std::vector<Value> items);

// v0.7.1: Struct type system — forward declares (definitions in struct_type.hpp)
struct ObjStructDef;
struct ObjStruct;
struct ObjImplTable;
ObjStructDef* new_struct_def(const std::string& name, const std::vector<std::string>& field_names,
                             bool is_mutable);
ObjStruct* new_struct(ObjStructDef* def, std::vector<Value> field_values);
ObjImplTable* new_impl_table();

// v0.7.1 Phase 2: Trait + sealed type system — forward declares (definitions in sealed_type.hpp)
struct ObjTraitDef;
struct ObjSealedDef;
struct ObjVariant;
ObjTraitDef* new_trait_def(const std::string& name);
ObjSealedDef* new_sealed_def(const std::string& name);
ObjVariant* new_variant(ObjSealedDef* sealed_def, uint16_t tag, std::vector<Value> field_values);

// v0.8: Claw/Forge agent type system — forward declares (definitions in claw_agent_type.hpp / forge_agent_type.hpp)
struct ObjClawAgent;
struct ObjForgeAgent;
struct ObjChannel;
struct ObjWorkspace;
struct ObjLoopContext;
ObjClawAgent* new_claw_agent();
ObjForgeAgent* new_forge_agent();
ObjLoopContext* new_loop_context();

uint32_t hash_string(const char* key, std::size_t length);
}  // namespace neamc::vm
