//
// Neam Virtual Machine - Schema enforcement
//

#pragma once

#include <stdexcept>
#include <string>

namespace neamc::vm
{
class SchemaViolationError : public std::runtime_error
{
public:
  explicit SchemaViolationError(const std::string& message) : std::runtime_error(message) {}
};
}  // namespace neamc::vm
