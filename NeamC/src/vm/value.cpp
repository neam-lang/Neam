//
// Neam Virtual Machine - Value model implementation
//

#include "neamc/vm/value.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>

#include "neamc/vm/object.hpp"
#include "neamc/vm/struct_type.hpp"
#include "neamc/vm/sealed_type.hpp"
#include "neamc/vm/claw_agent_type.hpp"
#include "neamc/vm/forge_agent_type.hpp"

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

Value Value::Range(ObjRange* range)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(range));
}

Value Value::IterState(ObjIterState* iter)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(iter));
}

Value Value::Set(ObjSet* set)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(set));
}

Value Value::Tuple(ObjTuple* tuple)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(tuple));
}

Value Value::StructDef(ObjStructDef* def)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(def));
}

Value Value::Struct(ObjStruct* obj)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(obj));
}

Value Value::ImplTable(ObjImplTable* table)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(table));
}

Value Value::TraitDef(ObjTraitDef* def)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(def));
}

Value Value::SealedDef(ObjSealedDef* def)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(def));
}

Value Value::Variant(ObjVariant* variant)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(variant));
}

Value Value::ClawAgent(ObjClawAgent* claw)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(claw));
}

Value Value::ForgeAgent(ObjForgeAgent* forge)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(forge));
}

Value Value::Channel(ObjChannel* channel)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(channel));
}

Value Value::Workspace(ObjWorkspace* workspace)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(workspace));
}

Value Value::LoopContext(ObjLoopContext* loop_ctx)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(loop_ctx));
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

bool Value::is_range() const
{
  return is_obj_type(*this, ObjType::OBJ_RANGE);
}

bool Value::is_iter_state() const
{
  return is_obj_type(*this, ObjType::OBJ_ITER_STATE);
}

bool Value::is_set() const
{
  return is_obj_type(*this, ObjType::OBJ_SET);
}

bool Value::is_tuple() const
{
  return is_obj_type(*this, ObjType::OBJ_TUPLE);
}

bool Value::is_struct_def() const
{
  return is_obj_type(*this, ObjType::OBJ_STRUCT_DEF);
}

bool Value::is_struct() const
{
  return is_obj_type(*this, ObjType::OBJ_STRUCT);
}

bool Value::is_impl_table() const
{
  return is_obj_type(*this, ObjType::OBJ_IMPL_TABLE);
}

bool Value::is_trait_def() const
{
  return is_obj_type(*this, ObjType::OBJ_TRAIT_DEF);
}

bool Value::is_sealed_def() const
{
  return is_obj_type(*this, ObjType::OBJ_SEALED_DEF);
}

bool Value::is_variant() const
{
  return is_obj_type(*this, ObjType::OBJ_VARIANT);
}

bool Value::is_claw_agent() const
{
  return is_obj_type(*this, ObjType::OBJ_CLAW_AGENT);
}

bool Value::is_forge_agent() const
{
  return is_obj_type(*this, ObjType::OBJ_FORGE_AGENT);
}

bool Value::is_channel() const
{
  return is_obj_type(*this, ObjType::OBJ_CHANNEL);
}

bool Value::is_workspace() const
{
  return is_obj_type(*this, ObjType::OBJ_WORKSPACE);
}

bool Value::is_loop_context() const
{
  return is_obj_type(*this, ObjType::OBJ_LOOP_CONTEXT);
}

