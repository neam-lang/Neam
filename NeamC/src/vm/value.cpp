//
// Neam Virtual Machine - Value model implementation
//

#include "neamc/vm/value.hpp"

#include <stdexcept>

namespace neamc::vm
{
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
  return ValueType::Agent;
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
}  // namespace neamc::vm
