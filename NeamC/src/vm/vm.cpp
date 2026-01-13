//
// Neam Virtual Machine - Interpreter implementation
//

#include "neamc/vm/vm.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

#include "neamc/llm/provider_factory.hpp"
#include "neamc/vm/async/future.hpp"
#include "neamc/vm/knowledge.hpp"
#include "neamc/vm/schema.hpp"

namespace neamc::vm
{
namespace
{
std::string to_std_string(const Value& value)
{
  if (!value.is_string())
  {
    throw std::runtime_error("Expected string value");
  }
  auto* str = as_string(value);
  return std::string(str->chars, str->length);
}

std::string to_std_string(const ObjString* value)
{
  if (!value)
  {
    return {};
  }
  return std::string(value->chars, value->length);
}

std::string value_to_string(const Value& value)
{
  if (value.is_string())
  {
    auto* str = as_string(value);
    return std::string(str->chars, str->length);
  }
  if (value.is_number())
  {
    std::ostringstream out;
    out << value.as_number();
    return out.str();
  }
  if (value.is_bool())
  {
    return value.as_bool() ? "true" : "false";
  }
  if (value.is_nil())
  {
    return "nil";
  }
  return "<object>";
}

std::vector<knowledge::Source> parse_sources_list(const Value& value)
{
  if (!value.is_list())
  {
    throw std::runtime_error("Knowledge sources must be list");
  }
  std::vector<knowledge::Source> sources;
  auto* list = as_list(value);
  for (const auto& item : list->items)
  {
    if (!item.is_map())
    {
      throw std::runtime_error("Knowledge source must be map");
    }
    auto* map = as_map(item);
    auto type_it = map->entries.find("type");
    auto path_it = map->entries.find("path");
    if (type_it == map->entries.end() || path_it == map->entries.end())
    {
      throw std::runtime_error("Knowledge source missing type or path");
    }
    sources.push_back(
        knowledge::Source{to_std_string(type_it->second), to_std_string(path_it->second)});
  }
  return sources;
}

std::string format_rag_context(const std::vector<knowledge::SearchResult>& results)
{
  if (results.empty())
  {
    return {};
  }
  std::ostringstream out;
  out << "Retrieved context:\n";
  for (const auto& result : results)
  {
    out << "- " << result.chunk.text << "\n";
  }
  return out.str();
}

std::string resolve_env_config(const VirtualMachine& vm, const std::string& key,
                               const char* env_var, const std::string& fallback)
{
  if (vm.env())
  {
    const auto it = vm.env()->config.find(key);
    if (it != vm.env()->config.end() && !it->second.empty())
    {
      return it->second;
    }
  }
  if (env_var)
  {
    if (const char* env_value = std::getenv(env_var))
    {
      if (*env_value != '\0')
      {
        return std::string(env_value);
      }
    }
  }
  return fallback;
}

std::string escape_json_string(const std::string& input)
{
  std::string output;
  output.reserve(input.size() + 8);
  for (const char c : input)
  {
    switch (c)
    {
      case '"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        output += c;
        break;
    }
  }
  return output;
}

bool parse_json_string(const std::string& input, std::size_t& pos, std::string& out)
{
  if (pos >= input.size() || input[pos] != '"')
  {
    return false;
  }
  ++pos;
  std::ostringstream buffer;
  while (pos < input.size())
  {
    const char c = input[pos++];
    if (c == '"')
    {
      out = buffer.str();
      return true;
    }
    if (c == '\\' && pos < input.size())
    {
      const char esc = input[pos++];
      switch (esc)
      {
        case '"':
          buffer << '"';
          break;
        case '\\':
          buffer << '\\';
          break;
        case 'n':
          buffer << '\n';
          break;
        case 'r':
          buffer << '\r';
          break;
        case 't':
          buffer << '\t';
          break;
        default:
          buffer << esc;
          break;
      }
      continue;
    }
    buffer << c;
  }
  return false;
}

void skip_ws(const std::string& input, std::size_t& pos)
{
  while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos])))
  {
    ++pos;
  }
}

