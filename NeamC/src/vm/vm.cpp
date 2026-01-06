//
// Neam Virtual Machine - Interpreter implementation
//

#include "neamc/vm/vm.hpp"

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <stdexcept>

namespace neamc::vm
{
namespace
{
Value print_native(const std::vector<Value>& args)
{
  for (const auto& arg : args)
  {
    switch (arg.type())
    {
      case ValueType::Nil:
        std::cout << "nil";
        break;
      case ValueType::Bool:
        std::cout << (arg.as_bool() ? "true" : "false");
        break;
      case ValueType::Number:
        std::cout << arg.as_number();
        break;
      case ValueType::String:
        std::cout << arg.as_string();
        break;
      case ValueType::Agent:
        std::cout << "<agent:" << arg.as_agent().name << ">";
        break;
      case ValueType::Function:
        std::cout << "<fn " << arg.as_function().name << ">";
        break;
      case ValueType::Native:
        std::cout << "<native " << arg.as_native().name << ">";
        break;
    }
    if (&arg != &args.back())
    {
      std::cout << " ";
    }
  }
  std::cout << std::endl;
  return Value::Nil();
}
}  // namespace

VirtualMachine::VirtualMachine()
{
  register_native("print", print_native, 1);
}

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

Value& VirtualMachine::peek_offset(std::size_t distance)
{
  if (distance >= stack_.size())
  {
    throw std::runtime_error("Stack underflow");
  }
  return stack_[stack_.size() - 1 - distance];
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

bool VirtualMachine::values_equal(const Value& lhs, const Value& rhs)
{
  if (lhs.type() != rhs.type())
  {
    return false;
  }
  switch (lhs.type())
  {
    case ValueType::Nil:
      return true;
    case ValueType::Bool:
      return lhs.as_bool() == rhs.as_bool();
    case ValueType::Number:
      return lhs.as_number() == rhs.as_number();
    case ValueType::String:
      return lhs.as_string() == rhs.as_string();
    case ValueType::Agent:
      return lhs.as_agent().name == rhs.as_agent().name;
    case ValueType::Function:
      return lhs.as_function().name == rhs.as_function().name;
    case ValueType::Native:
      return lhs.as_native().name == rhs.as_native().name;
  }
  return false;
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
  stack_.clear();
  events_.clear();
  frames_.clear();
  frames_.push_back(CallFrame{&chunk, 0, 0});

  while (!frames_.empty())
  {
    CallFrame& frame = frames_.back();
    const auto& code = frame.chunk->code();
    const auto& constants = frame.chunk->constants();

    if (frame.ip >= code.size())
    {
      throw std::runtime_error("Instruction pointer out of bounds");
    }

    const OpCode op = static_cast<OpCode>(code[frame.ip++]);
    switch (op)
    {
      case OpCode::OP_CONST:
      {
        const auto index = read_short(code, frame.ip);
        if (index >= constants.size())
        {
          throw std::runtime_error("Constant index out of range");
        }
        stack_.push_back(constants[index]);
        break;
      }
      case OpCode::OP_NIL:
        stack_.push_back(Value::Nil());
        break;
      case OpCode::OP_TRUE:
        stack_.push_back(Value::Bool(true));
        break;
      case OpCode::OP_FALSE:
        stack_.push_back(Value::Bool(false));
        break;
      case OpCode::OP_POP:
        (void)pop();
        break;
      case OpCode::OP_DUP:
        stack_.push_back(peek());
        break;
      case OpCode::OP_GET_LOCAL:
      {
        const auto slot = read_short(code, frame.ip);
        if (frame.stack_start + slot >= stack_.size())
        {
          throw std::runtime_error("Local index out of range");
        }
        stack_.push_back(stack_[frame.stack_start + slot]);
        break;
      }
      case OpCode::OP_SET_LOCAL:
      {
        const auto slot = read_short(code, frame.ip);
        if (frame.stack_start + slot >= stack_.size())
        {
          throw std::runtime_error("Local index out of range for set");
        }
        stack_[frame.stack_start + slot] = peek();
        break;
      }
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
      case OpCode::OP_NOT:
      {
        Value value = pop();
        stack_.push_back(Value::Bool(!is_truthy(value)));
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
      case OpCode::OP_EQUAL:
      {
        Value rhs = pop();
        Value lhs = pop();
        stack_.push_back(Value::Bool(values_equal(lhs, rhs)));
        break;
      }
      case OpCode::OP_GREATER:
      case OpCode::OP_LESS:
      {
        Value rhs = pop();
        Value lhs = pop();
        if (!lhs.is_number() || !rhs.is_number())
        {
          throw std::runtime_error("Comparison requires numbers");
        }
        bool result = false;
        if (op == OpCode::OP_GREATER)
        {
          result = lhs.as_number() > rhs.as_number();
        }
        else
        {
          result = lhs.as_number() < rhs.as_number();
        }
        stack_.push_back(Value::Bool(result));
        break;
      }
      case OpCode::OP_JUMP:
      {
        const auto offset = read_short(code, frame.ip);
        frame.ip += offset;
        break;
      }
      case OpCode::OP_JUMP_IF_FALSE:
      {
        const auto offset = read_short(code, frame.ip);
        if (!is_truthy(peek()))
        {
          frame.ip += offset;
        }
        break;
      }
      case OpCode::OP_LOOP:
      {
        const auto offset = read_short(code, frame.ip);
        frame.ip -= offset;
        break;
      }
      case OpCode::OP_CALL:
      {
        if (frame.ip >= code.size())
        {
          throw std::runtime_error("OP_CALL missing argument count");
        }
        const auto arg_count = code[frame.ip++];
        const auto callee_index = stack_.size() - 1 - arg_count;
        if (callee_index >= stack_.size())
        {
          throw std::runtime_error("Call stack underflow");
        }
        Value callee = stack_[callee_index];
        if (callee.is_function())
        {
          const auto& fn = callee.as_function();
          if (fn.arity != arg_count)
          {
            throw std::runtime_error("Argument count mismatch for function call");
          }
          stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(callee_index));
          frames_.push_back(CallFrame{fn.chunk.get(), 0, callee_index});
        }
        else if (callee.is_native())
        {
          const auto& native = callee.as_native();
          if (native.arity != arg_count)
          {
            throw std::runtime_error("Argument count mismatch for native call");
          }
          auto it = natives_.find(native.name);
          if (it == natives_.end())
          {
            throw std::runtime_error("Unknown native function: " + native.name);
          }
          std::vector<Value> args(arg_count);
          for (std::size_t i = 0; i < arg_count; ++i)
          {
            args[arg_count - 1 - i] = pop();
          }
          // remove callee
          stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(callee_index));
          Value result = it->second.callable(args);
          stack_.push_back(std::move(result));
        }
        else
        {
          throw std::runtime_error("Attempted to call non-callable value");
        }
        break;
      }
      case OpCode::OP_CALL_NATIVE:
      {
        if (frame.ip >= code.size())
        {
          throw std::runtime_error("OP_CALL_NATIVE missing argument count");
        }
        const auto arg_count = code[frame.ip++];
        const auto callee_index = stack_.size() - 1 - arg_count;
        if (callee_index >= stack_.size())
        {
          throw std::runtime_error("Call stack underflow");
        }
        Value callee = stack_[callee_index];
        if (!callee.is_native())
        {
          throw std::runtime_error("Attempted native call on non-native value");
        }
        const auto& native = callee.as_native();
        if (native.arity != arg_count)
        {
          throw std::runtime_error("Argument count mismatch for native call");
        }
        auto it = natives_.find(native.name);
        if (it == natives_.end())
        {
          throw std::runtime_error("Unknown native function: " + native.name);
        }
        std::vector<Value> args(arg_count);
        for (std::size_t i = 0; i < arg_count; ++i)
        {
          args[arg_count - 1 - i] = pop();
        }
        stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(callee_index));
        Value result = it->second.callable(args);
        stack_.push_back(std::move(result));
        break;
      }
      case OpCode::OP_RETURN:
      {
        Value result = pop();
        // Pop locals
        while (stack_.size() > frames_.back().stack_start)
        {
          stack_.pop_back();
        }
        frames_.pop_back();
        if (frames_.empty())
        {
          return result;
        }
        stack_.push_back(std::move(result));
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

  return Value::Nil();
}

void VirtualMachine::register_native(const std::string& name, NativeFn fn, std::size_t arity)
{
  natives_[name] = NativeFunction{name, arity, std::move(fn)};
}
}  // namespace neamc::vm
