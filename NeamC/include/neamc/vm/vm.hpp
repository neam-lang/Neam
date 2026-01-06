//
// Neam Virtual Machine - Interpreter
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/memory.hpp"
#include "neamc/vm/native.hpp"
#include "neamc/vm/object.hpp"
#include "neamc/vm/table.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm
{
class VirtualMachine
{
public:
  struct CallFrame
  {
    const Bytecode* chunk = nullptr;
    ObjFunction* function = nullptr;
    std::size_t ip = 0;
    std::size_t stack_start = 0;
  };
  VirtualMachine();
  ~VirtualMachine();
  Value run(const Bytecode& chunk);
  void define_native(const std::string& name, int arity, NativeFn function);
  const std::vector<Value>& stack() const { return stack_; }
  const std::vector<CallFrame>& frames() const { return frames_; }
  Table& globals() { return globals_; }
  Table& strings() { return interned_strings_; }

private:
  Value pop();
  Value& peek();
  Value& peek_offset(std::size_t distance);
  static bool is_truthy(const Value& value);
  Value binary_numeric_op(const Value& lhs, const Value& rhs, OpCode op);
  uint16_t read_short(const std::vector<uint8_t>& code, std::size_t& ip);
  bool values_equal(const Value& lhs, const Value& rhs);
  Value concatenate(const Value& lhs, const Value& rhs);

  std::vector<Value> stack_{};
  std::vector<CallFrame> frames_{};
  Table globals_{};
  Table interned_strings_{};
};
}  // namespace neamc::vm
