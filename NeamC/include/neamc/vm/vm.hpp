//
// Neam Virtual Machine - Interpreter
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm
{
class VirtualMachine
{
public:
  VirtualMachine();
  Value run(const Bytecode& chunk);
  void register_native(const std::string& name, NativeFn fn, std::size_t arity);
  const std::vector<Value>& stack() const { return stack_; }
  const std::vector<Value>& events() const { return events_; }

private:
  struct CallFrame
  {
    const Bytecode* chunk = nullptr;
    std::size_t ip = 0;
    std::size_t stack_start = 0;
  };

  Value pop();
  Value& peek();
  Value& peek_offset(std::size_t distance);
  static bool is_truthy(const Value& value);
  Value binary_numeric_op(const Value& lhs, const Value& rhs, OpCode op);
  uint16_t read_short(const std::vector<uint8_t>& code, std::size_t& ip);
  bool values_equal(const Value& lhs, const Value& rhs);

  std::vector<Value> stack_{};
  std::vector<Value> events_{};
  std::vector<CallFrame> frames_{};
  std::unordered_map<std::string, NativeFunction> natives_{};
};
}  // namespace neamc::vm
