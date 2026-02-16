//
// Neam Virtual Machine - Value model
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace neamc::vm
{
enum class ObjType;
struct Obj;
struct ObjString;
struct ObjFunction;
struct ObjNative;
struct ObjList;
struct ObjMap;
struct ObjSkill;
struct ObjAgent;
struct ObjContext;
struct ObjEnv;
struct ObjKnowledge;
struct ObjOption;
struct ObjFuture;
// v0.7.0
struct ObjRange;
struct ObjIterState;
struct ObjSet;
struct ObjTuple;
// v0.7.1
struct ObjStructDef;
struct ObjStruct;
struct ObjImplTable;
// v0.7.1 Phase 2
struct ObjTraitDef;
struct ObjSealedDef;
struct ObjVariant;
// v0.8
struct ObjClawAgent;
struct ObjForgeAgent;
struct ObjChannel;
struct ObjWorkspace;
struct ObjLoopContext;

enum class ValueType
{
  Nil,
  Bool,
  Number,
  Obj
};

struct Value
{
  ValueType type{ValueType::Nil};
  union
  {
    bool boolean;
    double number;
    Obj* obj;
  } as{};

  static Value Nil();
  static Value Bool(bool value);
  static Value Number(double value);
  static Value ObjVal(Obj* object);

  static Value String(const char* chars, std::size_t length);
  static Value FunctionValue(ObjFunction* function);
  static Value Native(ObjNative* native);
  static Value List(ObjList* list);
  static Value Map(ObjMap* map);
  static Value Skill(ObjSkill* skill);
  static Value Agent(ObjAgent* agent);
  static Value Context(ObjContext* context);
  static Value Env(ObjEnv* env);
  static Value Knowledge(ObjKnowledge* knowledge);
  static Value Option(ObjOption* option);
  static Value Future(ObjFuture* future);
  // v0.7.0
  static Value Range(ObjRange* range);
  static Value IterState(ObjIterState* iter);
  static Value Set(ObjSet* set);
  static Value Tuple(ObjTuple* tuple);
  // v0.7.1
  static Value StructDef(ObjStructDef* def);
  static Value Struct(ObjStruct* obj);
  static Value ImplTable(ObjImplTable* table);
  // v0.7.1 Phase 2
  static Value TraitDef(ObjTraitDef* def);
  static Value SealedDef(ObjSealedDef* def);
  static Value Variant(ObjVariant* variant);
  // v0.8
  static Value ClawAgent(ObjClawAgent* claw);
  static Value ForgeAgent(ObjForgeAgent* forge);
  static Value Channel(ObjChannel* channel);
  static Value Workspace(ObjWorkspace* workspace);
  static Value LoopContext(ObjLoopContext* loop_ctx);

  bool is_nil() const { return type == ValueType::Nil; }
  bool is_bool() const { return type == ValueType::Bool; }
  bool is_number() const { return type == ValueType::Number; }
  bool is_obj() const { return type == ValueType::Obj; }
  bool is_string() const;
  bool is_function() const;
  bool is_native() const;
  bool is_list() const;
  bool is_map() const;
  bool is_skill() const;
  bool is_agent() const;
  bool is_context() const;
  bool is_env() const;
  bool is_knowledge() const;
  bool is_option() const;
  bool is_future() const;
  // v0.7.0
  bool is_range() const;
  bool is_iter_state() const;
  bool is_set() const;
  bool is_tuple() const;
  // v0.7.1
  bool is_struct_def() const;
  bool is_struct() const;
  bool is_impl_table() const;
  // v0.7.1 Phase 2
  bool is_trait_def() const;
  bool is_sealed_def() const;
  bool is_variant() const;
  // v0.8
  bool is_claw_agent() const;
  bool is_forge_agent() const;
  bool is_channel() const;
  bool is_workspace() const;
  bool is_loop_context() const;

  bool as_bool() const;
  double as_number() const;
  Obj* as_obj() const;
};

bool is_obj_type(const Value& value, ObjType type);
std::string value_to_string(const Value& value);
ObjString* as_string(const Value& value);
ObjFunction* as_function(const Value& value);
ObjNative* as_native(const Value& value);
ObjList* as_list(const Value& value);
ObjMap* as_map(const Value& value);
ObjSkill* as_skill(const Value& value);
ObjAgent* as_agent(const Value& value);
ObjContext* as_context(const Value& value);
ObjEnv* as_env(const Value& value);
ObjKnowledge* as_knowledge(const Value& value);
ObjOption* as_option(const Value& value);
ObjFuture* as_future(const Value& value);
// v0.7.0
ObjRange* as_range(const Value& value);
ObjIterState* as_iter_state(const Value& value);
ObjSet* as_set(const Value& value);
ObjTuple* as_tuple(const Value& value);
// v0.7.1
ObjStructDef* as_struct_def(const Value& value);
ObjStruct* as_struct(const Value& value);
ObjImplTable* as_impl_table(const Value& value);
// v0.7.1 Phase 2
ObjTraitDef* as_trait_def(const Value& value);
ObjSealedDef* as_sealed_def(const Value& value);
ObjVariant* as_variant(const Value& value);
// v0.8
ObjClawAgent* as_claw_agent(const Value& value);
ObjForgeAgent* as_forge_agent(const Value& value);
ObjChannel* as_channel(const Value& value);
ObjWorkspace* as_workspace(const Value& value);
ObjLoopContext* as_loop_context(const Value& value);

}  // namespace neamc::vm