std::string value_to_string(const Value& value)
{
  if (value.is_string())
  {
    auto* str = as_string(value);
    return std::string(str->chars, str->length);
  }
  if (value.is_number())
  {
    double num = value.as_number();
    if (num == static_cast<int64_t>(num) && std::abs(num) < 1e15)
    {
      return std::to_string(static_cast<int64_t>(num));
    }
    std::ostringstream out;
    out << num;
    return out.str();
  }
  if (value.is_bool())
  {
    return value.as_bool() ? "true" : "false";
  }
  if (value.is_nil())
  {
    return "nil";
  }
  if (value.is_list())
  {
    auto* list = as_list(value);
    std::string result = "[";
    for (std::size_t i = 0; i < list->items.size(); ++i)
    {
      if (i > 0) result += ", ";
      if (list->items[i].is_string())
      {
        result += "\"" + value_to_string(list->items[i]) + "\"";
      }
      else
      {
        result += value_to_string(list->items[i]);
      }
    }
    result += "]";
    return result;
  }
  if (value.is_map())
  {
    auto* map = as_map(value);
    std::string result = "{";
    bool first = true;
    for (const auto& entry : map->entries)
    {
      if (!first) result += ", ";
      result += entry.first + ": ";
      if (entry.second.is_string())
      {
        result += "\"" + value_to_string(entry.second) + "\"";
      }
      else
      {
        result += value_to_string(entry.second);
      }
      first = false;
    }
    result += "}";
    return result;
  }
  if (value.is_option())
  {
    auto* opt = as_option(value);
    if (opt->has_value)
    {
      return "Some(" + value_to_string(opt->value) + ")";
    }
    return "None";
  }
  if (value.is_set())
  {
    auto* set = as_set(value);
    std::string result = "set(";
    bool first = true;
    for (const auto& item : set->items)
    {
      if (!first) result += ", ";
      if (item.is_string())
      {
        result += "\"" + value_to_string(item) + "\"";
      }
      else
      {
        result += value_to_string(item);
      }
      first = false;
    }
    result += ")";
    return result;
  }
  if (value.is_tuple())
  {
    auto* tuple = as_tuple(value);
    std::string result = "(";
    for (std::size_t i = 0; i < tuple->items.size(); ++i)
    {
      if (i > 0) result += ", ";
      if (tuple->items[i].is_string())
      {
        result += "\"" + value_to_string(tuple->items[i]) + "\"";
      }
      else
      {
        result += value_to_string(tuple->items[i]);
      }
    }
    if (tuple->items.size() == 1) result += ",";
    result += ")";
    return result;
  }
  if (value.is_range())
  {
    auto* range = as_range(value);
    if (range->step == 1)
    {
      return "range(" + std::to_string(range->start) + ", " + std::to_string(range->end) + ")";
    }
    return "range(" + std::to_string(range->start) + ", " + std::to_string(range->end) +
           ", " + std::to_string(range->step) + ")";
  }
  if (value.is_struct())
  {
    return struct_to_string(as_struct(value));
  }
  if (value.is_struct_def())
  {
    return "<struct " + as_struct_def(value)->name + ">";
  }
  if (value.is_variant())
  {
    return variant_to_string(as_variant(value));
  }
  if (value.is_sealed_def())
  {
    return sealed_def_to_string(as_sealed_def(value));
  }
  if (value.is_trait_def())
  {
    return "<trait " + as_trait_def(value)->name + ">";
  }
  if (value.is_claw_agent())
  {
    auto* claw = as_claw_agent(value);
    return "<claw agent " + std::string(claw->name->chars, claw->name->length) + ">";
  }
  if (value.is_forge_agent())
  {
    auto* forge = as_forge_agent(value);
    return "<forge agent " + std::string(forge->name->chars, forge->name->length) + ">";
  }
  if (value.is_channel())
  {
    auto* ch = as_channel(value);
    if (ch->name)
      return "<channel " + std::string(ch->name->chars, ch->name->length) + ">";
    return "<channel>";
  }
  if (value.is_workspace())
  {
    auto* ws = as_workspace(value);
    if (ws->path)
      return "<workspace " + std::string(ws->path->chars, ws->path->length) + ">";
    return "<workspace>";
  }
  if (value.is_loop_context())
  {
    return "<loop_context>";
  }
  return "<object>";
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

ObjRange* as_range(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_RANGE))
  {
    throw std::runtime_error("Expected range object");
  }
  return reinterpret_cast<ObjRange*>(value.as.obj);
}

ObjIterState* as_iter_state(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_ITER_STATE))
  {
    throw std::runtime_error("Expected iter_state object");
  }
  return reinterpret_cast<ObjIterState*>(value.as.obj);
}

ObjSet* as_set(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_SET))
  {
    throw std::runtime_error("Expected set object");
  }
  return reinterpret_cast<ObjSet*>(value.as.obj);
}

ObjTuple* as_tuple(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_TUPLE))
  {
    throw std::runtime_error("Expected tuple object");
  }
  return reinterpret_cast<ObjTuple*>(value.as.obj);
}

ObjStructDef* as_struct_def(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_STRUCT_DEF))
  {
    throw std::runtime_error("Expected struct_def object");
  }
  return reinterpret_cast<ObjStructDef*>(value.as.obj);
}

ObjStruct* as_struct(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_STRUCT))
  {
    throw std::runtime_error("Expected struct object");
  }
  return reinterpret_cast<ObjStruct*>(value.as.obj);
}

ObjImplTable* as_impl_table(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_IMPL_TABLE))
  {
    throw std::runtime_error("Expected impl_table object");
  }
  return reinterpret_cast<ObjImplTable*>(value.as.obj);
}

ObjTraitDef* as_trait_def(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_TRAIT_DEF))
  {
    throw std::runtime_error("Expected trait_def object");
  }
  return reinterpret_cast<ObjTraitDef*>(value.as.obj);
}

ObjSealedDef* as_sealed_def(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_SEALED_DEF))
  {
    throw std::runtime_error("Expected sealed_def object");
  }
  return reinterpret_cast<ObjSealedDef*>(value.as.obj);
}

ObjVariant* as_variant(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_VARIANT))
  {
    throw std::runtime_error("Expected variant object");
  }
  return reinterpret_cast<ObjVariant*>(value.as.obj);
}

ObjClawAgent* as_claw_agent(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_CLAW_AGENT))
  {
    throw std::runtime_error("Expected claw_agent object");
  }
  return reinterpret_cast<ObjClawAgent*>(value.as.obj);
}

ObjForgeAgent* as_forge_agent(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_FORGE_AGENT))
  {
    throw std::runtime_error("Expected forge_agent object");
  }
  return reinterpret_cast<ObjForgeAgent*>(value.as.obj);
}

ObjChannel* as_channel(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_CHANNEL))
  {
    throw std::runtime_error("Expected channel object");
  }
  return reinterpret_cast<ObjChannel*>(value.as.obj);
}

ObjWorkspace* as_workspace(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_WORKSPACE))
  {
    throw std::runtime_error("Expected workspace object");
  }
  return reinterpret_cast<ObjWorkspace*>(value.as.obj);
}

ObjLoopContext* as_loop_context(const Value& value)
{
  if (!is_obj_type(value, ObjType::OBJ_LOOP_CONTEXT))
  {
    throw std::runtime_error("Expected loop_context object");
  }
  return reinterpret_cast<ObjLoopContext*>(value.as.obj);
}
}  // namespace neamc::vm
