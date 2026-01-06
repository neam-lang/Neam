//
// Neam Virtual Machine - Interpreter
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm
{
class VirtualMachine
{
public:
  Value run(const Bytecode& chunk);
  const std::vector<Value>& stack() const { return stack_; }
  const std::vector<Value>& events() const { return events_; }

private:
  Value pop();
  Value& peek();
  static bool is_truthy(const Value& value);
  Value binary_numeric_op(const Value& lhs, const Value& rhs, OpCode op);
  uint16_t read_short(const std::vector<uint8_t>& code, std::size_t& ip);

  std::vector<Value> stack_{};
  std::vector<Value> events_{};
};
}  // namespace neamc::vm
