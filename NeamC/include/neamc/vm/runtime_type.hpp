//
// Neam Virtual Machine - Runtime type info
//

#pragma once

#include <string>

#include "neamc/vm/value.hpp"

namespace neamc::vm
{
struct RuntimeTypeInfo
{
  std::string name;
  std::size_t size{0};
};

RuntimeTypeInfo runtime_type_of(const Value& value);
std::string typeof_name(const Value& value);
}  // namespace neamc::vm