bool parse_history_json(const std::string& input, std::vector<ObjContext::Message>& history)
{
  std::size_t pos = 0;
  skip_ws(input, pos);
  if (pos >= input.size() || input[pos] != '[')
  {
    return false;
  }
  ++pos;
  skip_ws(input, pos);
  history.clear();
  if (pos < input.size() && input[pos] == ']')
  {
    ++pos;
    return true;
  }
  while (pos < input.size())
  {
    skip_ws(input, pos);
    if (pos >= input.size() || input[pos] != '{')
    {
      return false;
    }
    ++pos;
    skip_ws(input, pos);
    ObjContext::Message message;
    bool have_role = false;
    bool have_content = false;
    while (pos < input.size())
    {
      skip_ws(input, pos);
      std::string key;
      if (!parse_json_string(input, pos, key))
      {
        return false;
      }
      skip_ws(input, pos);
      if (pos >= input.size() || input[pos] != ':')
      {
        return false;
      }
      ++pos;
      skip_ws(input, pos);
      std::string value;
      if (!parse_json_string(input, pos, value))
      {
        return false;
      }
      if (key == "role")
      {
        message.role = std::move(value);
        have_role = true;
      }
      else if (key == "content")
      {
        message.content = std::move(value);
        have_content = true;
      }
      skip_ws(input, pos);
      if (pos < input.size() && input[pos] == ',')
      {
        ++pos;
        continue;
      }
      if (pos < input.size() && input[pos] == '}')
      {
        ++pos;
        break;
      }
      return false;
    }
    if (!have_role || !have_content)
    {
      return false;
    }
    history.push_back(std::move(message));
    skip_ws(input, pos);
    if (pos < input.size() && input[pos] == ',')
    {
      ++pos;
      continue;
    }
    if (pos < input.size() && input[pos] == ']')
    {
      ++pos;
      return true;
    }
  }
  return false;
}

nlohmann::json value_to_json(const Value& value)
{
  if (value.is_nil())
  {
    return nullptr;
  }
  if (value.is_bool())
  {
    return value.as_bool();
  }
  if (value.is_number())
  {
    return value.as_number();
  }
  if (value.is_string())
  {
    return to_std_string(value);
  }
  if (value.is_list())
  {
    nlohmann::json result = nlohmann::json::array();
    auto* list = as_list(value);
    for (const auto& item : list->items)
    {
      result.push_back(value_to_json(item));
    }
    return result;
  }
  if (value.is_map())
  {
    nlohmann::json result = nlohmann::json::object();
    auto* map = as_map(value);
    for (const auto& entry : map->entries)
    {
      result[entry.first] = value_to_json(entry.second);
    }
    return result;
  }
  if (value.is_skill())
  {
    auto* skill = as_skill(value);
    return std::string("<skill ") + to_std_string(skill->name) + ">";
  }
  if (value.is_agent())
  {
    auto* agent = as_agent(value);
    return std::string("<agent ") + to_std_string(agent->name) + ">";
  }
  return "<object>";
}

bool schema_is_sensitive(const Value& schema_value)
{
  if (!schema_value.is_map())
  {
    return false;
  }
  auto* map = as_map(schema_value);
  auto it = map->entries.find("sensitive");
  if (it == map->entries.end())
  {
    return false;
  }
  return it->second.is_bool() && it->second.as_bool();
}

nlohmann::json build_skill_schema(const ObjSkill* skill)
{
  nlohmann::json schema = nlohmann::json::object();
  schema["type"] = "object";
  nlohmann::json properties = nlohmann::json::object();
  nlohmann::json required = nlohmann::json::array();
  for (const auto& name : skill->param_names)
  {
    auto it = skill->params->entries.find(name);
    if (it == skill->params->entries.end())
    {
      continue;
    }
    properties[name] = value_to_json(it->second);
    required.push_back(name);
  }
  schema["properties"] = std::move(properties);
  schema["required"] = std::move(required);
  schema["additionalProperties"] = false;
  return schema;
}

nlohmann::json build_args_object(const ObjSkill* skill, const std::vector<Value>& args)
{
  nlohmann::json obj = nlohmann::json::object();
  const std::size_t count = skill->param_names.size();
  for (std::size_t i = 0; i < count && i < args.size(); ++i)
  {
    obj[skill->param_names[i]] = value_to_json(args[i]);
  }
  return obj;
}

nlohmann::json build_redacted_args(const ObjSkill* skill, const std::vector<Value>& args)
{
  nlohmann::json obj = nlohmann::json::object();
  const std::size_t count = skill->param_names.size();
  for (std::size_t i = 0; i < count && i < args.size(); ++i)
  {
    const auto& name = skill->param_names[i];
    auto schema_it = skill->params->entries.find(name);
    const bool sensitive =
        schema_it != skill->params->entries.end() && schema_is_sensitive(schema_it->second);
    if (sensitive)
    {
      obj[name] = "***";
    }
    else
    {
      obj[name] = value_to_json(args[i]);
    }
  }
  return obj;
}

void validate_skill_args(const ObjSkill* skill, const std::vector<Value>& args)
{
  if (skill->param_names.size() != args.size())
  {
    throw SchemaViolationError("Argument count does not match skill schema");
  }
  const auto schema = build_skill_schema(skill);
  const auto instance = build_args_object(skill, args);
  nlohmann::json_schema::json_validator validator;
  validator.set_root_schema(schema);
  struct Handler : nlohmann::json_schema::error_handler
  {
    bool has_error = false;
    std::string message;
    void error(const nlohmann::json::json_pointer&, const nlohmann::json&,
               const std::string& msg) override
    {
      has_error = true;
      message = msg;
    }
  } handler;
  validator.validate(instance, handler);
  if (handler.has_error)
  {
    throw SchemaViolationError(handler.message.empty() ? "Schema validation failed"
                                                       : handler.message);
  }
}

