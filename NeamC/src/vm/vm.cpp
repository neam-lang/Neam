//
// Neam Virtual Machine - Interpreter implementation
//

#include "neamc/vm/vm.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <chrono>
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
#include "neamc/security/audit_log.hpp"
#include "neamc/security/behavioral_monitor.hpp"
#include "neamc/security/human_in_the_loop.hpp"
#include "neamc/security/injection_scanner.hpp"
#include "neamc/security/rate_limiter.hpp"
#include "neamc/vm/async/future.hpp"
#include "neamc/vm/external_skill.hpp"
#include "neamc/vm/knowledge.hpp"
#include "neamc/vm/schema.hpp"

namespace neamc::vm
{
namespace
{
// Read an integer from an environment variable with bounds checking.
int get_config_int(const char* env_var, int default_val, int min_val, int max_val)
{
  const char* env = std::getenv(env_var);
  if (env)
  {
    int val = std::atoi(env);
    if (val >= min_val && val <= max_val)
    {
      return val;
    }
  }
  return default_val;
}

// Maximum call stack depth — prevents native stack overflow (SIGSEGV) from
// infinite recursion. Configurable via NEAM_MAX_CALL_DEPTH env var.
int get_max_call_depth()
{
  return get_config_int("NEAM_MAX_CALL_DEPTH", 1000, 1, 100000);
}

const int kMaxCallDepth = get_max_call_depth();

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

std::string value_type_name(const Value& value)
{
  if (value.is_nil()) return "nil";
  if (value.is_bool()) return "bool";
  if (value.is_number()) return "number";
  if (value.is_string()) return "string";
  if (value.is_list()) return "list";
  if (value.is_map()) return "map";
  return "object";
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

bool match_key(const ObjString* key, const std::string& name)
{
  return key && key->length == name.size() &&
         std::memcmp(key->chars, name.data(), key->length) == 0;
}

const Value* find_global_value(const Table& table, const std::string& name)
{
  for (const auto& entry : table.entries())
  {
    if (match_key(entry.key, name))
    {
      return &entry.value;
    }
  }
  return nullptr;
}

std::string map_string_value(const ObjMap* map, const std::string& key)
{
  if (!map)
  {
    return {};
  }
  auto it = map->entries.find(key);
  if (it == map->entries.end())
  {
    return {};
  }
  if (it->second.is_string())
  {
    return to_std_string(it->second);
  }
  if (it->second.is_bool())
  {
    return it->second.as_bool() ? "true" : "false";
  }
  if (it->second.is_number())
  {
    return value_to_string(it->second);
  }
  return {};
}

double map_number_value(const ObjMap* map, const std::string& key)
{
  if (!map)
  {
    return 0.0;
  }
  auto it = map->entries.find(key);
  if (it == map->entries.end() || !it->second.is_number())
  {
    return 0.0;
  }
  return it->second.as_number();
}

[[maybe_unused]]
bool map_bool_value(const ObjMap* map, const std::string& key, bool fallback = false)
{
  if (!map)
  {
    return fallback;
  }
  auto it = map->entries.find(key);
  if (it == map->entries.end())
  {
    return fallback;
  }
  if (it->second.is_bool())
  {
    return it->second.as_bool();
  }
  if (it->second.is_string())
  {
    const auto value = to_std_string(it->second);
    if (value == "true")
    {
      return true;
    }
    if (value == "false")
    {
      return false;
    }
  }
  return fallback;
}

int64_t current_time_ms()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

bool matches_pattern(const std::string& pattern, const std::string& required)
{
  if (pattern == required)
  {
    return true;
  }
  if (!pattern.empty() && pattern.back() == '*')
  {
    const auto prefix = pattern.substr(0, pattern.size() - 1);
    return required.rfind(prefix, 0) == 0;
  }
  return false;
}

struct ReActResponse
{
  std::string thought;
  std::string action;
  std::vector<std::string> action_args;
  std::string final_answer;
  bool is_complete{false};
};

std::string trim_copy(std::string value)
{
  auto start = value.find_first_not_of(" \t\r\n");
  auto end = value.find_last_not_of(" \t\r\n");
  if (start == std::string::npos || end == std::string::npos)
  {
    return {};
  }
  return value.substr(start, end - start + 1);
}

ReActResponse parse_react_response(const std::string& response)
{
  ReActResponse state;
  const auto finish_pos = response.find("FINISH:");
  if (finish_pos != std::string::npos)
  {
    state.is_complete = true;
    state.final_answer = trim_copy(response.substr(finish_pos + 7));
    return state;
  }

  const auto thought_pos = response.find("THOUGHT:");
  if (thought_pos != std::string::npos)
  {
    const auto end_pos = response.find('\n', thought_pos);
    state.thought = trim_copy(response.substr(thought_pos + 8, end_pos - thought_pos - 8));
  }

  const auto action_pos = response.find("ACTION:");
  if (action_pos != std::string::npos)
  {
    const auto end_pos = response.find('\n', action_pos);
    const auto line =
        response.substr(action_pos + 7, end_pos == std::string::npos ? std::string::npos
                                                                     : end_pos - action_pos - 7);
    const auto action_line = trim_copy(line);
    const auto paren_pos = action_line.find('(');
    if (paren_pos != std::string::npos)
    {
      state.action = trim_copy(action_line.substr(0, paren_pos));
      const auto close_pos = action_line.find(')', paren_pos);
      if (close_pos != std::string::npos)
      {
        const auto args_str =
            action_line.substr(paren_pos + 1, close_pos - paren_pos - 1);
        std::stringstream arg_stream(args_str);
        std::string arg;
        while (std::getline(arg_stream, arg, ','))
        {
          state.action_args.push_back(trim_copy(arg));
        }
      }
    }
    else
    {
      state.action = action_line;
    }
  }
  return state;
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

std::vector<std::string> list_to_strings(const Value& value)
{
  if (!value.is_list())
  {
    return {};
  }
  std::vector<std::string> result;
  auto* list = as_list(value);
  result.reserve(list->items.size());
  for (const auto& item : list->items)
  {
    if (item.is_string())
    {
      result.push_back(to_std_string(item));
    }
  }
  return result;
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
    case OpCode::OP_GRANT:
      return "OP_GRANT";
    case OpCode::OP_CHECKPOINT:
      return "OP_CHECKPOINT";
    case OpCode::OP_REWIND:
      return "OP_REWIND";
    default:
      return "OP_UNKNOWN";
  }
}

}  // anonymous namespace

bool VirtualMachine::BudgetTracker::is_exhausted() const
{
  const int64_t now_ms = current_time_ms();
  if (limits.max_wall_time_ms > 0.0 && elapsed_ms(now_ms) >= limits.max_wall_time_ms)
  {
    return true;
  }
  if (limits.max_tokens > 0.0 && used_tokens >= limits.max_tokens)
  {
    return true;
  }
  if (limits.max_api_calls > 0.0 && used_api_calls >= limits.max_api_calls)
  {
    return true;
  }
  if (limits.max_cost > 0.0 && used_cost >= limits.max_cost)
  {
    return true;
  }
  return false;
}

int64_t VirtualMachine::BudgetTracker::elapsed_ms(int64_t now_ms) const
{
  if (start_time_ms <= 0)
  {
    return 0;
  }
  return now_ms - start_time_ms;
}

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
    throw std::runtime_error(
        "Type error: cannot apply '" + std::string(opcode_name(op)) +
        "' to " + value_type_name(lhs) + " and " + value_type_name(rhs) +
        " (expected numbers)");
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
        throw std::runtime_error(
            "Division by zero: " + std::to_string(a) + " / 0");
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
    throw std::runtime_error(
        "Type error: cannot concatenate " + value_type_name(lhs) +
        " and " + value_type_name(rhs) + " (expected strings)");
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
  frames_.push_back(CallFrame{&chunk, nullptr, 0, 0, false, {}});
  return run_frames(0);
}

Value VirtualMachine::run_frames(std::size_t target_frame_count)
{
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
          throw std::runtime_error(
              "Constant index out of range: index " + std::to_string(index) +
              " exceeds pool size " + std::to_string(constants.size()));
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
          throw std::runtime_error(
              "Local index out of range: slot " + std::to_string(slot) +
              " (stack_start=" + std::to_string(frame.stack_start) +
              ", stack_size=" + std::to_string(stack_.size()) + ")");
        }
        stack_.push_back(stack_[frame.stack_start + slot]);
        break;
      }
      case OpCode::OP_SET_LOCAL:
      {
        const auto slot = read_short(code, frame.ip);
        if (frame.stack_start + slot >= stack_.size())
        {
          throw std::runtime_error(
              "Local index out of range for set: slot " + std::to_string(slot) +
              " (stack_start=" + std::to_string(frame.stack_start) +
              ", stack_size=" + std::to_string(stack_.size()) + ")");
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
          throw std::runtime_error(
              "Undefined variable '" + std::string(name->chars, name->length) + "'");
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
          throw std::runtime_error(
              "Undefined variable '" + std::string(name->chars, name->length) +
              "' (cannot assign to undeclared variable)");
        }
        break;
      }
      case OpCode::OP_NEGATE:
      {
        Value value = pop();
        if (!value.is_number())
        {
          throw std::runtime_error(
              "Type error: cannot negate " + value_type_name(value) +
              " (expected number)");
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
          throw std::runtime_error(
              "Type error: cannot compare " + value_type_name(lhs) +
              " and " + value_type_name(rhs) + " (expected numbers)");
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
        // Stack depth protection (v0.6.5 reliability fix)
        if (static_cast<int>(frames_.size()) >= kMaxCallDepth)
        {
          throw std::runtime_error(
              "Stack overflow: maximum call depth (" +
              std::to_string(kMaxCallDepth) +
              ") exceeded. Check for infinite recursion.");
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
            std::string fname = fn->name ? std::string(fn->name->chars, fn->name->length) : "<anonymous>";
            throw std::runtime_error(
                "Argument count mismatch: '" + fname + "' expects " +
                std::to_string(fn->arity) + " argument(s), got " +
                std::to_string(arg_count));
          }
          stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(callee_index));
          frames_.push_back(CallFrame{&fn->chunk, fn, 0, callee_index, false, {}});
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
            throw std::runtime_error(
              "Permission denied: skill '" + skill_name +
              "' is not in the allowed skills list");
          }
          if (!skill->impl)
          {
            throw std::runtime_error(
                "Skill '" + skill_name + "' has no implementation");
          }
          if (skill->impl->arity != arg_count)
          {
            throw std::runtime_error(
                "Argument count mismatch: skill '" + skill_name + "' expects " +
                std::to_string(skill->impl->arity) + " argument(s), got " +
                std::to_string(arg_count));
          }
          validate_skill_args(skill, args);
          stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(callee_index));
          frames_.push_back(CallFrame{&skill->impl->chunk, skill->impl, 0, callee_index, true, skill_name});
        }
        else
        {
          throw std::runtime_error(
              "Type error: attempted to call non-callable " +
              value_type_name(callee));
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
            throw std::runtime_error(
                "Property error: '" + key + "' not found in map");
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
            throw std::runtime_error(
                "Property error: unknown agent property '" + key + "'");
          }
        }
        else
        {
          throw std::runtime_error(
              "Type error: cannot access property '" + key +
              "' on " + value_type_name(receiver));
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
            throw std::runtime_error(
                "Type error: list index must be a number, got " +
                value_type_name(index_value));
          }
          const double raw_index = index_value.as_number();
          const double floored = std::floor(raw_index);
          if (raw_index < 0 || floored != raw_index)
          {
            throw std::runtime_error(
                "Index error: list index " + std::to_string(raw_index) +
                " must be a non-negative integer");
          }
          const std::size_t index = static_cast<std::size_t>(floored);
          auto* list = as_list(base_value);
          if (index >= list->items.size())
          {
            throw std::runtime_error(
                "Index out of range: index " + std::to_string(index) +
                " not valid for list of size " + std::to_string(list->items.size()));
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
            throw std::runtime_error(
                "Key error: '" + key + "' not found in map");
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

            const std::string agent_name = to_std_string(agent->name);
            const auto extension_it = agent_extensions_.find(agent_name);
            const AgentExtension extension =
                extension_it == agent_extensions_.end() ? AgentExtension{} : extension_it->second;

            // v0.6.9 D8: Agent recursion depth guard
            static thread_local int agent_depth = 0;
            struct DepthGuard {
              DepthGuard() { ++agent_depth; }
              ~DepthGuard() { --agent_depth; }
            } depth_guard;

            {
              int max_depth =
                  security::RateLimiter::instance().input_limits().max_agent_depth;
              if (agent_depth > max_depth)
              {
                security::AuditLogger::instance().log_tool_call(
                    security::TraceContext::current(), agent_name, "agent.ask",
                    "", 0, "error:depth_exceeded");
                std::string depth_err =
                    "Agent call depth exceeded (max " + std::to_string(max_depth) + ")";
                stack_.push_back(Value::String(depth_err.c_str(), depth_err.size()));
                break;
              }
            }

            // v0.6.9 D9: Kill switch — reject if agent is disabled
            if (security::BehavioralMonitor::instance().is_disabled(agent_name))
            {
              security::AuditLogger::instance().log({
                  {},
                  security::TraceContext::current(),
                  security::EventType::AgentDisabled,
                  agent_name,
                  {},
                  "Agent is disabled (kill switch active)",
                  {}});
              std::string disabled_err = "Agent '" + agent_name + "' is disabled";
              stack_.push_back(Value::String(disabled_err.c_str(), disabled_err.size()));
              break;
            }

            // v0.6.9 D9: Track agent execution start time for behavioral metrics
            auto agent_exec_start = std::chrono::steady_clock::now();
            int agent_tool_call_count = 0;
            std::vector<std::string> agent_tools_used;

            auto get_budget_definition = [&](const std::string& name) -> BudgetDefinition {
              auto it = budgets_.find(name);
              if (it != budgets_.end())
              {
                return it->second;
              }
              BudgetDefinition def;
              if (const Value* budget_value = find_global_value(globals_, name))
              {
                if (budget_value->is_map())
                {
                  auto* map = as_map(*budget_value);
                  def.max_tokens = map_number_value(map, "max_tokens");
                  def.max_api_calls = map_number_value(map, "max_api_calls");
                  def.max_wall_time_ms = map_number_value(map, "max_wall_time");
                  if (def.max_wall_time_ms <= 0.0)
                  {
                    def.max_wall_time_ms = map_number_value(map, "max_wall_time_ms");
                  }
                  def.max_cost = map_number_value(map, "max_cost");
                }
              }
              budgets_[name] = def;
              return def;
            };

            auto get_budget_tracker = [&](const std::string& name) -> BudgetTracker& {
              auto it = budget_trackers_.find(name);
              if (it == budget_trackers_.end())
              {
                BudgetTracker tracker;
                tracker.limits = get_budget_definition(name);
                tracker.start_time_ms = current_time_ms();
                auto [inserted_it, _] = budget_trackers_.emplace(name, tracker);
                return inserted_it->second;
              }
              if (it->second.start_time_ms == 0)
              {
                it->second.start_time_ms = current_time_ms();
              }
              return it->second;
            };

            auto has_capability = [&](const std::string& entity,
                                      const std::string& required) -> bool {
              auto cap_it = entity_capabilities_.find(entity);
              if (cap_it == entity_capabilities_.end())
              {
                return false;
              }
              for (const auto& pattern : cap_it->second)
              {
                if (matches_pattern(pattern, required))
                {
                  return true;
                }
              }
              return false;
            };

            bool capability_ok = true;
            for (const auto& required : extension.required_capabilities)
            {
              if (!has_capability(agent_name, required))
              {
                capability_ok = false;
                break;
              }
            }
            if (!capability_ok)
            {
              const std::string message = "Missing capability for agent request";
              stack_.push_back(Value::String(message.c_str(), message.size()));
              break;
            }

            if (!extension.memory.empty())
            {
              auto& store = memory_stores_[extension.memory];
              store.events.push_back(
                  MemoryEvent{current_time_ms(), "user", query, agent_name});
            }

            agent->context->history.push_back({"user", query});

            auto resolve_env_value = [&](const ObjMap* env_map,
                                         const std::string& key) -> std::string {
              if (!env_map)
              {
                return {};
              }
              auto it = env_map->entries.find(key);
              if (it == env_map->entries.end())
              {
                return {};
              }
              if (it->second.is_map())
              {
                auto* inner = as_map(it->second);
                const auto env_var = map_string_value(inner, "env_var");
                if (!env_var.empty())
                {
                  if (const char* env_value = std::getenv(env_var.c_str()))
                  {
                    return std::string(env_value);
                  }
                }
              }
              if (it->second.is_string() || it->second.is_bool() || it->second.is_number())
              {
                return value_to_string(it->second);
              }
              return {};
            };

            const ObjMap* env_map = nullptr;
            if (!extension.env.empty())
            {
              if (const Value* env_value = find_global_value(globals_, extension.env))
              {
                if (env_value->is_map())
                {
                  env_map = as_map(*env_value);
                }
              }
            }

            llm::ProviderConfig config;
            config.model = to_std_string(agent->model);
            config.endpoint = to_std_string(agent->endpoint);
            config.api_key = to_std_string(agent->api_key_env);
            config.temperature = agent->temperature;
            config.default_host =
                resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST",
                                   "http://localhost:11434");

            std::string provider_name = to_std_string(agent->provider);
            if (env_map)
            {
              const auto env_provider = resolve_env_value(env_map, "provider");
              const auto env_model = resolve_env_value(env_map, "model");
              const auto env_endpoint = resolve_env_value(env_map, "endpoint");
              const auto env_api_key = resolve_env_value(env_map, "api_key");
              const auto env_temperature = resolve_env_value(env_map, "temperature");
              if (!env_provider.empty())
              {
                provider_name = env_provider;
              }
              if (!env_model.empty())
              {
                config.model = env_model;
              }
              if (!env_endpoint.empty())
              {
                config.endpoint = env_endpoint;
              }
              if (!env_api_key.empty())
              {
                config.api_key = env_api_key;
              }
              if (!env_temperature.empty())
              {
                config.temperature = std::strtod(env_temperature.c_str(), nullptr);
              }
            }

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

            auto provider = llm::create_provider(provider_name, config);

            auto get_guard_def = [&](const std::string& name) -> GuardDef* {
              auto it = guards_.find(name);
              if (it != guards_.end())
              {
                return &it->second;
              }
              const Value* guard_value = find_global_value(globals_, name);
              if (!guard_value || !guard_value->is_map())
              {
                return nullptr;
              }
              GuardDef def;
              auto* map = as_map(*guard_value);
              def.description = map_string_value(map, "description");
              auto handler_it = map->entries.find("handlers");
              if (handler_it != map->entries.end() && handler_it->second.is_list())
              {
                auto* handler_list = as_list(handler_it->second);
                for (const auto& handler_value : handler_list->items)
                {
                  if (!handler_value.is_map())
                  {
                    continue;
                  }
                  auto* handler_map = as_map(handler_value);
                  GuardHandlerDef handler_def;
                  handler_def.type = map_string_value(handler_map, "type");
                  if (auto params_it = handler_map->entries.find("params");
                      params_it != handler_map->entries.end())
                  {
                    handler_def.parameters = list_to_strings(params_it->second);
                  }
                  auto impl_it = handler_map->entries.find("impl");
                  if (impl_it != handler_map->entries.end() &&
                      is_obj_type(impl_it->second, ObjType::OBJ_FUNCTION))
                  {
                    handler_def.impl = as_function(impl_it->second);
                    def.handlers.push_back(handler_def);
                  }
                }
              }
              auto [inserted_it, _] = guards_.emplace(name, std::move(def));
              return &inserted_it->second;
            };

            auto get_guardchain_def = [&](const std::string& name) -> GuardChainDef* {
              auto it = guardchains_.find(name);
              if (it != guardchains_.end())
              {
                return &it->second;
              }
              const Value* chain_value = find_global_value(globals_, name);
              if (!chain_value || !chain_value->is_map())
              {
                return nullptr;
              }
              GuardChainDef chain;
              auto* map = as_map(*chain_value);
              auto guard_it = map->entries.find("guards");
              if (guard_it != map->entries.end())
              {
                chain.guards = list_to_strings(guard_it->second);
              }
              auto [inserted_it, _] = guardchains_.emplace(name, std::move(chain));
              return &inserted_it->second;
            };

            auto run_guard_chain =
                [&](const std::vector<std::string>& chains,
                    const std::string& handler_type, std::string& value) -> bool {
              auto& audit = security::AuditLogger::instance();
              for (const auto& chain_name : chains)
              {
                auto* chain = get_guardchain_def(chain_name);
                if (!chain)
                {
                  continue;
                }
                for (const auto& guard_name : chain->guards)
                {
                  auto* guard = get_guard_def(guard_name);
                  if (!guard)
                  {
                    continue;
                  }
                  for (const auto& handler : guard->handlers)
                  {
                    if (handler.type != handler_type || !handler.impl)
                    {
                      continue;
                    }
                    std::vector<Value> handler_args;
                    if (!handler.parameters.empty())
                    {
                      handler_args.push_back(
                          Value::String(value.c_str(), value.size()));
                    }
                    Value result =
                        call_function(handler.impl, handler_args, false, guard_name);
                    if (result.is_string())
                    {
                      const auto output = to_std_string(result);
                      if (output == "block")
                      {
                        auto input_preview = value.size() > 64
                                                 ? value.substr(0, 64) + "..."
                                                 : value;
                        audit.log_guard(security::TraceContext::current(),
                                        guard_name, "block", input_preview);
                        return false;
                      }
                      value = output;
                    }
                  }
                }
              }
              // Log pass only if there were chains to evaluate
              if (!chains.empty())
              {
                audit.log_guard(security::TraceContext::current(),
                                chains.front(), "pass", "");
              }
              return true;
            };

            auto get_tool_def = [&](const std::string& name) -> ToolDef* {
              auto it = tools_.find(name);
              if (it != tools_.end())
              {
                return &it->second;
              }
              const Value* tool_value = find_global_value(globals_, name);
              if (!tool_value || !tool_value->is_map())
              {
                return nullptr;
              }
              ToolDef tool;
              auto* map = as_map(*tool_value);
              tool.description = map_string_value(map, "description");
              if (auto cap_it = map->entries.find("capabilities");
                  cap_it != map->entries.end())
              {
                tool.capabilities = list_to_strings(cap_it->second);
              }
              if (auto guard_it = map->entries.find("guards");
                  guard_it != map->entries.end())
              {
                tool.guards = list_to_strings(guard_it->second);
              }
              if (auto cost_it = map->entries.find("budget_costs");
                  cost_it != map->entries.end() && cost_it->second.is_map())
              {
                auto* cost_map = as_map(cost_it->second);
                for (const auto& entry : cost_map->entries)
                {
                  if (entry.second.is_number())
                  {
                    tool.budget_costs[entry.first] = entry.second.as_number();
                  }
                }
              }
              if (auto impl_it = map->entries.find("impl"); impl_it != map->entries.end())
              {
                if (is_obj_type(impl_it->second, ObjType::OBJ_FUNCTION))
                {
                  tool.impl = as_function(impl_it->second);
                }
              }
              auto [inserted_it, _] = tools_.emplace(name, std::move(tool));
              return &inserted_it->second;
            };

            auto consume_budget = [&](const std::string& budget_name,
                                      const std::string& resource,
                                      double amount) -> bool {
              if (budget_name.empty())
              {
                return true;
              }
              auto& tracker = get_budget_tracker(budget_name);
              if (resource == "tokens")
              {
                tracker.used_tokens += amount;
              }
              else if (resource == "api_calls")
              {
                tracker.used_api_calls += amount;
              }
              else if (resource == "cost")
              {
                tracker.used_cost += amount;
              }

              bool exhausted = tracker.is_exhausted();
              // Log when >80% utilized or exhausted
              double used = 0, limit = 0;
              if (resource == "tokens")
              {
                used = tracker.used_tokens;
                limit = tracker.limits.max_tokens;
              }
              else if (resource == "api_calls")
              {
                used = tracker.used_api_calls;
                limit = tracker.limits.max_api_calls;
              }
              else if (resource == "cost")
              {
                used = tracker.used_cost;
                limit = tracker.limits.max_cost;
              }
              if (limit > 0 && (exhausted || (used / limit) > 0.8))
              {
                security::AuditLogger::instance().log_budget(
                    security::TraceContext::current(),
                    budget_name, resource, used, limit, exhausted);
              }
              return !exhausted;
            };

            auto run_tool = [&](const std::string& tool_name,
                                const std::vector<std::string>& arg_texts) -> std::string {
              auto& audit = security::AuditLogger::instance();
              auto trace_id = security::TraceContext::current();
              auto tool_start = std::chrono::steady_clock::now();

              ToolDef* tool = get_tool_def(tool_name);
              if (!tool || !tool->impl)
              {
                audit.log_tool_call(trace_id, agent_name, tool_name, "",
                                    0, "error:unknown_tool");
                return "Unknown tool: " + tool_name;
              }

              for (const auto& required : tool->capabilities)
              {
                if (!has_capability(agent_name, required))
                {
                  audit.log_tool_call(trace_id, agent_name, tool_name, "",
                                      0, "error:missing_capability");
                  return "Missing capability: " + required;
                }
              }

              // v0.6.9: Policy enforcement — check before budget/guards
              if (!extension.policy.empty())
              {
                auto policy_it = policies_.find(extension.policy);
                if (policy_it == policies_.end())
                {
                  // Try loading from globals
                  const Value* pv = find_global_value(globals_, extension.policy);
                  if (pv && pv->is_map())
                  {
                    auto* pm = as_map(*pv);
                    security::PolicyDef def;
                    def.name = extension.policy;
                    if (auto it = pm->entries.find("allow"); it != pm->entries.end())
                    {
                      for (const auto& v : as_list(it->second)->items)
                        def.allow_tools.insert(to_std_string(v));
                    }
                    if (auto it = pm->entries.find("deny"); it != pm->entries.end())
                    {
                      for (const auto& v : as_list(it->second)->items)
                        def.deny_tools.insert(to_std_string(v));
                    }
                    if (auto it = pm->entries.find("confirm"); it != pm->entries.end())
                    {
                      for (const auto& v : as_list(it->second)->items)
                        def.confirm_tools.insert(to_std_string(v));
                    }
                    if (auto it = pm->entries.find("default_deny"); it != pm->entries.end())
                    {
                      def.default_deny = is_truthy(it->second);
                    }
                    policies_[extension.policy] = std::move(def);
                    policy_it = policies_.find(extension.policy);
                  }
                }
                if (policy_it != policies_.end())
                {
                  std::string args_preview;
                  for (const auto& a : arg_texts)
                  {
                    if (!args_preview.empty()) args_preview += ",";
                    args_preview += a;
                  }
                  auto result =
                      security::evaluate_policy(policy_it->second, tool_name, args_preview);
                  if (result.action == security::PolicyAction::Deny)
                  {
                    audit.log_policy_deny(trace_id, agent_name, tool_name, result.reason);
                    return "Denied by policy: " + result.reason;
                  }
                  if (result.action == security::PolicyAction::Confirm)
                  {
                    // D10: Check if already approved
                    auto& cm = security::ConfirmationManager::instance();
                    auto status = cm.check(trace_id);
                    if (!status.has_value() || *status != security::ConfirmationStatus::Approved)
                    {
                      std::string args_json;
                      for (const auto& a : arg_texts)
                      {
                        if (!args_json.empty()) args_json += ",";
                        args_json += a;
                      }
                      cm.submit(trace_id, agent_name, tool_name, args_json,
                                "Policy requires confirmation: " + result.reason);
                      audit.log({
                          {},
                          trace_id,
                          security::EventType::PolicyConfirm,
                          agent_name,
                          tool_name,
                          "Tool requires human confirmation",
                          {{"reason", result.reason}}});
                      return "Pending confirmation [" + trace_id + "]: " + result.reason;
                    }
                  }
                }
              }

              // v0.6.9 D10: Sensitive skill check
              {
                const Value* skill_value = find_global_value(globals_, tool_name);
                if (skill_value && skill_value->is_skill())
                {
                  auto* skill = as_skill(*skill_value);
                  if (skill->sensitive)
                  {
                    auto& cm = security::ConfirmationManager::instance();
                    auto status = cm.check(trace_id);
                    if (!status.has_value() || *status != security::ConfirmationStatus::Approved)
                    {
                      std::string args_json;
                      for (const auto& a : arg_texts)
                      {
                        if (!args_json.empty()) args_json += ",";
                        args_json += a;
                      }
                      cm.submit(trace_id, agent_name, tool_name, args_json,
                                "Skill marked sensitive");
                      audit.log({
                          {},
                          trace_id,
                          security::EventType::PolicyConfirm,
                          agent_name,
                          tool_name,
                          "Sensitive skill requires human confirmation",
                          {}});
                      return "Pending confirmation [" + trace_id + "]: "
                             "Sensitive skill '" + tool_name + "' requires approval";
                    }
                  }
                }
              }

              // v0.6.9 D5: Circuit breaker check
              {
                auto& breaker = security::CircuitBreaker::instance();
                if (breaker.is_open(tool_name))
                {
                  audit.log_tool_call(trace_id, agent_name, tool_name, "",
                                      0, "error:circuit_open");
                  return "Tool '" + tool_name + "' is temporarily disabled (circuit breaker open)";
                }
              }

              // v0.6.9 D5: Per-tool rate limiting
              {
                auto& limiter = security::RateLimiter::instance();
                if (!limiter.check_tool(tool_name))
                {
                  audit.log_rate_limit(trace_id, tool_name, "tool");
                  return "Rate limit exceeded for tool: " + tool_name;
                }
              }

              for (const auto& cost : tool->budget_costs)
              {
                if (!consume_budget(extension.budget, cost.first, cost.second))
                {
                  audit.log_tool_call(trace_id, agent_name, tool_name, "",
                                      0, "error:budget_exhausted");
                  return "Budget exhausted for: " + cost.first;
                }
              }

              std::string input_payload;
              for (std::size_t i = 0; i < arg_texts.size(); ++i)
              {
                if (i > 0)
                {
                  input_payload += ",";
                }
                input_payload += arg_texts[i];
              }

              // v0.6.9 D8: Tool argument size limit
              {
                auto max_arg_bytes =
                    security::RateLimiter::instance().input_limits().max_tool_arg_bytes;
                if (input_payload.size() > max_arg_bytes)
                {
                  audit.log_tool_call(trace_id, agent_name, tool_name,
                                      input_payload.substr(0, 128), 0,
                                      "error:arg_too_large");
                  return "Tool argument exceeds maximum size of " +
                         std::to_string(max_arg_bytes) + " bytes";
                }
              }

              // v0.6.9: Injection scanning on tool arguments
              {
                static const security::InjectionScanner scanner(
                    security::InjectionStrictness::Moderate);
                auto scan_result = scanner.scan(input_payload);
                if (scan_result.blocked)
                {
                  std::string patterns_str;
                  for (const auto& p : scan_result.patterns)
                  {
                    if (!patterns_str.empty()) patterns_str += ",";
                    patterns_str += p;
                  }
                  audit.log_injection(trace_id, scan_result.score, patterns_str);
                  audit.log_tool_call(trace_id, agent_name, tool_name,
                                      input_payload.substr(0, 128), 0,
                                      "blocked:injection");
                  return "Blocked: potential prompt injection detected in tool arguments "
                         "(score=" + std::to_string(scan_result.score) + ")";
                }
              }

              std::string guard_input = input_payload;
              if (!run_guard_chain(tool->guards, "on_tool_input", guard_input))
              {
                auto ms = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - tool_start)
                        .count());
                audit.log_tool_call(trace_id, agent_name, tool_name,
                                    input_payload.substr(0, 128), ms, "blocked:guard");
                return "Request blocked by guard policy.";
              }

              std::vector<Value> tool_args;
              tool_args.reserve(arg_texts.size());
              for (const auto& arg : arg_texts)
              {
                tool_args.push_back(Value::String(arg.c_str(), arg.size()));
              }

              // v0.6.9 D5: Execute with circuit breaker tracking
              std::string output;
              try
              {
                Value result = call_function(tool->impl, tool_args, true, tool_name);
                output = value_to_string(result);
                security::CircuitBreaker::instance().record_success(tool_name);
              }
              catch (const std::exception& ex)
              {
                security::CircuitBreaker::instance().record_failure(tool_name);
                auto ms = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - tool_start)
                        .count());
                audit.log_tool_call(trace_id, agent_name, tool_name,
                                    input_payload.substr(0, 128), ms, "error:exception");
                return std::string("Tool error: ") + ex.what();
              }

              if (!run_guard_chain(tool->guards, "on_tool_output", output))
              {
                auto ms = static_cast<int>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - tool_start)
                        .count());
                audit.log_tool_call(trace_id, agent_name, tool_name,
                                    input_payload.substr(0, 128), ms, "blocked:output_guard");
                return "Response blocked by guard policy.";
              }

              // Log successful tool execution
              auto ms = static_cast<int>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - tool_start)
                      .count());
              audit.log_tool_call(trace_id, agent_name, tool_name,
                                  input_payload.substr(0, 128), ms, "success");

              // v0.6.9 D9: Track tool usage for behavioral monitoring
              ++agent_tool_call_count;
              agent_tools_used.push_back(tool_name);

              return output;
            };

            auto run_llm = [&](const std::vector<llm::Message>& messages) -> std::string {
              std::ostringstream preview;
              preview << "provider=" << provider_name << "\n";
              preview << "model=" << config.model << "\n";
              for (const auto& message : messages)
              {
                preview << "[" << message.role << "] " << message.content << "\n";
              }
              emit_debug_event(DebugEventType::BeforeAgentAsk, "agent.ask", frame.ip - 1,
                               preview.str());

              if (!extension.budget.empty())
              {
                auto& tracker = get_budget_tracker(extension.budget);
                if (tracker.is_exhausted())
                {
                  security::AuditLogger::instance().log_budget(
                      security::TraceContext::current(),
                      extension.budget, "api_calls",
                      tracker.used_api_calls, tracker.limits.max_api_calls, true);
                  return "Budget exhausted: " + extension.budget;
                }
                tracker.used_api_calls += 1.0;
              }

              const auto response_text = provider->chat(messages);
              if (!extension.budget.empty())
              {
                auto& tracker = get_budget_tracker(extension.budget);
                const double token_estimate =
                    std::max(1.0, static_cast<double>(response_text.size()) / 4.0);
                tracker.used_tokens += token_estimate;
              }
              return response_text;
            };

            auto build_messages = [&](const std::string& system_prompt,
                                      const std::string& user_prompt) {
              std::vector<llm::Message> messages;
              if (!system_prompt.empty())
              {
                // v0.6.9: Wrap system prompt with injection boundaries
                static const security::InjectionScanner scanner(
                    security::InjectionStrictness::Moderate);
                auto nonce = security::generate_trace_id().substr(0, 8);
                auto wrapped = scanner.wrap_system_prompt(system_prompt, nonce);
                messages.push_back({"system", wrapped});
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
                  auto* knowledge_obj = kb_it->second;

                  // Map VM strategy enum to knowledge strategy enum
                  knowledge::Strategy kb_strategy = knowledge::Strategy::kBasic;
                  switch (knowledge_obj->retrieval_strategy)
                  {
                    case RetrievalStrategy::kBasic:
                      kb_strategy = knowledge::Strategy::kBasic;
                      break;
                    case RetrievalStrategy::kMMR:
                      kb_strategy = knowledge::Strategy::kMMR;
                      break;
                    case RetrievalStrategy::kHybrid:
                      kb_strategy = knowledge::Strategy::kHybrid;
                      break;
                    case RetrievalStrategy::kHyDE:
                      kb_strategy = knowledge::Strategy::kHyDE;
                      break;
                    case RetrievalStrategy::kSelfRAG:
                      kb_strategy = knowledge::Strategy::kSelfRAG;
                      break;
                    case RetrievalStrategy::kCRAG:
                      kb_strategy = knowledge::Strategy::kCRAG;
                      break;
                    case RetrievalStrategy::kAgentic:
                      kb_strategy = knowledge::Strategy::kAgentic;
                      break;
                    case RetrievalStrategy::kGraphRAG:
                      kb_strategy = knowledge::Strategy::kGraphRAG;
                      break;
                  }

                  // Map VM options to knowledge options
                  knowledge::StrategyOptions kb_options;
                  kb_options.top_k = knowledge_obj->strategy_options.top_k;
                  kb_options.relevance_threshold = knowledge_obj->strategy_options.relevance_threshold;
                  kb_options.mmr_lambda = knowledge_obj->strategy_options.mmr_lambda;
                  kb_options.num_hypothetical = knowledge_obj->strategy_options.num_hypothetical;
                  kb_options.enable_relevance_check = knowledge_obj->strategy_options.enable_relevance_check;
                  kb_options.enable_support_check = knowledge_obj->strategy_options.enable_support_check;
                  kb_options.enable_web_fallback = knowledge_obj->strategy_options.enable_web_fallback;
                  kb_options.enable_query_decomposition = knowledge_obj->strategy_options.enable_query_decomposition;
                  kb_options.max_corrections = knowledge_obj->strategy_options.max_corrections;
                  kb_options.max_iterations = knowledge_obj->strategy_options.max_iterations;
                  kb_options.enable_reflection = knowledge_obj->strategy_options.enable_reflection;
                  kb_options.search_depth = knowledge_obj->strategy_options.search_depth;
                  kb_options.include_communities = knowledge_obj->strategy_options.include_communities;

                  // Create LLM callback for advanced strategies
                  knowledge::LLMCallback llm_callback = nullptr;
                  if (kb_strategy != knowledge::Strategy::kBasic &&
                      kb_strategy != knowledge::Strategy::kMMR &&
                      kb_strategy != knowledge::Strategy::kHybrid)
                  {
                    llm_callback = [&provider](const std::string& prompt) -> std::string {
                      return provider->complete(prompt);
                    };
                  }

                  const char* strategy_names[] = {"basic", "mmr", "hybrid", "hyde", "self_rag",
                                                  "crag", "agentic", "graph_rag"};
                  output_stream() << "[RAG] Searching KB '" << kb_name << "' with strategy '"
                                  << strategy_names[static_cast<int>(kb_strategy)] << "' for: '"
                                  << user_prompt << "'\n";

                  auto retrieval_result = knowledge::retrieve_with_strategy(
                      knowledge_obj->store, user_prompt, kb_strategy, kb_options, llm_callback);

                  if (!retrieval_result.documents.empty())
                  {
                    output_stream() << "[RAG] Found " << retrieval_result.documents.size()
                                    << " documents via " << retrieval_result.strategy_used << "\n";
                    if (!retrieval_result.hypothetical_docs.empty())
                    {
                      output_stream() << "[RAG] HyDE generated " << retrieval_result.hypothetical_docs.size()
                                      << " hypothetical doc(s)\n";
                    }
                    if (!retrieval_result.sub_queries.empty())
                    {
                      output_stream() << "[RAG] CRAG decomposed into " << retrieval_result.sub_queries.size()
                                      << " sub-queries\n";
                    }
                  }

                  const auto context = format_rag_context(retrieval_result.documents);
                  if (!context.empty())
                  {
                    combined_context += context;
                  }
                }
                if (!combined_context.empty())
                {
                  // v0.6.9: Tag RAG content as data-only to prevent injection
                  static const security::InjectionScanner ctx_scanner(
                      security::InjectionStrictness::Moderate);
                  auto tagged = ctx_scanner.tag_retrieved_context(combined_context);
                  messages.push_back({"system", tagged});
                }
              }
              for (const auto& message : agent->context->history)
              {
                messages.push_back({message.role, message.content});
              }
              messages.push_back({"user", user_prompt});
              return messages;
            };

            // v0.6.6: Collect agent's skills as LLM tool definitions.
            // Skills are stored as string names in agent->skills (compiler emits names).
            // We resolve each name to its ObjSkill via globals lookup.
            auto collect_agent_tools = [&]() -> std::vector<llm::ToolDefinition> {
              std::vector<llm::ToolDefinition> tool_defs;
              if (!agent->skills || agent->skills->items.empty())
              {
                return tool_defs;
              }
              for (const auto& skill_value : agent->skills->items)
              {
                ObjSkill* skill = nullptr;
                if (skill_value.is_skill())
                {
                  skill = as_skill(skill_value);
                }
                else if (skill_value.is_string())
                {
                  // Resolve skill name from globals
                  const std::string skill_name = to_std_string(skill_value);
                  const Value* resolved = find_global_value(globals_, skill_name);
                  if (resolved && resolved->is_skill())
                  {
                    skill = as_skill(*resolved);
                  }
                }
                if (!skill || !skill->name)
                {
                  continue;
                }
                if (!skill->impl && !skill->external)
                {
                  continue;
                }
                llm::ToolDefinition def;
                def.name = std::string(skill->name->chars, skill->name->length);
                def.description = skill->description
                    ? std::string(skill->description->chars, skill->description->length)
                    : "";
                def.input_schema = build_skill_schema(skill);
                // v0.6.7: Set type for Claude built-in tools
                if (skill->external &&
                    skill->external->binding == vm::SkillBinding::ClaudeBuiltin)
                {
                  def.type = skill->external->claude_tool_type;
                }
                tool_defs.push_back(std::move(def));
              }
              return tool_defs;
            };

            // v0.6.6: Convert JSON argument value to Neam Value
            auto json_to_value = [](const nlohmann::json& j) -> Value {
              if (j.is_null())
              {
                return Value::Nil();
              }
              if (j.is_boolean())
              {
                return Value::Bool(j.get<bool>());
              }
              if (j.is_number())
              {
                return Value::Number(j.get<double>());
              }
              if (j.is_string())
              {
                const auto& s = j.get_ref<const std::string&>();
                return Value::String(s.c_str(), s.size());
              }
              // For arrays/objects, serialize to JSON string
              const auto s = j.dump();
              return Value::String(s.c_str(), s.size());
            };

            // v0.6.6: Find a skill object by name from agent->skills list.
            // Resolves string skill names via globals lookup.
            auto find_agent_skill = [&](const std::string& name) -> ObjSkill* {
              if (!agent->skills)
              {
                return nullptr;
              }
              for (const auto& skill_value : agent->skills->items)
              {
                ObjSkill* skill = nullptr;
                if (skill_value.is_skill())
                {
                  skill = as_skill(skill_value);
                }
                else if (skill_value.is_string())
                {
                  const std::string skill_name = to_std_string(skill_value);
                  if (skill_name != name)
                  {
                    continue;
                  }
                  const Value* resolved = find_global_value(globals_, skill_name);
                  if (resolved && resolved->is_skill())
                  {
                    skill = as_skill(*resolved);
                  }
                }
                if (skill && skill->name &&
                    std::string(skill->name->chars, skill->name->length) == name)
                {
                  return skill;
                }
              }
              return nullptr;
            };

            std::string plan_pattern;
            if (!extension.plan.empty())
            {
              if (const Value* plan_value = find_global_value(globals_, extension.plan))
              {
                if (plan_value->is_map())
                {
                  plan_pattern = map_string_value(as_map(*plan_value), "pattern");
                }
              }
            }
            std::string plan_pattern_lower = plan_pattern;
            std::transform(plan_pattern_lower.begin(), plan_pattern_lower.end(),
                           plan_pattern_lower.begin(), [](unsigned char c) {
                             return static_cast<char>(std::tolower(c));
                           });

            std::string final_response;
            if (plan_pattern_lower == "react")
            {
              const int max_steps = get_config_int("NEAM_MAX_REACT_STEPS", 100, 1, 10000);
              std::string observation = query;
              for (int step = 0; step < max_steps; ++step)
              {
                std::string system_prompt = agent->system ? to_std_string(agent->system) : "";
                system_prompt += "\n\nYou are operating in ReAct mode. Format:\n";
                system_prompt += "THOUGHT: <analysis>\nACTION: <action>(<args>)\n";
                system_prompt += "or FINISH: <answer> when complete.";

                std::string user_prompt = "Observation: " + observation;
                if (!run_guard_chain(extension.guardchains, "on_tool_input", user_prompt))
                {
                  final_response = "Request blocked by guard policy.";
                  break;
                }

                const auto response_text = run_llm(build_messages(system_prompt, user_prompt));
                trace_logger_.log_llm_output(agent_name, response_text);
                const auto react_state = parse_react_response(response_text);
                if (react_state.is_complete)
                {
                  final_response = react_state.final_answer;
                  if (!run_guard_chain(extension.guardchains, "on_tool_output", final_response))
                  {
                    final_response = "Response blocked by guard policy.";
                  }
                  break;
                }
                if (!react_state.action.empty())
                {
                  const std::string action_result =
                      run_tool(react_state.action, react_state.action_args);
                  observation =
                      "Action: " + react_state.action + "\nResult: " + action_result;
                  agent->context->history.push_back(
                      {"tool", observation});
                }
                else
                {
                  observation = response_text;
                }
              }
              if (final_response.empty())
              {
                final_response = "Max steps reached without completion.";
              }
            }
            // v0.6.6: Native tool calling when agent has skills attached
            else if (agent->skills && !agent->skills->items.empty())
            {
              const auto tool_defs = collect_agent_tools();
              if (tool_defs.empty())
              {
                // Skills exist but none are valid — fall back to plain chat
                std::string user_prompt = query;
                if (!run_guard_chain(extension.guardchains, "on_tool_input", user_prompt))
                {
                  final_response = "Request blocked by guard policy.";
                }
                else
                {
                  final_response = run_llm(build_messages(
                      agent->system ? to_std_string(agent->system) : "", user_prompt));
                  if (!run_guard_chain(extension.guardchains, "on_tool_output", final_response))
                  {
                    final_response = "Response blocked by guard policy.";
                  }
                }
              }
              else
              {
                const int max_tool_steps = get_config_int("NEAM_MAX_TOOL_STEPS", 25, 1, 1000);
                const std::string system_prompt =
                    agent->system ? to_std_string(agent->system) : "";

                // Build initial message list
                std::vector<llm::Message> messages;
                if (!system_prompt.empty())
                {
                  messages.push_back({"system", system_prompt});
                }
                // Include RAG context if connected knowledge exists
                if (agent->connected_knowledge && !agent->connected_knowledge->items.empty())
                {
                  // Reuse the build_messages helper for the system/context parts only
                  auto full_msgs = build_messages(system_prompt, query);
                  messages = std::move(full_msgs);
                }
                else
                {
                  for (const auto& hist : agent->context->history)
                  {
                    messages.push_back({hist.role, hist.content});
                  }
                  // Note: query is already in history (added at line ~1607)
                }

                // Guard check on input
                std::string guarded_query = query;
                if (!run_guard_chain(extension.guardchains, "on_tool_input", guarded_query))
                {
                  final_response = "Request blocked by guard policy.";
                }
                else
                {
                  // Native tool call loop
                  for (int step = 0; step < max_tool_steps; ++step)
                  {
                    // Budget check before LLM call
                    if (!extension.budget.empty())
                    {
                      auto& tracker = get_budget_tracker(extension.budget);
                      if (tracker.is_exhausted())
                      {
                        final_response = "Budget exhausted: " + extension.budget;
                        break;
                      }
                      tracker.used_api_calls += 1.0;
                    }

                    emit_debug_event(DebugEventType::BeforeAgentAsk, "agent.ask.tools",
                                     frame.ip - 1, "step=" + std::to_string(step));

                    auto chat_resp = provider->chat_with_tools(messages, tool_defs, "auto");
                    trace_logger_.log_llm_output(agent_name, chat_resp.text);

                    // Budget: estimate token usage
                    if (!extension.budget.empty())
                    {
                      auto& tracker = get_budget_tracker(extension.budget);
                      const double token_estimate =
                          std::max(1.0, static_cast<double>(chat_resp.text.size()) / 4.0);
                      tracker.used_tokens += token_estimate;
                    }

                    // If no tool calls, we're done
                    if (!chat_resp.has_tool_calls())
                    {
                      final_response = chat_resp.text;
                      if (!run_guard_chain(extension.guardchains, "on_tool_output",
                                           final_response))
                      {
                        final_response = "Response blocked by guard policy.";
                      }
                      break;
                    }

                    // Add assistant response with tool calls to message history.
                    // content_blocks stores only tool call info (type:"function");
                    // text content goes in msg.content for the adapter to handle.
                    nlohmann::json assistant_blocks = nlohmann::json::array();
                    for (const auto& tc : chat_resp.tool_calls)
                    {
                      assistant_blocks.push_back(
                          {{"type", "function"},
                           {"id", tc.id},
                           {"name", tc.name},
                           {"arguments", tc.input.dump()}});
                    }
                    llm::Message assistant_msg;
                    assistant_msg.role = "assistant";
                    assistant_msg.content = chat_resp.text;
                    assistant_msg.content_blocks = std::move(assistant_blocks);
                    messages.push_back(std::move(assistant_msg));

                    // Execute each tool call
                    for (const auto& tc : chat_resp.tool_calls)
                    {
                      std::string result_content;
                      bool is_error = false;

                      ObjSkill* skill = find_agent_skill(tc.name);
                      if (!skill || (!skill->impl && !skill->external))
                      {
                        result_content = "Unknown tool: " + tc.name;
                        is_error = true;
                      }
                      else
                      {
                        // Check allowed skills
                        if (!allowed_skills_.empty() && allowed_skills_.count(tc.name) == 0)
                        {
                          result_content = "Permission denied: skill '" + tc.name +
                                           "' is not in the allowed skills list";
                          is_error = true;
                        }
                        else
                        {
                          // Check capabilities via tool def
                          ToolDef* tool = get_tool_def(tc.name);
                          bool cap_ok = true;
                          if (tool)
                          {
                            for (const auto& required : tool->capabilities)
                            {
                              if (!has_capability(agent_name, required))
                              {
                                result_content = "Missing capability: " + required;
                                is_error = true;
                                cap_ok = false;
                                break;
                              }
                            }
                            if (cap_ok)
                            {
                              for (const auto& cost : tool->budget_costs)
                              {
                                if (!consume_budget(extension.budget, cost.first, cost.second))
                                {
                                  result_content = "Budget exhausted for: " + cost.first;
                                  is_error = true;
                                  cap_ok = false;
                                  break;
                                }
                              }
                            }
                          }

                          if (cap_ok)
                          {
                            // Convert JSON arguments to Value arguments
                            std::vector<Value> args;
                            args.reserve(skill->param_names.size());
                            for (const auto& param_name : skill->param_names)
                            {
                              if (tc.input.contains(param_name))
                              {
                                args.push_back(json_to_value(tc.input.at(param_name)));
                              }
                              else
                              {
                                args.push_back(Value::Nil());
                              }
                            }

                            // Validate and log
                            trace_logger_.log_tool_call("vm", tc.name,
                                                        build_redacted_args(skill, args));
                            emit_debug_event(DebugEventType::BeforeToolExecution,
                                             "skill:" + tc.name, frame.ip - 1, {});

                            try
                            {
                              validate_skill_args(skill, args);

                              // Guard check on tool input
                              std::string guard_input = tc.input.dump();
                              if (tool && !run_guard_chain(tool->guards, "on_tool_input",
                                                           guard_input))
                              {
                                result_content = "Request blocked by guard policy.";
                                is_error = true;
                              }
                              else if (skill->external)
                              {
                                // v0.6.8: External skill dispatch with MCP client lookup
                                McpClient* mcp_ptr = nullptr;
                                if (skill->external->binding == SkillBinding::McpTool)
                                {
                                  auto mcp_it = mcp_clients_.find(skill->external->mcp_server_name);
                                  if (mcp_it != mcp_clients_.end())
                                  {
                                    mcp_ptr = mcp_it->second.get();
                                  }
                                }
                                result_content = dispatch_external_skill(skill, tc.input, mcp_ptr);

                                // Guard check on tool output
                                if (tool && !run_guard_chain(tool->guards, "on_tool_output",
                                                             result_content))
                                {
                                  result_content = "Response blocked by guard policy.";
                                  is_error = true;
                                }
                              }
                              else
                              {
                                Value result = call_function(skill->impl, args, true, tc.name);
                                result_content = value_to_string(result);

                                // Guard check on tool output
                                if (tool && !run_guard_chain(tool->guards, "on_tool_output",
                                                             result_content))
                                {
                                  result_content = "Response blocked by guard policy.";
                                  is_error = true;
                                }
                              }
                            }
                            catch (const SchemaViolationError& e)
                            {
                              result_content = std::string("Schema error: ") + e.what();
                              is_error = true;
                            }
                            catch (const std::exception& e)
                            {
                              result_content = std::string("Tool error: ") + e.what();
                              is_error = true;
                            }
                          }
                        }
                      }

                      // Add tool result message
                      llm::Message tool_msg;
                      tool_msg.role = "tool";
                      tool_msg.content = result_content;
                      tool_msg.content_blocks = nlohmann::json::array();
                      tool_msg.content_blocks.push_back(
                          {{"tool_call_id", tc.id},
                           {"content", result_content},
                           {"is_error", is_error}});
                      messages.push_back(std::move(tool_msg));
                    }

                    // Check if we've exceeded step limit
                    if (step == max_tool_steps - 1)
                    {
                      final_response = "Max tool call steps reached without completion.";
                    }
                  }
                }
              }
            }
            else
            {
              std::string user_prompt = query;
              if (!run_guard_chain(extension.guardchains, "on_tool_input", user_prompt))
              {
                final_response = "Request blocked by guard policy.";
              }
              else
              {
                const auto response_text = run_llm(build_messages(
                    agent->system ? to_std_string(agent->system) : "", user_prompt));
                final_response = response_text;
                if (!run_guard_chain(extension.guardchains, "on_tool_output", final_response))
                {
                  final_response = "Response blocked by guard policy.";
                }
              }
            }

            agent->context->history.push_back({"assistant", final_response});
            if (!extension.memory.empty())
            {
              auto& store = memory_stores_[extension.memory];
              store.events.push_back(
                  MemoryEvent{current_time_ms(), "assistant", final_response, agent_name});
            }

            // v0.6.9 D9: Record behavioral metrics and check for anomalies
            {
              auto agent_elapsed_ms = static_cast<double>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - agent_exec_start)
                      .count());
              auto& monitor = security::BehavioralMonitor::instance();
              monitor.record_request(agent_name, agent_tool_call_count,
                                     agent_elapsed_ms, agent_tools_used);
              auto anomaly = monitor.check_anomaly(agent_name, agent_tool_call_count,
                                                    agent_tools_used);
              if (anomaly.anomalous)
              {
                security::AuditLogger::instance().log({
                    {},
                    security::TraceContext::current(),
                    security::EventType::AnomalyDetected,
                    agent_name,
                    {},
                    "Behavioral anomaly: " + anomaly.reason,
                    {{"deviation_score", anomaly.deviation_score},
                     {"tool_calls", agent_tool_call_count},
                     {"elapsed_ms", agent_elapsed_ms}}});
              }
            }

            trace_logger_.log_llm_output(agent_name, final_response);
            stack_.push_back(
                Value::String(final_response.c_str(), final_response.size()));
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
          else if (method == "len")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("list.len expects no arguments");
            }
            stack_.push_back(Value::Number(static_cast<double>(list->items.size())));
          }
          else if (method == "get")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("list.get expects 1 argument");
            }
            if (!args[0].is_number())
            {
              throw std::runtime_error("list.get expects numeric index");
            }
            const auto index = static_cast<int64_t>(args[0].as_number());
            if (index < 0 || static_cast<std::size_t>(index) >= list->items.size())
            {
              stack_.push_back(Value::Nil());
            }
            else
            {
              stack_.push_back(list->items[static_cast<std::size_t>(index)]);
            }
          }
          else if (method == "set")
          {
            if (arg_count != 2)
            {
              throw std::runtime_error("list.set expects 2 arguments");
            }
            if (!args[0].is_number())
            {
              throw std::runtime_error("list.set expects numeric index");
            }
            const auto index = static_cast<int64_t>(args[0].as_number());
            if (index < 0 || static_cast<std::size_t>(index) >= list->items.size())
            {
              throw std::runtime_error("list.set index out of range");
            }
            list->items[static_cast<std::size_t>(index)] = args[1];
            stack_.push_back(Value::List(list));
          }
          else if (method == "concat")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("list.concat expects 1 argument");
            }
            if (!args[0].is_list())
            {
              throw std::runtime_error("list.concat expects list");
            }
            auto* other = as_list(args[0]);
            std::vector<Value> items = list->items;
            items.insert(items.end(), other->items.begin(), other->items.end());
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "reverse")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("list.reverse expects no arguments");
            }
            std::reverse(list->items.begin(), list->items.end());
            stack_.push_back(Value::List(list));
          }
          else if (method == "sort")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("list.sort expects no arguments");
            }
            if (!list->items.empty())
            {
              const bool all_numbers =
                  std::all_of(list->items.begin(), list->items.end(),
                              [](const Value& value) { return value.is_number(); });
              const bool all_strings =
                  std::all_of(list->items.begin(), list->items.end(),
                              [](const Value& value) { return value.is_string(); });
              if (!all_numbers && !all_strings)
              {
                throw std::runtime_error("list.sort expects all numbers or all strings");
              }
              if (all_numbers)
              {
                std::sort(list->items.begin(), list->items.end(),
                          [](const Value& a, const Value& b) {
                            return a.as_number() < b.as_number();
                          });
              }
              else
              {
                std::sort(list->items.begin(), list->items.end(),
                          [](const Value& a, const Value& b) {
                            auto* left = as_string(a);
                            auto* right = as_string(b);
                            const std::string lhs(left->chars, left->length);
                            const std::string rhs(right->chars, right->length);
                            return lhs < rhs;
                          });
              }
            }
            stack_.push_back(Value::List(list));
          }
          else if (method == "find")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("list.find expects 1 argument");
            }
            bool found = false;
            for (const auto& item : list->items)
            {
              Value call_args[] = {item};
              Value result = call_native(args[0], 1, call_args);
              if (is_truthy(result))
              {
                stack_.push_back(item);
                found = true;
                break;
              }
            }
            if (!found)
            {
              stack_.push_back(Value::Nil());
            }
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
          else if (method == "values")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("map.values expects no arguments");
            }
            std::vector<Value> items;
            items.reserve(map->entries.size());
            for (const auto& entry : map->entries)
            {
              items.push_back(entry.second);
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "contains")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("map.contains expects 1 argument");
            }
            std::string key = to_std_string(args[0]);
            stack_.push_back(Value::Bool(map->entries.find(key) != map->entries.end()));
          }
          else if (method == "remove")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("map.remove expects 1 argument");
            }
            std::string key = to_std_string(args[0]);
            auto it = map->entries.find(key);
            if (it == map->entries.end())
            {
              stack_.push_back(Value::Nil());
            }
            else
            {
              Value value = it->second;
              map->entries.erase(it);
              stack_.push_back(std::move(value));
            }
          }
          else if (method == "size")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("map.size expects no arguments");
            }
            stack_.push_back(Value::Number(static_cast<double>(map->entries.size())));
          }
          else if (method == "clear")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("map.clear expects no arguments");
            }
            map->entries.clear();
            stack_.push_back(Value::Map(map));
          }
          else if (method == "entries")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("map.entries expects no arguments");
            }
            std::vector<Value> items;
            items.reserve(map->entries.size());
            for (const auto& entry : map->entries)
            {
              std::unordered_map<std::string, Value> out_entry;
              out_entry.emplace("key", Value::String(entry.first.c_str(), entry.first.size()));
              out_entry.emplace("value", entry.second);
              items.push_back(Value::Map(new_map(std::move(out_entry))));
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
          else if (method == "trim")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("string.trim expects no arguments");
            }
            const auto is_space = [](unsigned char c) { return std::isspace(c); };
            std::size_t start = 0;
            std::size_t end = base.size();
            while (start < end && is_space(static_cast<unsigned char>(base[start])))
            {
              ++start;
            }
            while (end > start && is_space(static_cast<unsigned char>(base[end - 1])))
            {
              --end;
            }
            const std::string trimmed = base.substr(start, end - start);
            stack_.push_back(Value::String(trimmed.c_str(), trimmed.size()));
          }
          else if (method == "to_upper")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("string.to_upper expects no arguments");
            }
            std::string out = base;
            for (char& c : out)
            {
              c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            stack_.push_back(Value::String(out.c_str(), out.size()));
          }
          else if (method == "to_lower")
          {
            if (arg_count != 0)
            {
              throw std::runtime_error("string.to_lower expects no arguments");
            }
            std::string out = base;
            for (char& c : out)
            {
              c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            stack_.push_back(Value::String(out.c_str(), out.size()));
          }
          else if (method == "starts_with")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("string.starts_with expects 1 argument");
            }
            const std::string prefix = to_std_string(args[0]);
            stack_.push_back(Value::Bool(base.rfind(prefix, 0) == 0));
          }
          else if (method == "ends_with")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("string.ends_with expects 1 argument");
            }
            const std::string suffix = to_std_string(args[0]);
            if (suffix.size() > base.size())
            {
              stack_.push_back(Value::Bool(false));
            }
            else
            {
              const bool matches = base.compare(base.size() - suffix.size(), suffix.size(), suffix) == 0;
              stack_.push_back(Value::Bool(matches));
            }
          }
          else if (method == "contains")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("string.contains expects 1 argument");
            }
            const std::string needle = to_std_string(args[0]);
            stack_.push_back(Value::Bool(base.find(needle) != std::string::npos));
          }
          else if (method == "substring")
          {
            if (arg_count != 2)
            {
              throw std::runtime_error("string.substring expects 2 arguments");
            }
            if (!args[0].is_number() || !args[1].is_number())
            {
              throw std::runtime_error("string.substring expects numeric arguments");
            }
            const auto start = static_cast<std::size_t>(std::max(0.0, args[0].as_number()));
            const auto length = static_cast<std::size_t>(std::max(0.0, args[1].as_number()));
            if (start >= base.size())
            {
              stack_.push_back(Value::String("", 0));
            }
            else
            {
              const std::string slice = base.substr(start, length);
              stack_.push_back(Value::String(slice.c_str(), slice.size()));
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
        const auto count = read_short(code, frame.ip);
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
        const auto count = read_short(code, frame.ip);
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
        Value sensitive_value = pop();
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
        skill->sensitive = is_truthy(sensitive_value);
        globals_.set(name, Value::Skill(skill));
        break;
      }
      // v0.6.7: Define an external skill (MCP, HTTP, or Claude built-in)
      case OpCode::OP_DEFINE_EXTERN_SKILL:
      {
        Value sensitive_value = pop();
        Value binding_config_value = pop();
        Value binding_type_value = pop();
        Value params_value = pop();
        Value param_order_value = pop();
        Value description_value = pop();
        Value name_value = pop();

        auto* name = as_string(name_value);
        auto* description = as_string(description_value);

        if (!is_obj_type(params_value, ObjType::OBJ_MAP))
        {
          throw std::runtime_error("Extern skill params must be map");
        }
        if (!is_obj_type(param_order_value, ObjType::OBJ_LIST))
        {
          throw std::runtime_error("Extern skill param order must be list");
        }
        if (!is_obj_type(binding_config_value, ObjType::OBJ_MAP))
        {
          throw std::runtime_error("Extern skill binding config must be map");
        }

        auto* params = as_map(params_value);
        auto* param_order = as_list(param_order_value);
        auto* binding_config = as_map(binding_config_value);

        std::vector<std::string> param_names;
        param_names.reserve(param_order->items.size());
        for (const auto& item : param_order->items)
        {
          param_names.push_back(to_std_string(item));
        }

        // Build ExternalSkillConfig from binding type + config map
        auto* ext = new ExternalSkillConfig();
        const std::string binding_type_str = to_std_string(binding_type_value);

        auto map_str = [&](const std::string& key) -> std::string {
          auto it = binding_config->entries.find(key);
          if (it != binding_config->entries.end() && it->second.is_string())
          {
            return to_std_string(it->second);
          }
          return {};
        };

        if (binding_type_str == "mcp")
        {
          ext->binding = SkillBinding::McpTool;
          ext->mcp_server_name = map_str("server");
          ext->mcp_tool_name = map_str("tool");
        }
        else if (binding_type_str == "http")
        {
          ext->binding = SkillBinding::HttpApi;
          ext->http_method = map_str("method");
          ext->http_url_template = map_str("url");
          ext->http_body_template = map_str("body_template");
          ext->http_response_path = map_str("response_path");
          // Parse timeout if present
          auto timeout_it = binding_config->entries.find("timeout");
          if (timeout_it != binding_config->entries.end() &&
              timeout_it->second.is_number())
          {
            ext->http_timeout_ms =
                static_cast<long>(timeout_it->second.as_number());
          }
          // Parse headers list if present
          auto headers_it = binding_config->entries.find("headers");
          if (headers_it != binding_config->entries.end() &&
              headers_it->second.is_list())
          {
            auto* hlist = as_list(headers_it->second);
            for (const auto& h : hlist->items)
            {
              if (h.is_string())
              {
                std::string header_str = to_std_string(h);
                auto colon_pos = header_str.find(':');
                if (colon_pos != std::string::npos)
                {
                  auto key = header_str.substr(0, colon_pos);
                  auto value = header_str.substr(colon_pos + 1);
                  auto start = value.find_first_not_of(" \t");
                  if (start != std::string::npos)
                  {
                    value = value.substr(start);
                  }
                  ext->http_headers.emplace_back(std::move(key), std::move(value));
                }
              }
            }
          }
        }
        else if (binding_type_str == "claude_builtin")
        {
          ext->binding = SkillBinding::ClaudeBuiltin;
          ext->claude_tool_type = map_str("type");
        }
        else
        {
          delete ext;
          throw std::runtime_error("Unknown extern skill binding type: " + binding_type_str);
        }

        auto* skill = new_skill(name, description, params, std::move(param_names), nullptr);
        skill->external = ext;
        skill->sensitive = is_truthy(sensitive_value);
        globals_.set(name, Value::Skill(skill));
        break;
      }
      // v0.6.7: Define an MCP server connection
      // v0.6.8: Now launches a real MCP client process
      case OpCode::OP_DEFINE_MCP_SERVER:
      {
        Value config_value = pop();
        Value name_value = pop();
        if (!is_obj_type(config_value, ObjType::OBJ_MAP))
        {
          throw std::runtime_error("MCP server config must be map");
        }
        auto* name = as_string(name_value);
        std::string server_name(name->chars, name->length);

        // Store config in globals for reference
        std::string key = "__mcp_server__" + server_name;
        auto* key_str = copy_string(key.c_str(), key.size());
        globals_.set(key_str, config_value);

        // v0.6.8: Extract command/args and create McpClient
        auto* config_map = as_map(config_value);
        auto map_val = [&](const std::string& k) -> std::string {
          auto it = config_map->entries.find(k);
          if (it != config_map->entries.end() && it->second.is_string())
          {
            return to_std_string(it->second);
          }
          return {};
        };

        std::string command = map_val("command");
        std::vector<std::string> args;
        auto args_it = config_map->entries.find("args");
        if (args_it != config_map->entries.end() && args_it->second.is_list())
        {
          auto* args_list = as_list(args_it->second);
          for (const auto& item : args_list->items)
          {
            if (item.is_string())
            {
              args.push_back(to_std_string(item));
            }
          }
        }

        std::unordered_map<std::string, std::string> env;
        auto env_it = config_map->entries.find("env");
        if (env_it != config_map->entries.end() && env_it->second.is_map())
        {
          auto* env_map = as_map(env_it->second);
          for (const auto& [ek, ev] : env_map->entries)
          {
            if (ev.is_string())
            {
              env[ek] = to_std_string(ev);
            }
          }
        }

        if (!command.empty())
        {
          auto client = std::make_unique<McpClient>(command, args, env);
          try
          {
            client->initialize();
            mcp_clients_[server_name] = std::move(client);
          }
          catch (const std::exception& e)
          {
            output_stream() << "[MCP] Warning: Failed to connect to server '"
                            << server_name << "': " << e.what() << "\n";
          }
        }
        break;
      }
      // v0.6.7: Adopt tools from an MCP server
      case OpCode::OP_ADOPT_MCP_TOOLS:
      {
        Value alias_value = pop();
        Value filter_value = pop();
        Value server_name_value = pop();

        const std::string server_name = to_std_string(server_name_value);

        // Look up MCP server config
        std::string config_key = "__mcp_server__" + server_name;
        auto* config_key_str = copy_string(config_key.c_str(), config_key.size());
        Value config_val;
        if (!globals_.get(config_key_str, &config_val))
        {
          throw std::runtime_error("Unknown MCP server: " + server_name);
        }

        // Determine alias prefix
        std::string prefix = server_name;
        if (!alias_value.is_nil() && alias_value.is_string())
        {
          prefix = to_std_string(alias_value);
        }

        // Get filter list (empty = adopt all)
        std::vector<std::string> filter;
        if (is_obj_type(filter_value, ObjType::OBJ_LIST))
        {
          auto* flist = as_list(filter_value);
          for (const auto& item : flist->items)
          {
            if (item.is_string())
            {
              filter.push_back(to_std_string(item));
            }
          }
        }

        // v0.6.8: Real MCP tool discovery via tools/list
        auto mcp_it = mcp_clients_.find(server_name);
        if (mcp_it != mcp_clients_.end() && mcp_it->second)
        {
          try
          {
            auto discovered_tools = mcp_it->second->list_tools();
            for (const auto& tool_info : discovered_tools)
            {
              // Apply filter: if filter is non-empty, only adopt listed tools
              if (!filter.empty())
              {
                bool in_filter = false;
                for (const auto& f : filter)
                {
                  if (f == tool_info.name)
                  {
                    in_filter = true;
                    break;
                  }
                }
                if (!in_filter)
                {
                  continue;
                }
              }

              std::string full_name = prefix + "." + tool_info.name;
              auto* skill_name = copy_string(full_name.c_str(), full_name.size());
              std::string desc = tool_info.description.empty()
                  ? ("MCP tool " + tool_info.name + " from server " + server_name)
                  : tool_info.description;
              auto* skill_desc = copy_string(desc.c_str(), desc.size());
              auto* skill_params = new_map({});
              auto* ext = new ExternalSkillConfig();
              ext->binding = SkillBinding::McpTool;
              ext->mcp_server_name = server_name;
              ext->mcp_tool_name = tool_info.name;
              auto* skill = new_skill(skill_name, skill_desc, skill_params, {}, nullptr);
              skill->external = ext;
              globals_.set(skill_name, Value::Skill(skill));
            }
          }
          catch (const std::exception& e)
          {
            output_stream() << "[MCP] Warning: tools/list failed for server '"
                            << server_name << "': " << e.what() << "\n";
          }
        }
        else if (!filter.empty())
        {
          // Fallback: create stubs for explicitly named tools
          for (const auto& tool_name : filter)
          {
            std::string full_name = prefix + "." + tool_name;
            auto* skill_name = copy_string(full_name.c_str(), full_name.size());
            std::string desc = "MCP tool " + tool_name + " from server " + server_name;
            auto* skill_desc = copy_string(desc.c_str(), desc.size());
            auto* skill_params = new_map({});
            auto* ext = new ExternalSkillConfig();
            ext->binding = SkillBinding::McpTool;
            ext->mcp_server_name = server_name;
            ext->mcp_tool_name = tool_name;
            auto* skill = new_skill(skill_name, skill_desc, skill_params, {}, nullptr);
            skill->external = ext;
            globals_.set(skill_name, Value::Skill(skill));
          }
        }
        break;
      }
      case OpCode::OP_DEFINE_KNOWLEDGE:
      {
        // Pop values in reverse order they were pushed
        Value strategy_options_value = pop();
        Value retrieval_strategy_value = pop();
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

        // Parse retrieval strategy
        RetrievalStrategy strategy = RetrievalStrategy::kBasic;
        if (retrieval_strategy_value.is_number())
        {
          strategy = static_cast<RetrievalStrategy>(static_cast<int>(retrieval_strategy_value.as_number()));
        }

        // Parse strategy options
        RetrievalStrategyOptions options;
        if (is_obj_type(strategy_options_value, ObjType::OBJ_MAP))
        {
          auto* opts_map = as_map(strategy_options_value);
          auto get_number = [&](const std::string& key, double default_val) -> double {
            auto it = opts_map->entries.find(key);
            if (it != opts_map->entries.end() && it->second.is_number())
            {
              return it->second.as_number();
            }
            return default_val;
          };
          auto get_bool = [&](const std::string& key, bool default_val) -> bool {
            auto it = opts_map->entries.find(key);
            if (it != opts_map->entries.end() && it->second.is_bool())
            {
              return it->second.as_bool();
            }
            return default_val;
          };

          options.top_k = static_cast<std::size_t>(get_number("top_k", 4));
          options.relevance_threshold = get_number("relevance_threshold", 0.5);
          options.mmr_lambda = get_number("mmr_lambda", 0.5);
          options.num_hypothetical = static_cast<std::size_t>(get_number("num_hypothetical", 1));
          options.enable_relevance_check = get_bool("enable_relevance_check", true);
          options.enable_support_check = get_bool("enable_support_check", true);
          options.enable_web_fallback = get_bool("enable_web_fallback", false);
          options.enable_query_decomposition = get_bool("enable_query_decomposition", true);
          options.max_corrections = static_cast<std::size_t>(get_number("max_corrections", 2));
          options.max_iterations = static_cast<std::size_t>(get_number("max_iterations", 5));
          options.enable_reflection = get_bool("enable_reflection", true);
          options.search_depth = static_cast<std::size_t>(get_number("search_depth", 2));
          options.include_communities = get_bool("include_communities", true);
        }

        auto sources = parse_sources_list(sources_value);
        auto* knowledge_obj =
            new_knowledge(name, vector_store, embedding_model, chunk_size, chunk_overlap,
                          std::move(sources), strategy, options);
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
        Value connector_value = pop();
        Value plan_value = pop();
        Value world_model_value = pop();
        Value memory_value = pop();
        Value env_value = pop();
        Value budget_value = pop();
        Value policy_value = pop();  // v0.6.9
        Value guardchains_value = pop();
        Value required_caps_value = pop();
        Value knowledge_value = pop();
        Value skills_value = pop();
        Value system_value = pop();
        Value temperature_value = pop();
        Value api_key_env_value = pop();
        Value endpoint_value = pop();
        Value model_value = pop();
        Value provider_value = pop();
        Value name_value = pop();

        if (!skills_value.is_list() || !knowledge_value.is_list() ||
            !required_caps_value.is_list() || !guardchains_value.is_list())
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

        AgentExtension extension;
        for (const auto& cap : as_list(required_caps_value)->items)
        {
          extension.required_capabilities.push_back(to_std_string(cap));
        }
        for (const auto& guard : as_list(guardchains_value)->items)
        {
          extension.guardchains.push_back(to_std_string(guard));
        }
        if (policy_value.is_string())
        {
          extension.policy = to_std_string(policy_value);
        }
        if (budget_value.is_string())
        {
          extension.budget = to_std_string(budget_value);
        }
        if (env_value.is_string())
        {
          extension.env = to_std_string(env_value);
        }
        if (memory_value.is_string())
        {
          extension.memory = to_std_string(memory_value);
        }
        if (world_model_value.is_string())
        {
          extension.world_model = to_std_string(world_model_value);
        }
        if (plan_value.is_string())
        {
          extension.plan = to_std_string(plan_value);
        }
        if (connector_value.is_string())
        {
          extension.connector = to_std_string(connector_value);
        }
        agent_extensions_[to_std_string(name)] = std::move(extension);
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
        if (frames_.size() <= target_frame_count)
        {
          if (target_frame_count == 0)
          {
            trace_logger_.log_end();
          }
          return result;
        }
        stack_.push_back(result);
        break;
      }
      case OpCode::OP_GRANT:
      {
        Value target_value = pop();
        Value capability_value = pop();
        const std::string target = to_std_string(target_value);
        const std::string capability_name = to_std_string(capability_value);
        std::string pattern = capability_name;
        if (const Value* capability_def = find_global_value(globals_, capability_name))
        {
          if (capability_def->is_map())
          {
            auto* map = as_map(*capability_def);
            const auto defined_pattern = map_string_value(map, "pattern");
            if (!defined_pattern.empty())
            {
              pattern = defined_pattern;
            }
          }
        }
        entity_capabilities_[target].insert(pattern);
        break;
      }
      case OpCode::OP_CHECKPOINT:
      {
        Value label_value = pop();
        std::string label;
        if (label_value.is_string())
        {
          label = to_std_string(label_value);
        }
        auto& store = memory_stores_["global"];
        store.checkpoints.push_back(store.events.size());
        if (!label.empty())
        {
          store.labeled_checkpoints[label] = store.events.size();
        }
        break;
      }
      case OpCode::OP_REWIND:
      {
        Value target_value = pop();
        auto& store = memory_stores_["global"];
        if (target_value.is_string())
        {
          const auto label = to_std_string(target_value);
          auto it = store.labeled_checkpoints.find(label);
          if (it != store.labeled_checkpoints.end())
          {
            store.events.resize(it->second);
          }
          break;
        }
        if (target_value.is_number())
        {
          const auto index = static_cast<std::size_t>(target_value.as_number());
          if (index <= store.events.size())
          {
            store.events.resize(index);
          }
          break;
        }
        if (!store.checkpoints.empty())
        {
          store.events.resize(store.checkpoints.back());
          store.checkpoints.pop_back();
        }
        break;
      }
      default:
        throw std::runtime_error("Unknown opcode encountered");
    }
  }

  return Value::Nil();
}

Value VirtualMachine::call_function(ObjFunction* fn, const std::vector<Value>& args, bool is_tool,
                                    const std::string& tool_name)
{
  if (!fn)
  {
    return Value::Nil();
  }
  const std::size_t target_frame_count = frames_.size();
  const std::size_t stack_start = stack_.size();
  for (const auto& arg : args)
  {
    stack_.push_back(arg);
  }
  frames_.push_back(CallFrame{&fn->chunk, fn, 0, stack_start, is_tool, tool_name});
  return run_frames(target_frame_count);
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
