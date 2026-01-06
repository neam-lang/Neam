//
// Neam Virtual Machine - Interpreter implementation
//

#include "neamc/vm/vm.hpp"

#include <stdexcept>

namespace neamc::vm
{
Value VirtualMachine::pop()
{
  if (stack_.empty())
  {
    throw std::runtime_error("Stack underflow");
  }
  Value value = std::move(stack_.back());
  stack_.pop_back();
  return value;
}

Value& VirtualMachine::peek()
{
  if (stack_.empty())
  {
    throw std::runtime_error("Stack underflow");
  }
  return stack_.back();
}

bool VirtualMachine::is_truthy(const Value& value)
{
  if (value.is_nil())
  {
    return false;
  }
  if (value.is_bool())
  {
    return value.as_bool();
  }
  if (value.is_number())
  {
    return value.as_number() != 0.0;
  }
  return true;
}

uint16_t VirtualMachine::read_short(const std::vector<uint8_t>& code, std::size_t& ip)
{
  if (ip + 1 >= code.size())
  {
    throw std::runtime_error("Bytecode truncated while reading short");
  }
  const uint16_t low = code[ip++];
  const uint16_t high = code[ip++];
  return static_cast<uint16_t>(low | (high << 8));
}

Value VirtualMachine::binary_numeric_op(const Value& lhs, const Value& rhs, OpCode op)
{
  if (!lhs.is_number() || !rhs.is_number())
  {
    throw std::runtime_error("Numeric operation on non-number types");
  }

  const double a = lhs.as_number();
  const double b = rhs.as_number();
  double result = 0.0;

  switch (op)
  {
    case OpCode::OP_ADD:
      result = a + b;
      break;
    case OpCode::OP_SUB:
      result = a - b;
      break;
    case OpCode::OP_MUL:
      result = a * b;
      break;
    case OpCode::OP_DIV:
      if (b == 0.0)
      {
        throw std::runtime_error("Division by zero");
      }
      result = a / b;
      break;
    default:
      throw std::runtime_error("Unsupported numeric operation");
  }

  return Value::Number(result);
}

Value VirtualMachine::run(const Bytecode& chunk)
{
  const auto& code = chunk.code();
  const auto& constants = chunk.constants();
  std::size_t ip = 0;
  stack_.clear();
  events_.clear();

  while (ip < code.size())
  {
    const OpCode op = static_cast<OpCode>(code[ip++]);
    switch (op)
    {
      case OpCode::OP_CONST:
      {
        const auto index = read_short(code, ip);
        if (index >= constants.size())
        {
          throw std::runtime_error("Constant index out of range");
        }
        stack_.push_back(constants[index]);
        break;
      }
      case OpCode::OP_POP:
        (void)pop();
        break;
      case OpCode::OP_DUP:
        stack_.push_back(peek());
        break;
      case OpCode::OP_NEGATE:
      {
        Value value = pop();
        if (!value.is_number())
        {
          throw std::runtime_error("Negation on non-number type");
        }
        stack_.push_back(Value::Number(-value.as_number()));
        break;
      }
      case OpCode::OP_ADD:
      case OpCode::OP_SUB:
      case OpCode::OP_MUL:
      case OpCode::OP_DIV:
      {
        Value rhs = pop();
        Value lhs = pop();
        stack_.push_back(binary_numeric_op(lhs, rhs, op));
        break;
      }
      case OpCode::OP_JUMP:
      {
        const auto offset = read_short(code, ip);
        ip += offset;
        break;
      }
      case OpCode::OP_JUMP_IF_FALSE:
      {
        const auto offset = read_short(code, ip);
        if (!is_truthy(peek()))
        {
          ip += offset;
        }
        break;
      }
      case OpCode::OP_EMIT:
      {
        events_.push_back(pop());
        break;
      }
      default:
        throw std::runtime_error("Unknown opcode encountered");
    }
  }

  if (stack_.empty())
  {
    return Value::Nil();
  }
  return stack_.back();
}
}  // namespace neamc::vm
