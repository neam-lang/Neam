//
// Neam Virtual Machine - Interpreter implementation
//

#include "neamc/vm/vm.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace neamc::vm
{
VirtualMachine::VirtualMachine()
{
  current_vm = this;
  globals_.clear();
  interned_strings_.clear();
  register_core_natives(*this);
}

VirtualMachine::~VirtualMachine()
{
  if (current_vm == this)
  {
    current_vm = nullptr;
  }
  free_objects();
}

Value VirtualMachine::pop()
{
  if (stack_.empty())
  {
    throw std::runtime_error("Stack underflow");
  }
  Value value = stack_.back();
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
  if (lhs.type != rhs.type)
  {
    return false;
  }
  switch (lhs.type)
  {
    case ValueType::Nil:
      return true;
    case ValueType::Bool:
      return lhs.as_bool() == rhs.as_bool();
    case ValueType::Number:
      return lhs.as_number() == rhs.as_number();
    case ValueType::Obj:
    {
      if (is_obj_type(lhs, ObjType::OBJ_STRING) && is_obj_type(rhs, ObjType::OBJ_STRING))
      {
        auto* a = as_string(lhs);
        auto* b = as_string(rhs);
        if (a->length != b->length)
        {
          return false;
        }
        return std::memcmp(a->chars, b->chars, a->length) == 0;
      }
      return lhs.as_obj() == rhs.as_obj();
    }
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

Value VirtualMachine::concatenate(const Value& lhs, const Value& rhs)
{
  if (!is_obj_type(lhs, ObjType::OBJ_STRING) || !is_obj_type(rhs, ObjType::OBJ_STRING))
  {
    throw std::runtime_error("Concatenation expects string operands");
  }
  const auto* a = as_string(lhs);
  const auto* b = as_string(rhs);
  const std::size_t length = a->length + b->length;
  char* chars = ALLOCATE(char, length + 1);
  std::memcpy(chars, a->chars, a->length);
  std::memcpy(chars + a->length, b->chars, b->length);
  chars[length] = '\0';
  return Value::ObjVal(take_string(chars, length));
}

Value VirtualMachine::run(const Bytecode& chunk)
{
  stack_.clear();
  frames_.clear();
  frames_.push_back(CallFrame{&chunk, nullptr, 0, 0});
  const bool trace = std::getenv("NEAM_TRACE") != nullptr;

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
    if (trace)
    {
      std::cerr << "Executing op " << static_cast<int>(op) << " stack=" << stack_.size() << "\n";
    }
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
      case OpCode::OP_DEFINE_GLOBAL:
      {
        const auto name_index = read_short(code, frame.ip);
        auto* name = as_string(constants[name_index]);
        globals_.set(name, peek());
        (void)pop();
        break;
      }
      case OpCode::OP_GET_GLOBAL:
      {
        const auto name_index = read_short(code, frame.ip);
        auto* name = as_string(constants[name_index]);
        Value value;
        if (!globals_.get(name, &value))
        {
          throw std::runtime_error("Undefined global variable");
        }
        stack_.push_back(value);
        break;
      }
      case OpCode::OP_SET_GLOBAL:
      {
        const auto name_index = read_short(code, frame.ip);
        auto* name = as_string(constants[name_index]);
        if (globals_.set(name, peek()))
        {
          throw std::runtime_error("Undefined global variable");
        }
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
        if (op == OpCode::OP_ADD && is_obj_type(lhs, ObjType::OBJ_STRING) &&
            is_obj_type(rhs, ObjType::OBJ_STRING))
        {
          stack_.push_back(concatenate(lhs, rhs));
        }
        else
        {
          stack_.push_back(binary_numeric_op(lhs, rhs, op));
        }
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
      case OpCode::OP_CALL_NATIVE:
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
        if (is_obj_type(callee, ObjType::OBJ_FUNCTION))
        {
          auto* fn = as_function(callee);
          if (fn->arity != arg_count)
          {
            throw std::runtime_error("Argument count mismatch for function call");
          }
          stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(callee_index));
          frames_.push_back(CallFrame{&fn->chunk, fn, 0, callee_index});
        }
        else if (is_obj_type(callee, ObjType::OBJ_NATIVE))
        {
          auto* native = as_native(callee);
          if (native->arity != arg_count)
          {
            throw std::runtime_error("Argument count mismatch for native call");
          }
          if (!native->function)
          {
            throw std::runtime_error("Unbound native function");
          }
          std::vector<Value> args(arg_count);
          for (std::size_t i = 0; i < arg_count; ++i)
          {
            args[arg_count - 1 - i] = pop();
          }
          stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(callee_index));
          Value result = native->function(static_cast<int>(arg_count), args.data());
          stack_.push_back(std::move(result));
        }
        else
        {
          throw std::runtime_error("Attempted to call non-callable value");
        }
        break;
      }
      case OpCode::OP_RETURN:
      {
        Value result = pop();
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
      default:
        throw std::runtime_error("Unknown opcode encountered");
    }
  }

  return Value::Nil();
}

void VirtualMachine::define_native(const std::string& name, int arity, NativeFn function)
{
  auto* name_string = copy_string(name.c_str(), name.size());
  Value native_val = Value::Native(new_native(name_string, arity, function));
  globals_.set(name_string, native_val);
}
}  // namespace neamc::vm
