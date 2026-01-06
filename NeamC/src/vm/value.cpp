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

Value Value::String(const std::string& value)
{
  return Value::String(value.data(), value.size());
}

Value Value::FunctionValue(ObjFunction* function)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(function));
}

Value Value::Native(ObjNative* native)
{
  return Value::ObjVal(reinterpret_cast<Obj*>(native));
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
}  // namespace neamc::vm