std::string opcode_name(OpCode op)
{
  switch (op)
  {
    case OpCode::OP_CONST:
      return "OP_CONST";
    case OpCode::OP_NIL:
      return "OP_NIL";
    case OpCode::OP_TRUE:
      return "OP_TRUE";
    case OpCode::OP_FALSE:
      return "OP_FALSE";
    case OpCode::OP_POP:
      return "OP_POP";
    case OpCode::OP_DUP:
      return "OP_DUP";
    case OpCode::OP_GET_LOCAL:
      return "OP_GET_LOCAL";
    case OpCode::OP_SET_LOCAL:
      return "OP_SET_LOCAL";
    case OpCode::OP_DEFINE_GLOBAL:
      return "OP_DEFINE_GLOBAL";
    case OpCode::OP_GET_GLOBAL:
      return "OP_GET_GLOBAL";
    case OpCode::OP_SET_GLOBAL:
      return "OP_SET_GLOBAL";
    case OpCode::OP_NEGATE:
      return "OP_NEGATE";
    case OpCode::OP_NOT:
      return "OP_NOT";
    case OpCode::OP_ADD:
      return "OP_ADD";
    case OpCode::OP_SUB:
      return "OP_SUB";
    case OpCode::OP_MUL:
      return "OP_MUL";
    case OpCode::OP_DIV:
      return "OP_DIV";
    case OpCode::OP_EQUAL:
      return "OP_EQUAL";
    case OpCode::OP_GREATER:
      return "OP_GREATER";
    case OpCode::OP_LESS:
      return "OP_LESS";
    case OpCode::OP_JUMP:
      return "OP_JUMP";
    case OpCode::OP_JUMP_IF_FALSE:
      return "OP_JUMP_IF_FALSE";
    case OpCode::OP_LOOP:
      return "OP_LOOP";
    case OpCode::OP_CALL:
      return "OP_CALL";
    case OpCode::OP_CALL_NATIVE:
      return "OP_CALL_NATIVE";
    case OpCode::OP_GET_PROPERTY:
      return "OP_GET_PROPERTY";
    case OpCode::OP_GET_INDEX:
      return "OP_GET_INDEX";
    case OpCode::OP_INVOKE:
      return "OP_INVOKE";
    case OpCode::OP_AWAIT:
      return "OP_AWAIT";
    case OpCode::OP_RETURN:
      return "OP_RETURN";
    case OpCode::OP_EMIT:
      return "OP_EMIT";
    case OpCode::OP_TRACE:
      return "OP_TRACE";
    case OpCode::OP_BUILD_LIST:
      return "OP_BUILD_LIST";
    case OpCode::OP_BUILD_MAP:
      return "OP_BUILD_MAP";
    case OpCode::OP_DEFINE_SKILL:
      return "OP_DEFINE_SKILL";
    case OpCode::OP_DEFINE_KNOWLEDGE:
      return "OP_DEFINE_KNOWLEDGE";
    case OpCode::OP_DEFINE_AGENT:
      return "OP_DEFINE_AGENT";
  }
  return "OP_UNKNOWN";
}
}  // namespace

VirtualMachine::VirtualMachine()
{
  current_vm = this;
  input_ = &std::cin;
  output_ = &std::cout;
  globals_.clear();
  interned_strings_.clear();
  register_core_natives(*this);

  auto* env = new_env();
  auto* std_map = new_map({{"env", Value::Env(env)}});
  auto* std_name = copy_string("std", 3);
  globals_.set(std_name, Value::Map(std_map));
  env_ = env;
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
  return run_internal(chunk);
}

Value VirtualMachine::run(const Bytecode& chunk, std::istream* input, std::ostream* output)
{
  std::istream* previous_input = input_;
  std::ostream* previous_output = output_;
  if (input)
  {
    input_ = input;
  }
  if (output)
  {
    output_ = output;
  }
  try
  {
    Value result = run_internal(chunk);
    input_ = previous_input;
    output_ = previous_output;
    return result;
  }
  catch (...)
  {
    input_ = previous_input;
    output_ = previous_output;
    throw;
  }
}

void VirtualMachine::set_io(std::istream* input, std::ostream* output)
{
  if (input)
  {
    input_ = input;
  }
  if (output)
  {
    output_ = output;
  }
}

std::istream& VirtualMachine::input_stream() const
{
  return *input_;
}

std::ostream& VirtualMachine::output_stream() const
{
  return *output_;
}

