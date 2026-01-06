//
// Neam Virtual Machine - Value model implementation
//

#include "neamc/vm/value.hpp"

#include <stdexcept>

namespace neamc::vm
{
Value Value::FunctionValue(Function fn)
{
  return Value{std::make_shared<vm::Function>(std::move(fn))};
}

Value Value::Native(NativeFunction fn)
{
  return Value{std::make_shared<vm::NativeFunction>(std::move(fn))};
}

ValueType Value::type() const
{
  if (is_nil())
  {
    return ValueType::Nil;
  }
  if (is_bool())
  {
    return ValueType::Bool;
  }
  if (is_number())
  {
    return ValueType::Number;
  }
  if (is_string())
  {
    return ValueType::String;
  }
  if (is_agent())
  {
    return ValueType::Agent;
  }
  if (is_function())
  {
    return ValueType::Function;
  }
  return ValueType::Native;
}

bool Value::as_bool() const
{
  if (!is_bool())
  {
    throw std::runtime_error("Expected bool value");
  }
  return std::get<bool>(storage_);
}

double Value::as_number() const
{
  if (!is_number())
  {
    throw std::runtime_error("Expected number value");
  }
  return std::get<double>(storage_);
}

const std::string& Value::as_string() const
{
  if (!is_string())
  {
    throw std::runtime_error("Expected string value");
  }
  return *std::get<StringRef>(storage_);
}

const AgentRef& Value::as_agent() const
{
  if (!is_agent())
  {
    throw std::runtime_error("Expected agent reference");
  }
  return *std::get<AgentHandle>(storage_);
}

const Function& Value::as_function() const
{
  if (!is_function())
  {
    throw std::runtime_error("Expected function value");
  }
  return *std::get<FunctionHandle>(storage_);
}

const NativeFunction& Value::as_native() const
{
  if (!is_native())
  {
    throw std::runtime_error("Expected native function value");
  }
  return *std::get<NativeHandle>(storage_);
}
}  // namespace neamc::vm
