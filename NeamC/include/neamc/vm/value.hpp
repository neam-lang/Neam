// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Value model
//

#pragma once

#include <cstddef>
#include <cstdint>

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

  bool as_bool() const;
  double as_number() const;
  Obj* as_obj() const;
};

bool is_obj_type(const Value& value, ObjType type);
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

}  // namespace neamc::vm