Value VirtualMachine::run_internal(const Bytecode& chunk)
{
  stack_.clear();
  frames_.clear();
  emitted_.clear();
  trace_logger_.start_run();
  trace_logger_.log_start();
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
    trace_logger_.log_step(opcode_name(op), frame.ip - 1, stack_.size());
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
          if (native->arity >= 0 && native->arity != arg_count)
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
          Value result =
              native->function(*this, static_cast<int>(arg_count), args.data());
          stack_.push_back(std::move(result));
        }
        else if (is_obj_type(callee, ObjType::OBJ_SKILL))
        {
          auto* skill = as_skill(callee);
          const std::string skill_name(skill->name->chars, skill->name->length);
          std::vector<Value> args(arg_count);
          for (std::size_t i = 0; i < arg_count; ++i)
          {
            args[arg_count - 1 - i] = stack_[stack_.size() - 1 - i];
          }
          trace_logger_.log_tool_call("vm", skill_name, build_redacted_args(skill, args));
          emit_debug_event(DebugEventType::BeforeToolExecution, "skill:" + skill_name,
                           frame.ip - 1, {});
          if (!allowed_skills_.empty() && allowed_skills_.count(skill_name) == 0)
          {
            throw std::runtime_error("Skill blocked by env policy");
          }
          if (!skill->impl)
          {
            throw std::runtime_error("Skill has no implementation");
          }
          if (skill->impl->arity != arg_count)
          {
            throw std::runtime_error("Argument count mismatch for skill call");
          }
          validate_skill_args(skill, args);
          stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(callee_index));
          CallFrame tool_frame{&skill->impl->chunk, skill->impl, 0, callee_index};
          tool_frame.is_tool = true;
          tool_frame.tool_name = skill_name;
          frames_.push_back(std::move(tool_frame));
        }
        else
        {
          throw std::runtime_error("Attempted to call non-callable value");
        }
        break;
      }
      case OpCode::OP_GET_PROPERTY:
      {
        const auto name_index = read_short(code, frame.ip);
        if (name_index >= constants.size())
        {
          throw std::runtime_error("Property name constant out of range");
        }
        auto* name = as_string(constants[name_index]);
        const std::string key(name->chars, name->length);
        Value receiver = pop();
        if (is_obj_type(receiver, ObjType::OBJ_MAP))
        {
          auto* map = as_map(receiver);
          auto it = map->entries.find(key);
          if (it == map->entries.end())
          {
            throw std::runtime_error("Missing map property");
          }
          stack_.push_back(it->second);
        }
        else if (is_obj_type(receiver, ObjType::OBJ_AGENT))
        {
          auto* agent = as_agent(receiver);
          if (key == "context")
          {
            stack_.push_back(Value::Context(agent->context));
          }
          else if (key == "provider" && agent->provider)
          {
            stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent->provider)));
          }
          else if (key == "model" && agent->model)
          {
            stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent->model)));
          }
          else if (key == "name" && agent->name)
          {
            stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent->name)));
          }
          else if (key == "skills" && agent->skills)
          {
            stack_.push_back(Value::List(agent->skills));
          }
          else if (key == "connected_knowledge" && agent->connected_knowledge)
          {
            stack_.push_back(Value::List(agent->connected_knowledge));
          }
          else
          {
            throw std::runtime_error("Unknown agent property");
          }
        }
        else
        {
          throw std::runtime_error("Property access on non-object");
        }
        break;
      }
      case OpCode::OP_GET_INDEX:
      {
        Value index_value = pop();
        Value base_value = pop();
        if (is_obj_type(base_value, ObjType::OBJ_LIST))
        {
          if (!index_value.is_number())
          {
            throw std::runtime_error("List index must be a number");
          }
          const double raw_index = index_value.as_number();
          const double floored = std::floor(raw_index);
          if (raw_index < 0 || floored != raw_index)
          {
            throw std::runtime_error("List index must be a non-negative integer");
          }
          const std::size_t index = static_cast<std::size_t>(floored);
          auto* list = as_list(base_value);
          if (index >= list->items.size())
          {
            throw std::runtime_error("List index out of range");
          }
          stack_.push_back(list->items[index]);
        }
        else if (is_obj_type(base_value, ObjType::OBJ_MAP))
        {
          const std::string key = to_std_string(index_value);
          auto* map = as_map(base_value);
          auto it = map->entries.find(key);
          if (it == map->entries.end())
          {
            throw std::runtime_error("Missing map key");
          }
          stack_.push_back(it->second);
        }
        else
        {
          throw std::runtime_error("Indexing is only supported on lists and maps");
        }
        break;
      }
      case OpCode::OP_INVOKE:
      {
        const auto name_index = read_short(code, frame.ip);
        if (frame.ip >= code.size())
        {
          throw std::runtime_error("OP_INVOKE missing argument count");
        }
        const auto arg_count = code[frame.ip++];
        if (name_index >= constants.size())
        {
          throw std::runtime_error("Method name constant out of range");
        }
        auto* name = as_string(constants[name_index]);
        const std::string method(name->chars, name->length);
        std::vector<Value> args(arg_count);
        for (std::size_t i = 0; i < arg_count; ++i)
        {
          args[arg_count - 1 - i] = pop();
        }
        Value receiver = pop();
        auto call_native = [this](const Value& callable, int count,
                                  Value* call_args) -> Value {
          if (!is_obj_type(callable, ObjType::OBJ_NATIVE))
          {
            throw std::runtime_error("Callable must be native function for list operation");
          }
          auto* native = as_native(callable);
          if (native->arity >= 0 && native->arity != count)
          {
            throw std::runtime_error("Argument count mismatch for native call");
          }
          if (!native->function)
          {
            throw std::runtime_error("Unbound native function");
          }
          return native->function(*this, count, call_args);
        };
        if (is_obj_type(receiver, ObjType::OBJ_AGENT))
        {
          auto* agent = as_agent(receiver);
          if (method == "ask")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("Agent.ask expects 1 argument");
            }
            std::string query = to_std_string(args[0]);
            if (!agent->context)
            {
              agent->context = new_context();
            }
            agent->context->history.push_back({"user", query});
            llm::ProviderConfig config;
            config.model = to_std_string(agent->model);
            config.endpoint = to_std_string(agent->endpoint);
            config.api_key = to_std_string(agent->api_key_env);
            config.temperature = agent->temperature;
            config.default_host =
                resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST",
                                   "http://localhost:11434");

            if (!config.api_key.empty())
            {
              if (const char* env_value = std::getenv(config.api_key.c_str()))
              {
                config.api_key = env_value;
              }
              else
              {
                config.api_key.clear();
              }
            }

            const std::string provider_name = to_std_string(agent->provider);
            auto provider = llm::create_provider(provider_name, config);

            std::vector<llm::Message> messages;
            if (agent->system)
            {
              messages.push_back({"system", to_std_string(agent->system)});
            }
            if (agent->connected_knowledge && !agent->connected_knowledge->items.empty())
            {
              std::string combined_context;
              for (const auto& kb_value : agent->connected_knowledge->items)
              {
                const std::string kb_name = to_std_string(kb_value);
                auto kb_it = knowledge_bases_.find(kb_name);
                if (kb_it == knowledge_bases_.end())
                {
                  continue;
                }
                auto* knowledge = kb_it->second;
                output_stream() << "[RAG] Searching KB '" << kb_name << "' for: '" << query
                                << "'\n";
                auto embedding =
                    knowledge::embed_text(query, knowledge->store.dimensions());
                auto results = knowledge->store.search(embedding, 3);
                if (!results.empty())
                {
                  output_stream() << "[RAG] Found context: \"" << results.front().chunk.text
                                  << "\"\n";
                }
                const auto context = format_rag_context(results);
                if (!context.empty())
                {
                  combined_context += context;
                }
              }
              if (!combined_context.empty())
              {
                messages.push_back({"system", combined_context});
              }
            }
            for (const auto& message : agent->context->history)
            {
              messages.push_back({message.role, message.content});
            }
            std::ostringstream preview;
            preview << "provider=" << provider_name << "\n";
            preview << "model=" << config.model << "\n";
            for (const auto& message : messages)
            {
              preview << "[" << message.role << "] " << message.content << "\n";
            }
            emit_debug_event(DebugEventType::BeforeAgentAsk, "agent.ask", frame.ip - 1,
                             preview.str());
            const auto response_text = provider->chat(messages);
            agent->context->history.push_back({"assistant", response_text});
            trace_logger_.log_llm_output(to_std_string(agent->name), response_text);
            stack_.push_back(Value::String(response_text.c_str(), response_text.size()));
          }
          else if (method == "reset")
          {
            if (agent->context)
            {
              agent->context->history.clear();
            }
            stack_.push_back(Value::Nil());
          }
          else
          {
            throw std::runtime_error("Unknown agent method");
          }
        }
        else if (is_obj_type(receiver, ObjType::OBJ_CONTEXT))
        {
          auto* context = as_context(receiver);
          if (method == "add_system")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("context.add_system expects 1 argument");
            }
            std::string text = to_std_string(args[0]);
            context->history.push_back({"system", std::move(text)});
            stack_.push_back(Value::Nil());
          }
          else if (method == "history")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("context.history expects no arguments");
            }
            std::vector<Value> items;
            items.reserve(context->history.size());
            for (const auto& message : context->history)
            {
              std::unordered_map<std::string, Value> entry;
              entry.emplace("role",
                            Value::String(message.role.c_str(), message.role.size()));
              entry.emplace(
                  "content",
                  Value::String(message.content.c_str(), message.content.size()));
              items.push_back(Value::Map(new_map(std::move(entry))));
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "save")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("context.save expects 1 argument");
            }
            const std::string path = to_std_string(args[0]);
            std::ofstream out(path, std::ios::binary);
            if (!out)
            {
              stack_.push_back(Value::Bool(false));
              break;
            }
            out << "[";
            for (std::size_t i = 0; i < context->history.size(); ++i)
            {
              const auto& message = context->history[i];
              out << "{\"role\":\"" << escape_json_string(message.role)
                  << "\",\"content\":\"" << escape_json_string(message.content) << "\"}";
              if (i + 1 < context->history.size())
              {
                out << ",";
              }
            }
            out << "]";
            stack_.push_back(Value::Bool(true));
          }
          else if (method == "load")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("context.load expects 1 argument");
            }
            const std::string path = to_std_string(args[0]);
            std::ifstream in(path, std::ios::binary);
            if (!in)
            {
              stack_.push_back(Value::Bool(false));
              break;
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            std::vector<ObjContext::Message> parsed;
            if (!parse_history_json(buffer.str(), parsed))
            {
              stack_.push_back(Value::Bool(false));
              break;
            }
            context->history = std::move(parsed);
            stack_.push_back(Value::Bool(true));
          }
          else
          {
            throw std::runtime_error("Unknown context method");
          }
        }
        else if (is_obj_type(receiver, ObjType::OBJ_ENV))
        {
          auto* env = as_env(receiver);
          if (method == "set_allowed_skills")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("env.set_allowed_skills expects 1 argument");
            }
            if (!args[0].is_list())
            {
              throw std::runtime_error("env.set_allowed_skills expects a list");
            }
            env->allowed_skills.clear();
            allowed_skills_.clear();
            auto* list = as_list(args[0]);
            for (const auto& item : list->items)
            {
              const std::string skill_name = to_std_string(item);
              env->allowed_skills.insert(skill_name);
              allowed_skills_.insert(skill_name);
            }
            stack_.push_back(Value::Nil());
          }
          else if (method == "config")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("env.config expects 1 argument");
            }
            if (!args[0].is_map())
            {
              throw std::runtime_error("env.config expects a map");
            }
            auto* map = as_map(args[0]);
            for (const auto& entry : map->entries)
            {
              if (!entry.second.is_string())
              {
                throw std::runtime_error("env.config values must be strings");
              }
              auto* value = as_string(entry.second);
              env->config[entry.first] =
                  std::string(value->chars, value->length);
            }
            stack_.push_back(Value::Nil());
          }
          else
          {
            throw std::runtime_error("Unknown env method");
          }
        }
        else if (is_obj_type(receiver, ObjType::OBJ_LIST))
        {
          auto* list = as_list(receiver);
          if (method == "push")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("list.push expects 1 argument");
            }
            list->items.push_back(args[0]);
            stack_.push_back(Value::List(list));
          }
          else if (method == "pop")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("list.pop expects no arguments");
            }
            if (list->items.empty())
            {
              stack_.push_back(Value::Nil());
            }
            else
            {
              Value value = list->items.back();
              list->items.pop_back();
              stack_.push_back(std::move(value));
            }
          }
          else if (method == "map")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("list.map expects 1 argument");
            }
            std::vector<Value> items;
            items.reserve(list->items.size());
            for (const auto& item : list->items)
            {
              Value call_args[] = {item};
              items.push_back(call_native(args[0], 1, call_args));
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "filter")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("list.filter expects 1 argument");
            }
            std::vector<Value> items;
            for (const auto& item : list->items)
            {
              Value call_args[] = {item};
              Value result = call_native(args[0], 1, call_args);
              if (is_truthy(result))
              {
                items.push_back(item);
              }
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "fold")
          {
            if (arg_count != 2)
            {
              throw std::runtime_error("list.fold expects 2 arguments");
            }
            Value acc = args[0];
            for (const auto& item : list->items)
            {
              Value call_args[] = {acc, item};
              acc = call_native(args[1], 2, call_args);
            }
            stack_.push_back(std::move(acc));
          }
          else
          {
            throw std::runtime_error("Unknown list method");
          }
        }
        else if (is_obj_type(receiver, ObjType::OBJ_MAP))
        {
          auto* map = as_map(receiver);
          if (method == "get")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("map.get expects 1 argument");
            }
            std::string key = to_std_string(args[0]);
            auto it = map->entries.find(key);
            if (it == map->entries.end())
            {
              stack_.push_back(Value::Nil());
            }
            else
            {
              stack_.push_back(it->second);
            }
          }
          else if (method == "insert")
          {
            if (arg_count != 2)
            {
              throw std::runtime_error("map.insert expects 2 arguments");
            }
            std::string key = to_std_string(args[0]);
            map->entries[key] = args[1];
            stack_.push_back(Value::Map(map));
          }
          else if (method == "keys")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("map.keys expects no arguments");
            }
            std::vector<Value> items;
            items.reserve(map->entries.size());
            for (const auto& entry : map->entries)
            {
              items.push_back(Value::String(entry.first.c_str(), entry.first.size()));
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else
          {
            throw std::runtime_error("Unknown map method");
          }
        }
        else if (is_obj_type(receiver, ObjType::OBJ_STRING))
        {
          auto* str = as_string(receiver);
          const std::string base(str->chars, str->length);
          if (method == "split")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("string.split expects 1 argument");
            }
            const std::string delimiter = to_std_string(args[0]);
            std::vector<Value> parts;
            if (delimiter.empty())
            {
              parts.reserve(base.size());
              for (char c : base)
              {
                parts.push_back(Value::String(&c, 1));
              }
            }
            else
            {
              std::size_t start = 0;
              while (start <= base.size())
              {
                const auto pos = base.find(delimiter, start);
                const auto len = (pos == std::string::npos) ? base.size() - start : pos - start;
                const std::string piece = base.substr(start, len);
                parts.push_back(Value::String(piece.c_str(), piece.size()));
                if (pos == std::string::npos)
                {
                  break;
                }
                start = pos + delimiter.size();
              }
            }
            stack_.push_back(Value::List(new_list(std::move(parts))));
          }
          else if (method == "join")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("string.join expects 1 argument");
            }
            if (!args[0].is_list())
            {
              throw std::runtime_error("string.join expects list argument");
            }
            auto* list = as_list(args[0]);
            std::ostringstream out;
            for (std::size_t i = 0; i < list->items.size(); ++i)
            {
              out << value_to_string(list->items[i]);
              if (i + 1 < list->items.size())
              {
                out << base;
              }
            }
            const std::string joined = out.str();
            stack_.push_back(Value::String(joined.c_str(), joined.size()));
          }
          else if (method == "replace")
          {
            if (arg_count != 2)
            {
              throw std::runtime_error("string.replace expects 2 arguments");
            }
            const std::string pattern = to_std_string(args[0]);
            const std::string replacement = to_std_string(args[1]);
            try
            {
              std::regex re(pattern);
              const std::string replaced = std::regex_replace(base, re, replacement);
              stack_.push_back(Value::String(replaced.c_str(), replaced.size()));
            }
            catch (const std::regex_error& ex)
            {
              throw std::runtime_error(std::string("Invalid regex: ") + ex.what());
            }
          }
          else
          {
            throw std::runtime_error("Unknown string method");
          }
        }
        else
        {
          throw std::runtime_error("Invoke on non-object");
        }
        break;
      }
      case OpCode::OP_AWAIT:
      {
        Value future_value = pop();
        if (!future_value.is_future())
        {
          throw std::runtime_error("await expects Future value");
        }
        auto* future = as_future(future_value);
        if (!future->future)
        {
          throw std::runtime_error("Future has no state");
        }
        Value result = future->future->wait();
        stack_.push_back(std::move(result));
        break;
      }
      case OpCode::OP_BUILD_LIST:
      {
        if (frame.ip >= code.size())
        {
          throw std::runtime_error("OP_BUILD_LIST missing count");
        }
        const auto count = code[frame.ip++];
        std::vector<Value> items(count);
        for (std::size_t i = 0; i < count; ++i)
        {
          items[count - 1 - i] = pop();
        }
        stack_.push_back(Value::List(new_list(std::move(items))));
        break;
      }
      case OpCode::OP_BUILD_MAP:
      {
        if (frame.ip >= code.size())
        {
          throw std::runtime_error("OP_BUILD_MAP missing count");
        }
        const auto count = code[frame.ip++];
        std::unordered_map<std::string, Value> entries;
        for (std::size_t i = 0; i < count; ++i)
        {
          Value value = pop();
          Value key_value = pop();
          const std::string key = to_std_string(key_value);
          entries.emplace(key, std::move(value));
        }
        stack_.push_back(Value::Map(new_map(std::move(entries))));
        break;
      }
      case OpCode::OP_DEFINE_SKILL:
      {
        Value impl_value = pop();
        Value params_value = pop();
        Value param_order_value = pop();
        Value description_value = pop();
        Value name_value = pop();
        auto* name = as_string(name_value);
        auto* description = as_string(description_value);
        if (!is_obj_type(impl_value, ObjType::OBJ_FUNCTION))
        {
          throw std::runtime_error("Skill impl must be function");
        }
        if (!is_obj_type(params_value, ObjType::OBJ_MAP))
        {
          throw std::runtime_error("Skill params must be map");
        }
        if (!is_obj_type(param_order_value, ObjType::OBJ_LIST))
        {
          throw std::runtime_error("Skill param order must be list");
        }
        auto* params = as_map(params_value);
        auto* param_order = as_list(param_order_value);
        std::vector<std::string> param_names;
        param_names.reserve(param_order->items.size());
        for (const auto& item : param_order->items)
        {
          param_names.push_back(to_std_string(item));
        }
        auto* impl = as_function(impl_value);
        auto* skill = new_skill(name, description, params, std::move(param_names), impl);
        globals_.set(name, Value::Skill(skill));
        break;
      }
      case OpCode::OP_DEFINE_KNOWLEDGE:
      {
        Value sources_value = pop();
        Value chunk_overlap_value = pop();
        Value chunk_size_value = pop();
        Value embedding_model_value = pop();
        Value vector_store_value = pop();
        Value name_value = pop();

        if (!embedding_model_value.is_string() || !vector_store_value.is_string() ||
            !chunk_size_value.is_number() || !chunk_overlap_value.is_number())
        {
          throw std::runtime_error("Knowledge definition has invalid types");
        }

        auto* name = as_string(name_value);
        auto* vector_store = as_string(vector_store_value);
        auto* embedding_model = as_string(embedding_model_value);
        const std::size_t chunk_size = static_cast<std::size_t>(chunk_size_value.as_number());
        const std::size_t chunk_overlap =
            static_cast<std::size_t>(chunk_overlap_value.as_number());

        auto sources = parse_sources_list(sources_value);
        auto* knowledge_obj =
            new_knowledge(name, vector_store, embedding_model, chunk_size, chunk_overlap,
                          std::move(sources));
        knowledge::Ingester ingester(knowledge_obj->store, knowledge_obj->chunk_size,
                                     knowledge_obj->chunk_overlap,
                                     to_std_string(knowledge_obj->embedding_model));
        for (const auto& source : knowledge_obj->sources)
        {
          ingester.ingest(source);
        }
        globals_.set(name, Value::Knowledge(knowledge_obj));
        knowledge_bases_[to_std_string(name)] = knowledge_obj;
        break;
      }
      case OpCode::OP_DEFINE_AGENT:
      {
        Value knowledge_value = pop();
        Value skills_value = pop();
        Value system_value = pop();
        Value temperature_value = pop();
        Value api_key_env_value = pop();
        Value endpoint_value = pop();
        Value model_value = pop();
        Value provider_value = pop();
        Value name_value = pop();

        if (!skills_value.is_list() || !knowledge_value.is_list())
        {
          throw std::runtime_error("Agent lists must be list");
        }

        auto* name = as_string(name_value);
        auto* provider = as_string(provider_value);
        auto* model = as_string(model_value);
        ObjString* endpoint = endpoint_value.is_nil() ? nullptr : as_string(endpoint_value);
        ObjString* api_key_env = api_key_env_value.is_nil() ? nullptr : as_string(api_key_env_value);
        ObjString* system = system_value.is_nil() ? nullptr : as_string(system_value);
        double temperature = temperature_value.is_nil() ? 0.0 : temperature_value.as_number();

        auto* skills = as_list(skills_value);
        auto* connected_knowledge = as_list(knowledge_value);
        auto* context = new_context();
        auto* agent =
            new_agent(name, provider, model, endpoint, api_key_env, system, temperature, skills,
                      connected_knowledge, context);
        globals_.set(name, Value::Agent(agent));
        break;
      }
      case OpCode::OP_EMIT:
      {
        Value value = pop();
        emitted_.push_back(std::move(value));
        break;
      }
      case OpCode::OP_TRACE:
      {
        Value event_value = pop();
        nlohmann::json payload = value_to_json(event_value);
        if (payload.is_object() && payload.contains("type"))
        {
          const std::string type = payload["type"].get<std::string>();
          nlohmann::json event_payload = nlohmann::json::object();
          if (payload.contains("payload"))
          {
            event_payload = payload["payload"];
          }
          trace_logger_.log_custom_event(type, event_payload);
        }
        else
        {
          trace_logger_.log_custom_event("STEP", payload);
        }
        break;
      }
      case OpCode::OP_RETURN:
      {
        Value result = pop();
        const bool is_tool = frames_.back().is_tool;
        const std::string tool_name = frames_.back().tool_name;
        while (stack_.size() > frames_.back().stack_start)
        {
          stack_.pop_back();
        }
        frames_.pop_back();
        if (is_tool)
        {
          trace_logger_.log_tool_result(tool_name, value_to_json(result));
        }
        if (frames_.empty())
        {
          trace_logger_.log_end();
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

void VirtualMachine::push_root(Value value)
{
  gc_roots_.push_back(value);
}

void VirtualMachine::pop_root()
{
  if (gc_roots_.empty())
  {
    throw std::runtime_error("GC root stack underflow");
  }
  gc_roots_.pop_back();
}

void VirtualMachine::define_native(const std::string& name, int arity, NativeFn function)
{
  auto* name_string = copy_string(name.c_str(), name.size());
  Value native_val = Value::Native(new_native(name_string, arity, function));
  globals_.set(name_string, native_val);
}

void VirtualMachine::emit_debug_event(DebugEventType type, std::string label, std::size_t ip,
                                      std::string payload)
{
  if (!debug_hook_)
  {
    return;
  }
  debug_hook_(DebugEvent{type, std::move(label), ip, std::move(payload)});
}
}  // namespace neamc::vm
