// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Value model implementation
//

#include "neamc/vm/value.hpp"

#include <stdexcept>

#include "neamc/vm/object.hpp"

namespace neamc::vm
{
Value Value::Nil()
{
  return Value{};
}

Value Value::Bool(bool value)
{
  Value v;
  v.type = ValueType::Bool;
  v.as.boolean = value;
  return v;
}

Value Value::Number(double value)
{
  Value v;
  v.type = ValueType::Number;
  v.as.number = value;
  return v;
}

Value Value::ObjVal(Obj* object)
{
  Value v;
  v.type = ValueType::Obj;
  v.as.obj = object;
  return v;
}

Value Value::String(const char* chars, std::size_t length)
{
  return Value::ObjVal(copy_string(chars, length));
}

Value Value::FunctionValue(ObjFunction* function)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(function));
}

Value Value::Native(ObjNative* native)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(native));
}

Value Value::List(ObjList* list)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(list));
}

Value Value::Map(ObjMap* map)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(map));
}

Value Value::Skill(ObjSkill* skill)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(skill));
}

Value Value::Agent(ObjAgent* agent)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(agent));
}

Value Value::Context(ObjContext* context)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(context));
}

Value Value::Env(ObjEnv* env)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(env));
}

Value Value::Knowledge(ObjKnowledge* knowledge)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(knowledge));
}

Value Value::Option(ObjOption* option)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(option));
}

Value Value::Future(ObjFuture* future)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(future));
}

bool Value::as_bool() const
{
  if (!is_bool())
  {
    throw std::runtime_error("Expected bool value");
  }
  return as.boolean;
}

double Value::as_number() const
{
  if (!is_number())
  {
    throw std::runtime_error("Expected number value");
  }
  return as.number;
}

Obj* Value::as_obj() const
{
  if (!is_obj())
  {
    throw std::runtime_error("Expected object value");
  }
  return as.obj;
}

bool Value::is_string() const
{
  return is_obj_type(*this, ObjType::OBJ_STRING);
}

bool Value::is_function() const
{
  return is_obj_type(*this, ObjType::OBJ_FUNCTION);
}

bool Value::is_native() const
{
  return is_obj_type(*this, ObjType::OBJ_NATIVE);
}

bool Value::is_list() const
{
  return is_obj_type(*this, ObjType::OBJ_LIST);
}

bool Value::is_map() const
{
  return is_obj_type(*this, ObjType::OBJ_MAP);
}

bool Value::is_skill() const
{
  return is_obj_type(*this, ObjType::OBJ_SKILL);
}

bool Value::is_agent() const
{
  return is_obj_type(*this, ObjType::OBJ_AGENT);
}

bool Value::is_context() const
{
  return is_obj_type(*this, ObjType::OBJ_CONTEXT);
}

bool Value::is_env() const
{
  return is_obj_type(*this, ObjType::OBJ_ENV);
}

bool Value::is_knowledge() const
{
  return is_obj_type(*this, ObjType::OBJ_KNOWLEDGE);
}

bool Value::is_option() const
{
  return is_obj_type(*this, ObjType::OBJ_OPTION);
}

bool Value::is_future() const
{
  return is_obj_type(*this, ObjType::OBJ_FUTURE);
}

bool is_obj_type(const Value& value, ObjType type)
{
  return value.is_obj() && value.as_obj()->type == type;
}

ObjString* as_string(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_STRING))
  {
    throw std::runtime_error("Expected string object");
  }
  return reinterpret_cast<ObjString*>(value.as.obj);
}

ObjFunction* as_function(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_FUNCTION))
  {
    throw std::runtime_error("Expected function object");
  }
  return reinterpret_cast<ObjFunction*>(value.as.obj);
}

ObjNative* as_native(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_NATIVE))
  {
    throw std::runtime_error("Expected native object");
  }
  return reinterpret_cast<ObjNative*>(value.as.obj);
}

ObjList* as_list(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_LIST))
  {
    throw std::runtime_error("Expected list object");
  }
  return reinterpret_cast<ObjList*>(value.as.obj);
}

ObjMap* as_map(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_MAP))
  {
    throw std::runtime_error("Expected map object");
  }
  return reinterpret_cast<ObjMap*>(value.as.obj);
}

ObjSkill* as_skill(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_SKILL))
  {
    throw std::runtime_error("Expected skill object");
  }
  return reinterpret_cast<ObjSkill*>(value.as.obj);
}

ObjAgent* as_agent(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_AGENT))
  {
    throw std::runtime_error("Expected agent object");
  }
  return reinterpret_cast<ObjAgent*>(value.as.obj);
}

ObjContext* as_context(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_CONTEXT))
  {
    throw std::runtime_error("Expected context object");
  }
  return reinterpret_cast<ObjContext*>(value.as.obj);
}

ObjEnv* as_env(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_ENV))
  {
    throw std::runtime_error("Expected env object");
  }
  return reinterpret_cast<ObjEnv*>(value.as.obj);
}

ObjKnowledge* as_knowledge(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_KNOWLEDGE))
  {
    throw std::runtime_error("Expected knowledge object");
  }
  return reinterpret_cast<ObjKnowledge*>(value.as.obj);
}

ObjOption* as_option(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_OPTION))
  {
    throw std::runtime_error("Expected option object");
  }
  return reinterpret_cast<ObjOption*>(value.as.obj);
}

ObjFuture* as_future(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_FUTURE))
  {
    throw std::runtime_error("Expected future object");
  }
  return reinterpret_cast<ObjFuture*>(value.as.obj);
}
}  // namespace neamc::vm
