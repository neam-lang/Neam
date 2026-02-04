// SPDX-License-Identifier: Apache-2.0
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

#include "neamc/vm/health_manager.hpp"
#include "neamc/vm/telemetry.hpp"
#include "neamc/vm/llm_gateway.hpp"
#include "neamc/llm/cost_table.hpp"
#include "neamc/llm/embedding_provider.hpp"
#include "neamc/llm/provider_factory.hpp"
#include "neamc/vm/async/executor.hpp"
#include "neamc/vm/async/future.hpp"
#include "neamc/vm/knowledge.hpp"
#include "neamc/vm/schema.hpp"
#include "neamc/voice/pipeline.hpp"
#include "neamc/voice/realtime.hpp"
#include "neamc/voice/stt.hpp"
#include "neamc/voice/tts.hpp"

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
    auto content_it = map->entries.find("content");
    if (type_it == map->entries.end() || (path_it == map->entries.end() && content_it == map->entries.end()))
    {
      throw std::runtime_error("Knowledge source missing type and path/content");
    }
    knowledge::Source src;
    src.type = to_std_string(type_it->second);
    if (path_it != map->entries.end())
    {
      src.path = to_std_string(path_it->second);
    }
    if (content_it != map->entries.end())
    {
      src.content = to_std_string(content_it->second);
    }
    sources.push_back(std::move(src));
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

Value json_to_value(const nlohmann::json& json)
{
  if (json.is_null())
  {
    return Value::Nil();
  }
  if (json.is_boolean())
  {
    return Value::Bool(json.get<bool>());
  }
  if (json.is_number())
  {
    return Value::Number(json.get<double>());
  }
  if (json.is_string())
  {
    const auto s = json.get<std::string>();
    return Value::String(s.c_str(), s.size());
  }
  if (json.is_array())
  {
    std::vector<Value> items;
    items.reserve(json.size());
    for (const auto& elem : json)
    {
      items.push_back(json_to_value(elem));
    }
    return Value::List(new_list(std::move(items)));
  }
  if (json.is_object())
  {
    std::unordered_map<std::string, Value> entries;
    for (auto& [key, val] : json.items())
    {
      entries[key] = json_to_value(val);
    }
    return Value::Map(new_map(std::move(entries)));
  }
  return Value::Nil();
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
    case OpCode::OP_MOD:
      return "OP_MOD";
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
    case OpCode::OP_AWAIT_ALL:
      return "OP_AWAIT_ALL";
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
    // Agentic orchestration opcodes
    case OpCode::OP_DEFINE_HANDOFF:
      return "OP_DEFINE_HANDOFF";
    case OpCode::OP_DEFINE_AGENT_CARD:
      return "OP_DEFINE_AGENT_CARD";
    case OpCode::OP_DEFINE_TASK:
      return "OP_DEFINE_TASK";
    case OpCode::OP_DEFINE_RUNNER:
      return "OP_DEFINE_RUNNER";
    case OpCode::OP_EXECUTE_HANDOFF:
      return "OP_EXECUTE_HANDOFF";
    case OpCode::OP_CREATE_TASK:
      return "OP_CREATE_TASK";
    case OpCode::OP_SUBMIT_TASK:
      return "OP_SUBMIT_TASK";
    case OpCode::OP_GET_TASK_STATUS:
      return "OP_GET_TASK_STATUS";
    case OpCode::OP_RUN_AGENT_LOOP:
      return "OP_RUN_AGENT_LOOP";
    case OpCode::OP_TRY_BEGIN:
      return "OP_TRY_BEGIN";
    case OpCode::OP_TRY_END:
      return "OP_TRY_END";
    case OpCode::OP_THROW:
      return "OP_THROW";
    case OpCode::OP_DEFINE_VOICE_PIPELINE:
      return "OP_DEFINE_VOICE_PIPELINE";
    case OpCode::OP_VOICE_TRANSCRIBE:
      return "OP_VOICE_TRANSCRIBE";
    case OpCode::OP_VOICE_SYNTHESIZE:
      return "OP_VOICE_SYNTHESIZE";
    case OpCode::OP_VOICE_PIPELINE_RUN:
      return "OP_VOICE_PIPELINE_RUN";
    case OpCode::OP_DEFINE_REALTIME_VOICE:
      return "OP_DEFINE_REALTIME_VOICE";
    case OpCode::OP_REALTIME_CONNECT:
      return "OP_REALTIME_CONNECT";
    case OpCode::OP_REALTIME_SEND_AUDIO:
      return "OP_REALTIME_SEND_AUDIO";
    case OpCode::OP_REALTIME_SEND_TEXT:
      return "OP_REALTIME_SEND_TEXT";
    case OpCode::OP_REALTIME_ON_EVENT:
      return "OP_REALTIME_ON_EVENT";
    case OpCode::OP_REALTIME_TOOL_RESULT:
      return "OP_REALTIME_TOOL_RESULT";
    case OpCode::OP_REALTIME_CLOSE:
      return "OP_REALTIME_CLOSE";
    // Cognitive opcodes (v0.5.0)
    case OpCode::OP_DEFINE_REASONING:
      return "OP_DEFINE_REASONING";
    case OpCode::OP_DEFINE_REFLECTION:
      return "OP_DEFINE_REFLECTION";
    case OpCode::OP_DEFINE_LEARNING:
      return "OP_DEFINE_LEARNING";
    case OpCode::OP_DEFINE_GOALS:
      return "OP_DEFINE_GOALS";
    case OpCode::OP_DEFINE_EVOLUTION:
      return "OP_DEFINE_EVOLUTION";
    case OpCode::OP_DEFINE_INNER_MODEL:
      return "OP_DEFINE_INNER_MODEL";
    case OpCode::OP_AGENT_RATE:
      return "OP_AGENT_RATE";
    case OpCode::OP_AGENT_REFLECT:
      return "OP_AGENT_REFLECT";
    case OpCode::OP_AGENT_EVOLVE:
      return "OP_AGENT_EVOLVE";
    case OpCode::OP_AGENT_STATUS:
      return "OP_AGENT_STATUS";
  }
  return "OP_UNKNOWN";
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
  mcp_registry_ = std::make_unique<mcp::McpRegistry>();
  memory_backend_ = std::make_unique<InMemoryBackend>();
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
    case OpCode::OP_MOD:
      if (b == 0.0)
      {
        throw std::runtime_error("Division by zero");
      }
      result = std::fmod(a, b);
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
  exception_handlers_.clear();
  emitted_.clear();
  trace_logger_.start_run();
  trace_logger_.log_start();
  frames_.push_back(CallFrame{&chunk, nullptr, 0, 0});
  return run_frames(0);
}

Value VirtualMachine::run_frames(std::size_t target_frame_count)
{
  const bool trace = std::getenv("NEAM_TRACE") != nullptr;

  while (!frames_.empty())
  {
    try
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
      case OpCode::OP_MOD:
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
          if (frames_.size() >= 1024)
          {
            throw std::runtime_error("Stack overflow: maximum call depth (1024) exceeded");
          }
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
          if (frames_.size() >= 1024)
          {
            throw std::runtime_error("Stack overflow: maximum call depth (1024) exceeded");
          }
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

            const std::string agent_name = to_std_string(agent->name);
            const auto extension_it = agent_extensions_.find(agent_name);
            const AgentExtension extension =
                extension_it == agent_extensions_.end() ? AgentExtension{} : extension_it->second;

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
            if (!extension.memory.empty() && memory_backend_)
            {
              MemoryEventRecord record;
              record.timestamp = current_time_ms();
              record.type = "user";
              record.data = query;
              record.agent = agent_name;
              memory_backend_->store_event(extension.memory, record);
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
                        return false;
                      }
                      value = output;
                    }
                  }
                }
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
              return !tracker.is_exhausted();
            };

            auto run_tool = [&](const std::string& tool_name,
                                const std::vector<std::string>& arg_texts) -> std::string {
              // Check for MCP tool (dotted name: "server.tool")
              auto dot_pos = tool_name.find('.');
              if (dot_pos != std::string::npos && mcp_registry_)
              {
                std::string srv = tool_name.substr(0, dot_pos);
                std::string tname = tool_name.substr(dot_pos + 1);
                if (mcp_registry_->has_server(srv))
                {
                  try
                  {
                    // Build arguments JSON from arg_texts
                    nlohmann::json args_json;
                    for (std::size_t i = 0; i < arg_texts.size(); ++i)
                    {
                      try
                      {
                        args_json["arg" + std::to_string(i)] = nlohmann::json::parse(arg_texts[i]);
                      }
                      catch (...)
                      {
                        args_json["arg" + std::to_string(i)] = arg_texts[i];
                      }
                    }
                    auto result = mcp_registry_->call_tool(srv, tname, args_json);
                    return result.is_string() ? result.get<std::string>() : result.dump();
                  }
                  catch (const std::exception& e)
                  {
                    return std::string("MCP tool error: ") + e.what();
                  }
                }
              }

              ToolDef* tool = get_tool_def(tool_name);
              if (!tool || !tool->impl)
              {
                return "Unknown tool: " + tool_name;
              }

              for (const auto& required : tool->capabilities)
              {
                if (!has_capability(agent_name, required))
                {
                  return "Missing capability: " + required;
                }
              }

              for (const auto& cost : tool->budget_costs)
              {
                if (!consume_budget(extension.budget, cost.first, cost.second))
                {
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

              std::string guard_input = input_payload;
              if (!run_guard_chain(tool->guards, "on_tool_input", guard_input))
              {
                return "Request blocked by guard policy.";
              }

              std::vector<Value> tool_args;
              tool_args.reserve(arg_texts.size());
              for (const auto& arg : arg_texts)
              {
                tool_args.push_back(Value::String(arg.c_str(), arg.size()));
              }
              Value result = call_function(tool->impl, tool_args, true, tool_name);
              std::string output = value_to_string(result);

              if (!run_guard_chain(tool->guards, "on_tool_output", output))
              {
                return "Response blocked by guard policy.";
              }
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
                  return "Budget exhausted: " + extension.budget;
                }
                tracker.used_api_calls += 1.0;
              }

              // Use chat_with_tools to get ChatResult with usage info
              const auto start = std::chrono::steady_clock::now();
              llm::ChatResult chat_result = provider->chat_with_tools(messages, {});
              const auto end = std::chrono::steady_clock::now();
              const auto latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

              // Estimate cost
              const double cost = llm::estimate_cost(config.model, chat_result.usage);

              // Log to trace
              trace_logger_.log_llm_call(agent_name, config.model, provider_name,
                                         chat_result.usage.prompt_tokens,
                                         chat_result.usage.completion_tokens,
                                         chat_result.usage.total_tokens,
                                         cost, static_cast<int64_t>(latency_ms));

              // Update budget with real token counts
              if (!extension.budget.empty())
              {
                auto& tracker = get_budget_tracker(extension.budget);
                if (chat_result.usage.total_tokens > 0)
                {
                  tracker.used_tokens += static_cast<double>(chat_result.usage.total_tokens);
                }
                else
                {
                  // Fallback estimate if provider didn't return usage
                  const double token_estimate =
                      std::max(1.0, static_cast<double>(chat_result.content.size()) / 4.0);
                  tracker.used_tokens += token_estimate;
                }
                tracker.used_cost += cost;
              }
              return chat_result.content;
            };

            auto build_messages = [&](const std::string& system_prompt,
                                      const std::string& user_prompt) {
              std::vector<llm::Message> messages;
              if (!system_prompt.empty())
              {
                messages.push_back({"system", system_prompt});
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
                      knowledge_obj->store, user_prompt, kb_strategy, kb_options, llm_callback,
                      knowledge_obj->embed_callback);

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
                  messages.push_back({"system", combined_context});
                }
              }
              for (const auto& message : agent->context->history)
              {
                messages.push_back({message.role, message.content});
              }
              messages.push_back({"user", user_prompt});
              return messages;
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

            // Helper to build system prompt with context_from (Phase 6) + evolved prompt (v0.5.0)
            auto build_system_prompt = [&]() -> std::string {
              // Check for evolved prompt (v0.5.0)
              auto evolved_it = agent_evolved_prompts_.find(agent_name);
              std::string system_prompt = (evolved_it != agent_evolved_prompts_.end())
                  ? evolved_it->second
                  : (agent->system ? to_std_string(agent->system) : "");
              // Load context from AGENTS.md file if specified
              if (agent->context_from)
              {
                std::string context_path = to_std_string(agent->context_from);
                std::ifstream context_file(context_path);
                if (context_file.is_open())
                {
                  std::stringstream buffer;
                  buffer << context_file.rdbuf();
                  std::string context_content = buffer.str();
                  if (!context_content.empty())
                  {
                    std::string context_header = "## Project Context (from " + context_path + ")\n\n";
                    if (!system_prompt.empty())
                    {
                      system_prompt = context_header + context_content + "\n\n---\n\n" + system_prompt;
                    }
                    else
                    {
                      system_prompt = context_header + context_content;
                    }
                  }
                }
              }
              // Add structured output instruction when output_type is set
              if (!extension.output_type.empty())
              {
                system_prompt += "\n\nIMPORTANT: You MUST respond with valid JSON matching the type '"
                    + extension.output_type + "'. Do not include any text outside the JSON object.";
              }
              return system_prompt;
            };

            // Determine response format
            const std::string response_fmt = extension.output_type.empty() ? "" : "json_object";

            std::string final_response;
            if (plan_pattern_lower == "react")
            {
              const int max_steps = 10;
              std::string observation = query;
              for (int step = 0; step < max_steps; ++step)
              {
                std::string system_prompt = build_system_prompt();
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
            else
            {
              std::string user_prompt = query;
              if (!run_guard_chain(extension.guardchains, "on_tool_input", user_prompt))
              {
                final_response = "Request blocked by guard policy.";
              }
              else
              {
                // Build tool definitions from available tools for native function calling
                std::vector<llm::ToolDefinition> tool_defs;
                for (const auto& [tool_name_entry, tool_def_entry] : tools_)
                {
                  llm::ToolDefinition td;
                  td.name = tool_name_entry;
                  td.description = tool_def_entry.description;
                  // Build parameter schema from tool impl arity
                  nlohmann::json params;
                  params["type"] = "object";
                  params["properties"] = nlohmann::json::object();
                  params["required"] = nlohmann::json::array();
                  params["additionalProperties"] = false;
                  if (tool_def_entry.impl)
                  {
                    for (int pi = 0; pi < tool_def_entry.impl->arity; ++pi)
                    {
                      std::string pname = "arg" + std::to_string(pi);
                      params["properties"][pname] = {{"type", "string"}};
                      params["required"].push_back(pname);
                    }
                  }
                  td.parameters = std::move(params);
                  tool_defs.push_back(std::move(td));
                }

                // Add MCP server tools (Phase 3)
                if (mcp_registry_ && !extension.mcp_servers.empty())
                {
                  for (const auto& srv_name : extension.mcp_servers)
                  {
                    auto mcp_tools = mcp_registry_->server_tools(srv_name);
                    for (const auto& mt : mcp_tools)
                    {
                      llm::ToolDefinition td;
                      td.name = srv_name + "." + mt.name;
                      td.description = mt.description;
                      td.parameters = mt.input_schema.empty() ? nlohmann::json{{"type", "object"},
                          {"properties", nlohmann::json::object()}} : mt.input_schema;
                      tool_defs.push_back(std::move(td));
                    }
                  }
                }

                auto messages = build_messages(build_system_prompt(), user_prompt);

                if (tool_defs.empty() && response_fmt.empty())
                {
                  // No tools, no structured output — simple chat
                  // Use streaming when NEAM_STREAM=1 is set
                  const char* stream_env = std::getenv("NEAM_STREAM");
                  if (stream_env && std::string(stream_env) == "1")
                  {
                    // Streaming path: tokens are written to output as they arrive
                    std::ostringstream preview;
                    preview << "provider=" << provider_name << "\n";
                    preview << "model=" << config.model << "\n";
                    for (const auto& m : messages)
                    {
                      preview << "[" << m.role << "] " << m.content << "\n";
                    }
                    emit_debug_event(DebugEventType::BeforeAgentAsk, "agent.ask", frame.ip - 1,
                                     preview.str());

                    if (!extension.budget.empty())
                    {
                      auto& tracker = get_budget_tracker(extension.budget);
                      if (tracker.is_exhausted())
                      {
                        final_response = "Budget exhausted: " + extension.budget;
                      }
                    }
                    if (final_response.empty())
                    {
                      if (!extension.budget.empty())
                      {
                        get_budget_tracker(extension.budget).used_api_calls += 1.0;
                      }
                      final_response = provider->chat_stream(messages,
                          [this](const std::string& token) {
                            output_stream() << token;
                            output_stream().flush();
                          });
                      if (!extension.budget.empty())
                      {
                        auto& tracker = get_budget_tracker(extension.budget);
                        const double token_estimate =
                            std::max(1.0, static_cast<double>(final_response.size()) / 4.0);
                        tracker.used_tokens += token_estimate;
                      }
                    }
                  }
                  else
                  {
                    const auto response_text = run_llm(messages);
                    final_response = response_text;
                  }
                }
                else if (tool_defs.empty())
                {
                  // No tools but structured output requested
                  auto chat_result = provider->chat_with_tools(messages, {}, response_fmt);
                  final_response = chat_result.content;
                }
                else
                {
                  // Tool-calling loop
                  const int max_tool_rounds = 10;
                  for (int round = 0; round < max_tool_rounds; ++round)
                  {
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

                    auto chat_result = provider->chat_with_tools(messages, tool_defs, response_fmt);

                    if (!extension.budget.empty())
                    {
                      auto& tracker = get_budget_tracker(extension.budget);
                      const double token_estimate =
                          std::max(1.0, static_cast<double>(chat_result.content.size()) / 4.0);
                      tracker.used_tokens += token_estimate;
                    }

                    if (!chat_result.has_tool_calls())
                    {
                      // No tool calls — final response
                      final_response = chat_result.content;
                      break;
                    }

                    // Add assistant message with tool calls to conversation
                    // (for OpenAI, we need the assistant message referencing tool_calls)
                    messages.push_back({"assistant", chat_result.content});

                    // Execute each tool call and append results
                    for (const auto& tc : chat_result.tool_calls)
                    {
                      trace_logger_.log_tool_call(agent_name, tc.name, tc.arguments);

                      // Parse arguments from JSON
                      std::vector<std::string> arg_texts;
                      try
                      {
                        auto args_json = nlohmann::json::parse(tc.arguments);
                        if (args_json.is_object())
                        {
                          // Extract values in order (arg0, arg1, ...)
                          for (int ai = 0; ; ++ai)
                          {
                            std::string key = "arg" + std::to_string(ai);
                            if (!args_json.contains(key))
                            {
                              break;
                            }
                            auto& val = args_json[key];
                            arg_texts.push_back(val.is_string() ? val.get<std::string>() : val.dump());
                          }
                          // If no arg0/arg1 keys, try to extract values in order
                          if (arg_texts.empty())
                          {
                            for (auto& [k, v] : args_json.items())
                            {
                              arg_texts.push_back(v.is_string() ? v.get<std::string>() : v.dump());
                            }
                          }
                        }
                      }
                      catch (...)
                      {
                        arg_texts.push_back(tc.arguments);
                      }

                      const std::string tool_result = run_tool(tc.name, arg_texts);

                      // Add tool result message
                      llm::Message tool_msg;
                      tool_msg.role = "tool";
                      tool_msg.content = tool_result;
                      tool_msg.tool_call_id = tc.id;
                      tool_msg.name = tc.name;
                      messages.push_back(std::move(tool_msg));
                    }
                  }
                  if (final_response.empty())
                  {
                    final_response = "Max tool-calling rounds reached.";
                  }
                }

                if (!run_guard_chain(extension.guardchains, "on_tool_output", final_response))
                {
                  final_response = "Response blocked by guard policy.";
                }
              }
            }

            // Apply reflection (v0.5.0) — evaluate and potentially revise response
            if (extension.reflection_config.mode == ReflectionMode::kEachResponse)
            {
              try
              {
                llm::ProviderConfig ref_config;
                ref_config.model = to_std_string(agent->model);
                ref_config.endpoint = to_std_string(agent->endpoint);
                ref_config.api_key = to_std_string(agent->api_key_env);
                ref_config.temperature = agent->temperature;
                ref_config.default_host = resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST", "http://localhost:11434");
                std::string prov_name = to_std_string(agent->provider);
                if (!ref_config.api_key.empty())
                {
                  if (const char* ev = std::getenv(ref_config.api_key.c_str())) ref_config.api_key = ev;
                  else ref_config.api_key.clear();
                }
                auto ref_provider = llm::create_provider(prov_name, ref_config);
                final_response = apply_reflection(agent, ref_provider.get(), agent_name,
                                                   query, final_response, extension.reflection_config);
              }
              catch (...) { /* reflection failure is non-fatal */ }
            }

            // Record learning interaction (v0.5.0)
            if (extension.learning_config.strategy != LearningStrategy::kNone && memory_backend_)
            {
              nlohmann::json interaction;
              interaction["query"] = query;
              interaction["response"] = final_response;
              auto ref_it = agent_reflection_results_.find(agent_name);
              if (ref_it != agent_reflection_results_.end())
              {
                interaction["reflection"] = ref_it->second;
              }
              interaction["timestamp"] = current_time_ms();
              MemoryEventRecord learn_evt;
              learn_evt.timestamp = current_time_ms();
              learn_evt.type = "learning_interaction";
              learn_evt.data = interaction.dump();
              learn_evt.agent = agent_name;
              memory_backend_->store_event("learning_" + agent_name, learn_evt);

              // Check if review is due
              auto& count = learning_interaction_counts_[agent_name];
              ++count;
              if (count % extension.learning_config.review_interval == 0)
              {
                try
                {
                  llm::ProviderConfig lr_config;
                  lr_config.model = to_std_string(agent->model);
                  lr_config.endpoint = to_std_string(agent->endpoint);
                  lr_config.api_key = to_std_string(agent->api_key_env);
                  lr_config.temperature = agent->temperature;
                  lr_config.default_host = resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST", "http://localhost:11434");
                  std::string prov_name = to_std_string(agent->provider);
                  if (!lr_config.api_key.empty())
                  {
                    if (const char* ev = std::getenv(lr_config.api_key.c_str())) lr_config.api_key = ev;
                    else lr_config.api_key.clear();
                  }
                  auto lr_provider = llm::create_provider(prov_name, lr_config);
                  trigger_learning_review(agent_name, lr_provider.get(), extension.learning_config);

                  // Check evolution threshold
                  if (!extension.evolution_config.mutable_fields.empty() &&
                      count >= extension.evolution_config.review_after)
                  {
                    apply_evolution(agent_name, lr_provider.get(), extension.evolution_config);
                  }
                }
                catch (...) { /* learning review failure is non-fatal */ }
              }
            }

            agent->context->history.push_back({"assistant", final_response});
            if (!extension.memory.empty())
            {
              auto& store = memory_stores_[extension.memory];
              store.events.push_back(
                  MemoryEvent{current_time_ms(), "assistant", final_response, agent_name});
            }
            if (!extension.memory.empty() && memory_backend_)
            {
              MemoryEventRecord record;
              record.timestamp = current_time_ms();
              record.type = "assistant";
              record.data = final_response;
              record.agent = agent_name;
              memory_backend_->store_event(extension.memory, record);
            }
            trace_logger_.log_llm_output(agent_name, final_response);

            // When output_type is set, parse JSON response into a structured Value
            if (!extension.output_type.empty())
            {
              try
              {
                auto parsed = nlohmann::json::parse(final_response);
                stack_.push_back(json_to_value(parsed));
              }
              catch (const nlohmann::json::parse_error&)
              {
                // Fallback to string if JSON parsing fails
                stack_.push_back(
                    Value::String(final_response.c_str(), final_response.size()));
              }
            }
            else
            {
              stack_.push_back(
                  Value::String(final_response.c_str(), final_response.size()));
            }
          }
          else if (method == "ask_async")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("Agent.ask_async expects 1 argument");
            }
            const std::string query_str = to_std_string(args[0]);
            const std::string agent_name_str = to_std_string(agent->name);

            // Capture what we need for the async call
            auto future = async::Executor::global().submit(
                [this, agent_name_str, query_str]() -> Value {
                  const std::string result = call_agent_internal(agent_name_str, query_str);
                  return Value::String(result.c_str(), result.size());
                });

            // Wrap in ObjFuture
            auto shared_future =
                std::make_shared<async::Future<Value>>(std::move(future));
            ObjFuture* future_obj = new_future(std::move(shared_future));
            stack_.push_back(Value::Future(future_obj));
          }
          else if (method == "ask_with_image")
          {
            if (arg_count != 2)
            {
              throw std::runtime_error("Agent.ask_with_image expects 2 arguments (query, image_path_or_url)");
            }
            const std::string query_str = to_std_string(args[0]);
            const std::string image_input = to_std_string(args[1]);
            const std::string agent_name_str = to_std_string(agent->name);

            // Build image content part
            std::vector<llm::MessageContent> images;
            llm::MessageContent text_part;
            text_part.type = llm::MessageContent::Type::kText;
            text_part.text = query_str;
            images.push_back(text_part);

            if (image_input.rfind("http://", 0) == 0 || image_input.rfind("https://", 0) == 0)
            {
              // URL-based image
              llm::MessageContent img_part;
              img_part.type = llm::MessageContent::Type::kImageUrl;
              img_part.image_url = image_input;
              images.push_back(img_part);
            }
            else
            {
              // File-based image: read and base64 encode
              std::ifstream img_file(image_input, std::ios::binary);
              if (!img_file)
              {
                throw std::runtime_error("Failed to open image file: " + image_input);
              }
              std::vector<uint8_t> img_bytes((std::istreambuf_iterator<char>(img_file)),
                                              std::istreambuf_iterator<char>());
              // Detect media type from extension
              std::string media_type = "image/png";
              if (image_input.size() >= 4)
              {
                std::string ext = image_input.substr(image_input.find_last_of('.'));
                if (ext == ".jpg" || ext == ".jpeg") media_type = "image/jpeg";
                else if (ext == ".gif") media_type = "image/gif";
                else if (ext == ".webp") media_type = "image/webp";
              }
              // Base64 encode
              static const char b64_chars[] =
                  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
              std::string encoded;
              encoded.reserve(((img_bytes.size() + 2) / 3) * 4);
              for (std::size_t i = 0; i < img_bytes.size(); i += 3)
              {
                uint32_t n = static_cast<uint32_t>(img_bytes[i]) << 16;
                if (i + 1 < img_bytes.size()) n |= static_cast<uint32_t>(img_bytes[i + 1]) << 8;
                if (i + 2 < img_bytes.size()) n |= static_cast<uint32_t>(img_bytes[i + 2]);
                encoded += b64_chars[(n >> 18) & 0x3F];
                encoded += b64_chars[(n >> 12) & 0x3F];
                encoded += (i + 1 < img_bytes.size()) ? b64_chars[(n >> 6) & 0x3F] : '=';
                encoded += (i + 2 < img_bytes.size()) ? b64_chars[n & 0x3F] : '=';
              }

              llm::MessageContent img_part;
              img_part.type = llm::MessageContent::Type::kImageBase64;
              img_part.base64_data = encoded;
              img_part.media_type = media_type;
              images.push_back(img_part);
            }

            const std::string response = call_agent_internal(agent_name_str, query_str, images);
            stack_.push_back(Value::String(response.c_str(), response.size()));
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
          else if (method == "run")
          {
            // Check if this is a runner map
            auto type_it = map->entries.find("__type__");
            if (type_it != map->entries.end() && type_it->second.is_string())
            {
              const std::string type_str = to_std_string(type_it->second);
              if (type_str == "runner")
              {
                // This is a runner - call run_agent_loop
                auto name_it = map->entries.find("name");
                if (name_it == map->entries.end())
                {
                  throw std::runtime_error("Runner has no name");
                }
                const std::string runner_name = to_std_string(name_it->second);

                if (arg_count != 1)
                {
                  throw std::runtime_error("runner.run expects 1 argument (input)");
                }

                // Run the agent loop
                AgentLoopResult loop_result = run_agent_loop(runner_name, args[0]);

                // Convert result to a map
                std::unordered_map<std::string, Value> result_map;
                result_map["final_output"] = loop_result.final_output;
                result_map["final_agent"] =
                    Value::String(loop_result.final_agent.c_str(), loop_result.final_agent.size());
                result_map["total_turns"] =
                    Value::Number(static_cast<double>(loop_result.total_turns));
                result_map["completed"] = Value::Bool(loop_result.completed);
                result_map["total_duration_ms"] =
                    Value::Number(static_cast<double>(loop_result.total_duration_ms));
                result_map["total_input_tokens"] =
                    Value::Number(static_cast<double>(loop_result.total_input_tokens));
                result_map["total_output_tokens"] =
                    Value::Number(static_cast<double>(loop_result.total_output_tokens));
                if (!loop_result.error_message.empty())
                {
                  result_map["error"] =
                      Value::String(loop_result.error_message.c_str(),
                                    loop_result.error_message.size());
                }

                // Convert trace to list
                std::vector<Value> trace_items;
                for (const auto& entry : loop_result.trace)
                {
                  std::unordered_map<std::string, Value> trace_map;
                  trace_map["turn"] = Value::Number(static_cast<double>(entry.turn));
                  trace_map["agent_name"] =
                      Value::String(entry.agent_name.c_str(), entry.agent_name.size());
                  trace_map["input"] = Value::String(entry.input.c_str(), entry.input.size());
                  trace_map["output"] = Value::String(entry.output.c_str(), entry.output.size());
                  trace_map["action"] = Value::String(entry.action.c_str(), entry.action.size());
                  trace_map["timestamp"] = Value::Number(static_cast<double>(entry.timestamp));
                  trace_map["duration_ms"] = Value::Number(static_cast<double>(entry.duration_ms));
                  trace_map["was_handoff"] = Value::Bool(entry.was_handoff);
                  trace_map["input_tokens"] =
                      Value::Number(static_cast<double>(entry.input_tokens));
                  trace_map["output_tokens"] =
                      Value::Number(static_cast<double>(entry.output_tokens));
                  if (!entry.handoff_to.empty())
                  {
                    trace_map["handoff_to"] =
                        Value::String(entry.handoff_to.c_str(), entry.handoff_to.size());
                  }

                  // Convert tool calls to list
                  std::vector<Value> tool_call_items;
                  for (const auto& tc : entry.tool_calls)
                  {
                    std::unordered_map<std::string, Value> tc_map;
                    tc_map["tool_name"] =
                        Value::String(tc.tool_name.c_str(), tc.tool_name.size());
                    tc_map["input"] = Value::String(tc.input.c_str(), tc.input.size());
                    tc_map["output"] = Value::String(tc.output.c_str(), tc.output.size());
                    tc_map["duration_ms"] = Value::Number(static_cast<double>(tc.duration_ms));
                    tool_call_items.push_back(Value::Map(new_map(std::move(tc_map))));
                  }
                  trace_map["tool_calls"] = Value::List(new_list(std::move(tool_call_items)));

                  trace_items.push_back(Value::Map(new_map(std::move(trace_map))));
                }
                result_map["trace"] = Value::List(new_list(std::move(trace_items)));

                // Build trace summary string
                std::string summary = "[TRACE] Runner: " + runner_name + "\n";
                for (const auto& entry : loop_result.trace)
                {
                  summary += "├─ [AGENT] " + entry.agent_name + " (turn " +
                             std::to_string(entry.turn + 1) + ", " +
                             std::to_string(entry.duration_ms) + "ms)\n";
                  for (const auto& tc : entry.tool_calls)
                  {
                    summary += "│  ├─ [TOOL] " + tc.tool_name + "\n";
                  }
                  if (entry.was_handoff)
                  {
                    summary += "│  └─ [HANDOFF] → " + entry.handoff_to + "\n";
                  }
                  else
                  {
                    summary += "│  └─ [OUTPUT] " +
                               (entry.output.size() > 50
                                    ? entry.output.substr(0, 50) + "..."
                                    : entry.output) +
                               "\n";
                  }
                }
                summary += "└─ [COMPLETE] " + std::to_string(loop_result.total_turns) +
                           " turns, " + std::to_string(loop_result.total_duration_ms) + "ms";
                if (loop_result.total_input_tokens > 0 || loop_result.total_output_tokens > 0)
                {
                  summary += ", " +
                             std::to_string(loop_result.total_input_tokens +
                                            loop_result.total_output_tokens) +
                             " tokens";
                }
                summary += "\n";
                result_map["trace_summary"] = Value::String(summary.c_str(), summary.size());

                stack_.push_back(Value::Map(new_map(std::move(result_map))));
              }
              else
              {
                throw std::runtime_error("Unknown map method");
              }
            }
            else
            {
              throw std::runtime_error("Unknown map method");
            }
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
      case OpCode::OP_AWAIT_ALL:
      {
        Value list_value = pop();
        if (!list_value.is_list())
        {
          throw std::runtime_error("await_all expects a list of futures");
        }
        auto* list = as_list(list_value);
        std::vector<Value> results;
        results.reserve(list->items.size());
        for (auto& item : list->items)
        {
          if (!item.is_future())
          {
            // Non-future values pass through unchanged
            results.push_back(item);
            continue;
          }
          auto* future = as_future(item);
          if (!future->future)
          {
            throw std::runtime_error("Future has no state");
          }
          results.push_back(future->future->wait());
        }
        stack_.push_back(Value::List(new_list(std::move(results))));
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

        // Parse embedding_model as "provider/model" format and create EmbeddingProvider
        std::string emb_model_str = to_std_string(knowledge_obj->embedding_model);
        knowledge::EmbeddingCallback embed_cb;
        if (!emb_model_str.empty())
        {
          auto slash_pos = emb_model_str.find('/');
          if (slash_pos != std::string::npos)
          {
            llm::EmbeddingConfig emb_config;
            emb_config.provider = emb_model_str.substr(0, slash_pos);
            emb_config.model = emb_model_str.substr(slash_pos + 1);
            // Look up API key from environment
            if (emb_config.provider == "openai")
            {
              const char* key = std::getenv("OPENAI_API_KEY");
              if (key)
              {
                emb_config.api_key = key;
              }
            }
            auto emb_provider = llm::create_embedding_provider(emb_config);
            if (emb_provider)
            {
              auto shared_provider = std::shared_ptr<llm::EmbeddingProvider>(std::move(emb_provider));
              embed_cb = [shared_provider](const std::string& text) -> std::vector<float> {
                return shared_provider->embed(text);
              };
            }
          }
        }
        knowledge_obj->embed_callback = embed_cb;

        knowledge::Ingester ingester(knowledge_obj->store, knowledge_obj->chunk_size,
                                     knowledge_obj->chunk_overlap,
                                     emb_model_str, embed_cb);
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
        // Pop cognitive fields (v0.5.0) — pushed last, popped first
        Value model_path_value = pop();
        Value evolve_value = pop();
        Value goals_value = pop();
        Value learning_value = pop();
        Value reflect_value = pop();
        Value reasoning_value = pop();
        // Pop mcp_servers (pushed last by compiler - Phase 3)
        Value mcp_servers_value = pop();
        // Pop output_type
        Value output_type_value = pop();
        // Pop context_from (Phase 6)
        Value context_from_value = pop();
        Value connector_value = pop();
        Value plan_value = pop();
        Value world_model_value = pop();
        Value memory_value = pop();
        Value env_value = pop();
        Value budget_value = pop();
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
        // NEW: Extract context_from path (Phase 6)
        ObjString* context_from = context_from_value.is_nil() ? nullptr : as_string(context_from_value);

        auto* skills = as_list(skills_value);
        auto* connected_knowledge = as_list(knowledge_value);
        auto* context = new_context();
        auto* agent =
            new_agent(name, provider, model, endpoint, api_key_env, system, temperature, skills,
                      connected_knowledge, context);
        // NEW: Set context_from on agent (Phase 6)
        agent->context_from = context_from;
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
        if (output_type_value.is_string())
        {
          extension.output_type = to_std_string(output_type_value);
        }
        if (mcp_servers_value.is_list())
        {
          for (const auto& srv : as_list(mcp_servers_value)->items)
          {
            extension.mcp_servers.push_back(to_std_string(srv));
          }
        }

        // Parse cognitive fields (v0.5.0)
        if (reasoning_value.is_string())
        {
          std::string mode_str = to_std_string(reasoning_value);
          if (mode_str == "chain_of_thought") extension.reasoning_mode = ReasoningMode::kChainOfThought;
          else if (mode_str == "plan_and_execute") extension.reasoning_mode = ReasoningMode::kPlanAndExecute;
          else if (mode_str == "tree_of_thought") extension.reasoning_mode = ReasoningMode::kTreeOfThought;
          else if (mode_str == "self_consistency") extension.reasoning_mode = ReasoningMode::kSelfConsistency;
        }
        if (reflect_value.is_string())
        {
          try
          {
            auto rj = nlohmann::json::parse(to_std_string(reflect_value));
            if (rj.contains("after"))
            {
              std::string after = rj["after"].get<std::string>();
              if (after == "each_response") extension.reflection_config.mode = ReflectionMode::kEachResponse;
              else if (after == "on_demand") extension.reflection_config.mode = ReflectionMode::kOnDemand;
              else
              {
                extension.reflection_config.mode = ReflectionMode::kEveryN;
              }
            }
            if (rj.contains("evaluate") && rj["evaluate"].is_array())
            {
              for (const auto& d : rj["evaluate"])
              {
                extension.reflection_config.dimensions.push_back(d.get<std::string>());
              }
            }
            if (rj.contains("min_confidence"))
            {
              extension.reflection_config.min_confidence = rj["min_confidence"].get<double>();
            }
            if (rj.contains("on_low_quality") && rj["on_low_quality"].is_object())
            {
              auto& olq = rj["on_low_quality"];
              if (olq.contains("strategy"))
              {
                std::string s = olq["strategy"].get<std::string>();
                if (s == "revise") extension.reflection_config.strategy = LowQualityStrategy::kRevise;
                else if (s == "retry") extension.reflection_config.strategy = LowQualityStrategy::kRetry;
                else if (s == "escalate") extension.reflection_config.strategy = LowQualityStrategy::kEscalate;
                else if (s == "acknowledge") extension.reflection_config.strategy = LowQualityStrategy::kAcknowledge;
              }
              if (olq.contains("max_revisions"))
              {
                extension.reflection_config.max_revisions = olq["max_revisions"].get<size_t>();
              }
              if (olq.contains("escalate_to"))
              {
                extension.reflection_config.escalate_to = olq["escalate_to"].get<std::string>();
              }
            }
          }
          catch (...) {}
        }
        if (learning_value.is_string())
        {
          try
          {
            auto lj = nlohmann::json::parse(to_std_string(learning_value));
            if (lj.contains("strategy"))
            {
              std::string s = lj["strategy"].get<std::string>();
              if (s == "experience_replay") extension.learning_config.strategy = LearningStrategy::kExperienceReplay;
              else if (s == "pattern_extraction") extension.learning_config.strategy = LearningStrategy::kPatternExtraction;
              else if (s == "prompt_evolution") extension.learning_config.strategy = LearningStrategy::kPromptEvolution;
              else if (s == "preference_learning") extension.learning_config.strategy = LearningStrategy::kPreferenceLearning;
            }
            if (lj.contains("review_interval"))
            {
              extension.learning_config.review_interval = lj["review_interval"].get<size_t>();
            }
            if (lj.contains("max_adaptations"))
            {
              extension.learning_config.max_adaptations = lj["max_adaptations"].get<size_t>();
            }
            if (lj.contains("rollback_on_decline"))
            {
              extension.learning_config.rollback_on_decline = lj["rollback_on_decline"].get<bool>();
            }
          }
          catch (...) {}
        }
        if (goals_value.is_string())
        {
          try
          {
            auto gj = nlohmann::json::parse(to_std_string(goals_value));
            if (gj.contains("goals") && gj["goals"].is_array())
            {
              for (const auto& g : gj["goals"])
              {
                extension.goal_config.goals.push_back(g.get<std::string>());
              }
            }
            if (gj.contains("initiative"))
            {
              extension.goal_config.initiative = gj["initiative"].get<bool>();
            }
            if (gj.contains("triggers") && gj["triggers"].is_object())
            {
              auto& trig = gj["triggers"];
              if (trig.contains("on_schedule"))
              {
                extension.goal_config.schedule = trig["on_schedule"].get<std::string>();
              }
            }
            if (gj.contains("max_daily_calls"))
            {
              extension.goal_config.max_daily_calls = gj["max_daily_calls"].get<size_t>();
            }
            if (gj.contains("max_daily_cost"))
            {
              extension.goal_config.max_daily_cost = gj["max_daily_cost"].get<double>();
            }
            if (gj.contains("max_daily_tokens"))
            {
              extension.goal_config.max_daily_tokens = gj["max_daily_tokens"].get<size_t>();
            }
          }
          catch (...) {}
        }
        if (evolve_value.is_string())
        {
          try
          {
            auto ej = nlohmann::json::parse(to_std_string(evolve_value));
            if (ej.contains("mutable") && ej["mutable"].is_array())
            {
              for (const auto& f : ej["mutable"])
              {
                extension.evolution_config.mutable_fields.push_back(f.get<std::string>());
              }
            }
            if (ej.contains("review_after"))
            {
              extension.evolution_config.review_after = ej["review_after"].get<size_t>();
            }
            if (ej.contains("core_identity"))
            {
              extension.evolution_config.core_identity = ej["core_identity"].get<std::string>();
            }
            if (ej.contains("allow_rollback"))
            {
              extension.evolution_config.allow_rollback = ej["allow_rollback"].get<bool>();
            }
          }
          catch (...) {}
        }
        if (model_path_value.is_string())
        {
          extension.model_path = to_std_string(model_path_value);
        }

        // Register with autonomous executor if goals/schedule configured (v0.5.0)
        if (!extension.goal_config.goals.empty() || !extension.goal_config.schedule.empty())
        {
          if (!autonomous_executor_)
          {
            autonomous_executor_ = std::make_unique<AutonomousExecutor>();
            autonomous_executor_->set_agent_call_fn(
                [this](const std::string& agent_name, const std::string& query) -> std::string {
                  return call_agent_internal(agent_name, query);
                });
            autonomous_executor_->set_log_fn(
                [this](const std::string& agent_name, const std::string& trigger_type,
                       const std::string& action) {
                  if (memory_backend_)
                  {
                    MemoryEventRecord evt;
                    evt.timestamp = current_time_ms();
                    evt.type = "autonomous_action";
                    nlohmann::json data;
                    data["trigger"] = trigger_type;
                    data["action"] = action;
                    evt.data = data.dump();
                    evt.agent = agent_name;
                    memory_backend_->store_event("autonomous_" + agent_name, evt);
                  }
                });
          }
          AutonomousAgentConfig auto_config;
          auto_config.agent_name = to_std_string(name);
          auto_config.goals = extension.goal_config.goals;
          auto_config.schedule = extension.goal_config.schedule;
          auto_config.initiative = extension.goal_config.initiative;
          auto_config.max_daily_calls = extension.goal_config.max_daily_calls;
          auto_config.max_daily_cost = extension.goal_config.max_daily_cost;
          auto_config.max_daily_tokens = extension.goal_config.max_daily_tokens;
          autonomous_executor_->register_agent(auto_config);
          if (!auto_config.schedule.empty())
          {
            autonomous_executor_->start();
          }
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
        // Persist to backend
        if (memory_backend_ && !label.empty())
        {
          memory_backend_->save_checkpoint("global", label, store.events.size());
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
          else if (memory_backend_)
          {
            auto pos = memory_backend_->load_checkpoint("global", label);
            if (pos.has_value())
            {
              store.events.resize(pos.value());
            }
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
      // ================================================================
      // Agentic Orchestration Opcodes
      // ================================================================
      case OpCode::OP_DEFINE_HANDOFF:
      {
        // Pop handoff definition values from stack
        // Format: [agent_name, target_agent, tool_name, description, input_filter, is_enabled, on_handoff, input_type]
        Value input_type_value = pop();
        Value on_handoff_value = pop();
        Value is_enabled_value = pop();
        Value input_filter_value = pop();
        Value description_value = pop();
        Value tool_name_value = pop();
        Value target_agent_value = pop();
        Value agent_name_value = pop();

        const std::string agent_name = to_std_string(agent_name_value);
        const std::string target_agent = to_std_string(target_agent_value);
        const std::string tool_name = tool_name_value.is_nil() ? ("transfer_to_" + target_agent) : to_std_string(tool_name_value);
        const std::string description = description_value.is_nil() ? ("Transfer to " + target_agent) : to_std_string(description_value);
        const std::string input_type = input_type_value.is_nil() ? "" : to_std_string(input_type_value);

        HandoffDef handoff;
        handoff.target_agent = target_agent;
        handoff.tool_name = tool_name;
        handoff.description = description;
        handoff.input_filter = is_obj_type(input_filter_value, ObjType::OBJ_FUNCTION) ? as_function(input_filter_value) : nullptr;
        handoff.is_enabled = is_obj_type(is_enabled_value, ObjType::OBJ_FUNCTION) ? as_function(is_enabled_value) : nullptr;
        handoff.on_handoff = is_obj_type(on_handoff_value, ObjType::OBJ_FUNCTION) ? as_function(on_handoff_value) : nullptr;
        handoff.input_type = input_type;

        register_handoff(agent_name, handoff);
        break;
      }
      case OpCode::OP_DEFINE_AGENT_CARD:
      {
        // Pop agent card definition values from stack
        Value auth_value = pop();
        Value endpoint_value = pop();
        Value output_schema_value = pop();
        Value input_schema_value = pop();
        Value capabilities_value = pop();
        Value description_value = pop();
        Value version_value = pop();
        Value agent_name_value = pop();

        const std::string agent_name = to_std_string(agent_name_value);

        AgentCardDef card;
        card.version = to_std_string(version_value);
        card.description = to_std_string(description_value);
        if (is_obj_type(capabilities_value, ObjType::OBJ_LIST))
        {
          auto* list = as_list(capabilities_value);
          for (const auto& item : list->items)
          {
            card.capabilities.push_back(to_std_string(item));
          }
        }
        card.input_schema = value_to_json(input_schema_value);
        card.output_schema = value_to_json(output_schema_value);
        card.endpoint_url = endpoint_value.is_nil() ? "" : to_std_string(endpoint_value);
        card.authentication = auth_value.is_nil() ? "" : to_std_string(auth_value);

        register_agent_card(agent_name, card);
        break;
      }
      case OpCode::OP_DEFINE_TASK:
      {
        // Pop task definition values from stack
        Value on_status_change_value = pop();
        Value timeout_value = pop();
        Value input_schema_value = pop();
        Value target_agent_value = pop();
        Value task_name_value = pop();

        const std::string task_name = to_std_string(task_name_value);
        const std::string target_agent = target_agent_value.is_nil() ? "" : to_std_string(target_agent_value);
        const int64_t timeout_ms = timeout_value.is_number() ? static_cast<int64_t>(timeout_value.as_number()) : 0;

        // Store task definition in a global map
        std::unordered_map<std::string, Value> task_def_map;
        task_def_map["name"] = task_name_value;
        task_def_map["target_agent"] = target_agent_value;
        task_def_map["input_schema"] = input_schema_value;
        task_def_map["timeout_ms"] = Value::Number(static_cast<double>(timeout_ms));
        task_def_map["on_status_change"] = on_status_change_value;

        auto* name_str = copy_string(task_name.c_str(), task_name.size());
        globals_.set(name_str, Value::Map(new_map(std::move(task_def_map))));
        break;
      }
      case OpCode::OP_DEFINE_RUNNER:
      {
        // Pop runner definition values from stack
        Value on_complete_value = pop();
        Value on_turn_value = pop();
        Value output_guardrails_value = pop();
        Value input_guardrails_value = pop();
        Value guardrails_value = pop();
        Value tracing_value = pop();
        Value max_turns_value = pop();
        Value entry_agent_value = pop();
        Value runner_name_value = pop();

        const std::string runner_name = to_std_string(runner_name_value);
        const std::string entry_agent = to_std_string(entry_agent_value);
        const std::size_t max_turns = max_turns_value.is_number() ? static_cast<std::size_t>(max_turns_value.as_number()) : 10;
        const bool tracing_enabled = is_truthy(tracing_value);

        RunnerDef runner;
        runner.entry_agent = entry_agent;
        runner.max_turns = max_turns;
        runner.tracing_enabled = tracing_enabled;
        if (is_obj_type(guardrails_value, ObjType::OBJ_LIST))
        {
          auto* list = as_list(guardrails_value);
          for (const auto& item : list->items)
          {
            runner.guardrails.push_back(to_std_string(item));
          }
        }
        if (is_obj_type(input_guardrails_value, ObjType::OBJ_LIST))
        {
          auto* list = as_list(input_guardrails_value);
          for (const auto& item : list->items)
          {
            runner.input_guardrails.push_back(to_std_string(item));
          }
        }
        if (is_obj_type(output_guardrails_value, ObjType::OBJ_LIST))
        {
          auto* list = as_list(output_guardrails_value);
          for (const auto& item : list->items)
          {
            runner.output_guardrails.push_back(to_std_string(item));
          }
        }
        runner.on_turn = is_obj_type(on_turn_value, ObjType::OBJ_FUNCTION) ? as_function(on_turn_value) : nullptr;
        runner.on_complete = is_obj_type(on_complete_value, ObjType::OBJ_FUNCTION) ? as_function(on_complete_value) : nullptr;

        register_runner(runner_name, runner);

        // Store runner as a map global so it can be referenced (like R.run())
        std::unordered_map<std::string, Value> runner_map;
        runner_map["__type__"] = Value::String("runner", 6);
        runner_map["name"] = runner_name_value;
        runner_map["entry_agent"] = entry_agent_value;
        runner_map["max_turns"] = max_turns_value;
        runner_map["tracing"] = tracing_value;

        auto* name_str = copy_string(runner_name.c_str(), runner_name.size());
        globals_.set(name_str, Value::Map(new_map(std::move(runner_map))));
        break;
      }
      case OpCode::OP_EXECUTE_HANDOFF:
      {
        Value context_value = pop();
        Value to_agent_value = pop();
        Value from_agent_value = pop();

        const std::string from_agent = to_std_string(from_agent_value);
        const std::string to_agent = to_std_string(to_agent_value);

        Value result = execute_handoff(from_agent, to_agent, context_value);
        stack_.push_back(std::move(result));
        break;
      }
      case OpCode::OP_CREATE_TASK:
      {
        Value input_value = pop();
        Value agent_name_value = pop();

        const std::string agent_name = to_std_string(agent_name_value);
        nlohmann::json input = value_to_json(input_value);

        const std::string task_id = create_task(agent_name, input);

        // Return task info as a map
        std::unordered_map<std::string, Value> task_map;
        task_map["id"] = Value::String(task_id.c_str(), task_id.size());
        task_map["agent"] = agent_name_value;
        task_map["status"] = Value::String("pending", 7);
        stack_.push_back(Value::Map(new_map(std::move(task_map))));
        break;
      }
      case OpCode::OP_SUBMIT_TASK:
      {
        Value task_id_value = pop();
        const std::string task_id = to_std_string(task_id_value);

        // submit_task returns void, so we check task status after
        submit_task(task_id);
        TaskStatusVM status = get_task_status(task_id);
        bool success = (status == TaskStatusVM::kRunning || status == TaskStatusVM::kCompleted);

        std::unordered_map<std::string, Value> result_map;
        result_map["success"] = Value::Bool(success);
        result_map["task_id"] = task_id_value;
        stack_.push_back(Value::Map(new_map(std::move(result_map))));
        break;
      }
      case OpCode::OP_GET_TASK_STATUS:
      {
        Value task_id_value = pop();
        const std::string task_id = to_std_string(task_id_value);

        TaskStatusVM status = get_task_status(task_id);
        const char* status_names[] = {"pending", "running", "completed", "failed", "cancelled"};
        const std::string status_str = status_names[static_cast<int>(status)];

        stack_.push_back(Value::String(status_str.c_str(), status_str.size()));
        break;
      }
      case OpCode::OP_RUN_AGENT_LOOP:
      {
        Value input_value = pop();
        Value runner_name_value = pop();

        const std::string runner_name = to_std_string(runner_name_value);

        AgentLoopResult result = run_agent_loop(runner_name, input_value);

        // Convert result to a map
        std::unordered_map<std::string, Value> result_map;
        result_map["completed"] = Value::Bool(result.completed);
        result_map["final_output"] = result.final_output;  // Already a Value
        result_map["final_agent"] = Value::String(result.final_agent.c_str(), result.final_agent.size());
        result_map["total_turns"] = Value::Number(static_cast<double>(result.total_turns));
        result_map["error_message"] = Value::String(result.error_message.c_str(), result.error_message.size());

        // Convert trace to list of maps
        std::vector<Value> trace_items;
        for (const auto& entry : result.trace)
        {
          std::unordered_map<std::string, Value> trace_map;
          trace_map["turn"] = Value::Number(static_cast<double>(entry.turn));
          trace_map["agent"] = Value::String(entry.agent_name.c_str(), entry.agent_name.size());
          trace_map["input"] = Value::String(entry.input.c_str(), entry.input.size());
          trace_map["output"] = Value::String(entry.output.c_str(), entry.output.size());
          trace_map["was_handoff"] = Value::Bool(entry.was_handoff);
          trace_map["handoff_to"] = Value::String(entry.handoff_to.c_str(), entry.handoff_to.size());
          trace_map["timestamp"] = Value::Number(static_cast<double>(entry.timestamp));
          trace_items.push_back(Value::Map(new_map(std::move(trace_map))));
        }
        result_map["trace"] = Value::List(new_list(std::move(trace_items)));

        stack_.push_back(Value::Map(new_map(std::move(result_map))));
        break;
      }
      case OpCode::OP_TRY_BEGIN:
      {
        // Read the catch offset (relative jump to catch block)
        const uint16_t catch_offset = read_short(code, frame.ip);
        const std::size_t catch_ip = frame.ip + catch_offset;
        exception_handlers_.push_back(
            ExceptionHandler{catch_ip, stack_.size(), frames_.size(), frame.chunk});
        break;
      }
      case OpCode::OP_TRY_END:
      {
        // Normal exit from try block — pop the exception handler
        if (!exception_handlers_.empty())
        {
          exception_handlers_.pop_back();
        }
        break;
      }
      case OpCode::OP_THROW:
      {
        Value thrown_value = pop();
        if (exception_handlers_.empty())
        {
          // No handler — convert to runtime error
          std::string msg = "Unhandled exception: " + to_std_string(thrown_value);
          throw std::runtime_error(msg);
        }
        // Unwind to the exception handler
        auto handler = exception_handlers_.back();
        exception_handlers_.pop_back();

        // Unwind call frames
        while (frames_.size() > handler.frame_depth)
        {
          frames_.pop_back();
        }
        // Restore stack to the depth when try began
        stack_.resize(handler.stack_depth);
        // Push the thrown value as the catch variable
        stack_.push_back(thrown_value);

        // Jump to catch block
        if (frames_.empty())
        {
          throw std::runtime_error("Exception handler frame lost");
        }
        frames_.back().ip = handler.catch_ip;
        break;
      }
      case OpCode::OP_DEFINE_VOICE_PIPELINE:
      {
        // Pop 14 values in reverse order (last pushed = first popped)
        Value stt_format_val = pop();
        Value stt_language_val = pop();
        Value tts_instructions_val = pop();
        Value tts_speed_val = pop();
        Value tts_format_val = pop();
        Value tts_endpoint_val = pop();
        Value stt_endpoint_val = pop();
        Value tts_voice_val = pop();
        Value tts_model_val = pop();
        Value tts_provider_val = pop();
        Value stt_model_val = pop();
        Value stt_provider_val = pop();
        Value agent_name_val = pop();
        Value pipeline_name_val = pop();

        VoicePipelineDef def;
        def.agent_name = to_std_string(agent_name_val);
        def.stt_provider = to_std_string(stt_provider_val);
        def.stt_model = to_std_string(stt_model_val);
        def.tts_provider = to_std_string(tts_provider_val);
        def.tts_model = to_std_string(tts_model_val);
        def.tts_voice = to_std_string(tts_voice_val);
        def.stt_endpoint = to_std_string(stt_endpoint_val);
        def.tts_endpoint = to_std_string(tts_endpoint_val);
        def.tts_format = to_std_string(tts_format_val);
        def.tts_speed = to_std_string(tts_speed_val);
        def.tts_instructions = to_std_string(tts_instructions_val);
        def.stt_language = to_std_string(stt_language_val);
        def.stt_format = to_std_string(stt_format_val);

        const std::string name = to_std_string(pipeline_name_val);
        register_voice_pipeline(name, def);

        // Store pipeline as a map global
        std::unordered_map<std::string, Value> pipeline_map;
        pipeline_map["__type__"] = Value::String("voice_pipeline", 14);
        pipeline_map["name"] = pipeline_name_val;
        pipeline_map["agent"] = agent_name_val;
        pipeline_map["stt_provider"] = stt_provider_val;
        pipeline_map["stt_model"] = stt_model_val;
        pipeline_map["tts_provider"] = tts_provider_val;
        pipeline_map["tts_model"] = tts_model_val;
        pipeline_map["tts_voice"] = tts_voice_val;
        pipeline_map["stt_endpoint"] = stt_endpoint_val;
        pipeline_map["tts_endpoint"] = tts_endpoint_val;
        pipeline_map["tts_format"] = tts_format_val;
        pipeline_map["tts_speed"] = tts_speed_val;
        pipeline_map["tts_instructions"] = tts_instructions_val;
        pipeline_map["stt_language"] = stt_language_val;
        pipeline_map["stt_format"] = stt_format_val;

        auto* name_str = copy_string(name.c_str(), name.size());
        globals_.set(name_str, Value::Map(new_map(std::move(pipeline_map))));
        break;
      }
      case OpCode::OP_VOICE_TRANSCRIBE:
      {
        Value audio_path_val = pop();
        Value pipeline_name_val = pop();

        const std::string pipeline_name = to_std_string(pipeline_name_val);
        const std::string audio_path = to_std_string(audio_path_val);

        std::string text = transcribe_audio(pipeline_name, audio_path);
        stack_.push_back(Value::String(text.c_str(), text.size()));
        break;
      }
      case OpCode::OP_VOICE_SYNTHESIZE:
      {
        Value output_path_val = pop();
        Value text_val = pop();
        Value pipeline_name_val = pop();

        const std::string pipeline_name = to_std_string(pipeline_name_val);
        const std::string text = to_std_string(text_val);
        const std::string output_path = to_std_string(output_path_val);

        synthesize_speech(pipeline_name, text, output_path);
        stack_.push_back(Value::String(output_path.c_str(), output_path.size()));
        break;
      }
      case OpCode::OP_VOICE_PIPELINE_RUN:
      {
        Value audio_output_val = pop();
        Value audio_input_val = pop();
        Value pipeline_name_val = pop();

        const std::string pipeline_name = to_std_string(pipeline_name_val);
        const std::string audio_input = to_std_string(audio_input_val);
        const std::string audio_output = to_std_string(audio_output_val);

        const auto* def = get_voice_pipeline(pipeline_name);
        if (!def)
        {
          throw std::runtime_error("Unknown voice pipeline: " + pipeline_name);
        }

        voice::STTConfig stt_config;
        stt_config.provider = def->stt_provider;
        stt_config.model = def->stt_model;
        stt_config.endpoint = def->stt_endpoint;
        stt_config.language = def->stt_language;
        stt_config.format = def->stt_format;

        voice::TTSConfig tts_config;
        tts_config.provider = def->tts_provider;
        tts_config.model = def->tts_model;
        tts_config.voice = def->tts_voice;
        tts_config.endpoint = def->tts_endpoint;
        tts_config.format = def->tts_format;
        tts_config.speed = def->tts_speed;
        tts_config.instructions = def->tts_instructions;

        auto result = voice::run_voice_pipeline(
            *this, def->agent_name, audio_input, audio_output,
            stt_config, tts_config);

        std::unordered_map<std::string, Value> result_map;
        result_map["input_text"] = Value::String(result.input_text.c_str(), result.input_text.size());
        result_map["response_text"] = Value::String(result.response_text.c_str(), result.response_text.size());
        result_map["output_audio"] = Value::String(result.output_audio_path.c_str(), result.output_audio_path.size());
        stack_.push_back(Value::Map(new_map(std::move(result_map))));
        break;
      }
      case OpCode::OP_DEFINE_REALTIME_VOICE:
      {
        // Pop 15 values in reverse order (name + agent + 13 config fields)
        Value llm_endpoint_val = pop();
        Value tts_endpoint_val = pop();
        Value stt_endpoint_val = pop();
        Value speed_val = pop();
        Value sample_rate_val = pop();
        Value output_format_val = pop();
        Value input_format_val = pop();
        Value silence_ms_val = pop();
        Value vad_threshold_val = pop();
        Value vad_val = pop();
        Value voice_val = pop();
        Value model_val = pop();
        Value provider_val = pop();
        Value agent_name_val = pop();
        Value config_name_val = pop();

        RealtimeVoiceDef def;
        def.agent_name = to_std_string(agent_name_val);
        def.provider = to_std_string(provider_val);
        def.model = to_std_string(model_val);
        def.voice = to_std_string(voice_val);
        def.vad = to_std_string(vad_val);
        std::string vt = to_std_string(vad_threshold_val);
        if (!vt.empty()) def.vad_threshold = std::stod(vt);
        std::string sm = to_std_string(silence_ms_val);
        if (!sm.empty()) def.silence_duration_ms = std::stoi(sm);
        def.input_format = to_std_string(input_format_val);
        def.output_format = to_std_string(output_format_val);
        std::string sr = to_std_string(sample_rate_val);
        if (!sr.empty()) def.sample_rate = std::stoi(sr);
        std::string sp = to_std_string(speed_val);
        if (!sp.empty()) def.speed = std::stod(sp);
        def.stt_endpoint = to_std_string(stt_endpoint_val);
        def.tts_endpoint = to_std_string(tts_endpoint_val);
        def.llm_endpoint = to_std_string(llm_endpoint_val);

        const std::string name = to_std_string(config_name_val);
        register_realtime_voice(name, def);

        // Store as a map global
        std::unordered_map<std::string, Value> rt_map;
        rt_map["__type__"] = Value::String("realtime_voice", 14);
        rt_map["name"] = config_name_val;
        rt_map["agent"] = agent_name_val;
        rt_map["provider"] = provider_val;
        rt_map["model"] = model_val;
        rt_map["voice"] = voice_val;

        auto* name_str = copy_string(name.c_str(), name.size());
        globals_.set(name_str, Value::Map(new_map(std::move(rt_map))));
        break;
      }
      case OpCode::OP_REALTIME_CONNECT:
      {
        Value config_name_val = pop();
        const std::string config_name = to_std_string(config_name_val);
        std::string session_id = realtime_connect(config_name);
        stack_.push_back(Value::String(session_id.c_str(), session_id.size()));
        break;
      }
      case OpCode::OP_REALTIME_SEND_AUDIO:
      {
        Value audio_val = pop();
        Value session_val = pop();
        const std::string session_id = to_std_string(session_val);
        const std::string audio_b64 = to_std_string(audio_val);
        auto session = get_realtime_session(session_id);
        if (!session)
        {
          throw std::runtime_error("Unknown realtime session: " + session_id);
        }
        session->send_audio_base64(audio_b64);
        stack_.push_back(Value::Nil());
        break;
      }
      case OpCode::OP_REALTIME_SEND_TEXT:
      {
        Value text_val = pop();
        Value session_val = pop();
        const std::string session_id = to_std_string(session_val);
        const std::string text = to_std_string(text_val);
        auto session = get_realtime_session(session_id);
        if (!session)
        {
          throw std::runtime_error("Unknown realtime session: " + session_id);
        }
        session->send_text(text);
        stack_.push_back(Value::Nil());
        break;
      }
      case OpCode::OP_REALTIME_ON_EVENT:
      {
        // This opcode is handled via native functions
        // Pop event_name, session_id, callback
        pop(); pop(); pop();
        stack_.push_back(Value::Nil());
        break;
      }
      case OpCode::OP_REALTIME_TOOL_RESULT:
      {
        Value result_val = pop();
        Value call_id_val = pop();
        Value session_val = pop();
        const std::string session_id = to_std_string(session_val);
        const std::string call_id = to_std_string(call_id_val);
        const std::string result = to_std_string(result_val);
        auto session = get_realtime_session(session_id);
        if (!session)
        {
          throw std::runtime_error("Unknown realtime session: " + session_id);
        }
        session->send_tool_result(call_id, result);
        stack_.push_back(Value::Nil());
        break;
      }
      case OpCode::OP_REALTIME_CLOSE:
      {
        Value session_val = pop();
        const std::string session_id = to_std_string(session_val);
        auto session = get_realtime_session(session_id);
        if (session)
        {
          session->close();
        }
        realtime_sessions_.erase(session_id);
        stack_.push_back(Value::Nil());
        break;
      }
      // Cognitive opcodes stubs (v0.5.0) — behavior added in later phases
      case OpCode::OP_DEFINE_REASONING:
      case OpCode::OP_DEFINE_REFLECTION:
      case OpCode::OP_DEFINE_LEARNING:
      case OpCode::OP_DEFINE_GOALS:
      case OpCode::OP_DEFINE_EVOLUTION:
      case OpCode::OP_DEFINE_INNER_MODEL:
      {
        // Stubs: pop the value pushed by compiler and discard
        pop();
        break;
      }
      case OpCode::OP_AGENT_RATE:
      {
        // agent_rate(agent, score) — 2 args popped
        pop(); // score
        pop(); // agent
        stack_.push_back(Value::Nil());
        break;
      }
      case OpCode::OP_AGENT_REFLECT:
      {
        // agent_reflect(agent) — 1 arg
        pop(); // agent
        stack_.push_back(Value::Nil());
        break;
      }
      case OpCode::OP_AGENT_EVOLVE:
      {
        // agent_evolve(agent) — 1 arg
        pop(); // agent
        stack_.push_back(Value::Nil());
        break;
      }
      case OpCode::OP_AGENT_STATUS:
      {
        // agent_status(agent) — 1 arg
        pop(); // agent
        stack_.push_back(Value::Nil());
        break;
      }
      default:
        throw std::runtime_error("Unknown opcode encountered");
    }
    }  // end try
    catch (const std::runtime_error& e)
    {
      // Check if there's a Neam exception handler on the stack
      if (!exception_handlers_.empty())
      {
        auto handler = exception_handlers_.back();
        exception_handlers_.pop_back();

        // Unwind call frames
        while (frames_.size() > handler.frame_depth)
        {
          frames_.pop_back();
        }
        // Restore stack
        stack_.resize(handler.stack_depth);

        // Push error as a map with message and type fields
        std::unordered_map<std::string, Value> error_map;
        error_map["message"] = Value::String(e.what(), std::strlen(e.what()));
        error_map["type"] = Value::String("RuntimeError", 12);
        stack_.push_back(Value::Map(new_map(std::move(error_map))));

        // Jump to catch block
        if (frames_.empty())
        {
          throw;
        }
        frames_.back().ip = handler.catch_ip;
        continue;
      }
      throw;  // No handler — propagate
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
  if (frames_.size() >= 1024)
  {
    throw std::runtime_error("Stack overflow: maximum call depth (1024) exceeded");
  }
  frames_.push_back(CallFrame{&fn->chunk, fn, 0, stack_start});
  frames_.back().is_tool = is_tool;
  frames_.back().tool_name = tool_name;
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

// ============================================================================
// Agentic Orchestration Implementation
// ============================================================================

void VirtualMachine::register_handoff(const std::string& agent_name, const HandoffDef& handoff)
{
  agent_handoffs_[agent_name].push_back(handoff);
}

const std::vector<VirtualMachine::HandoffDef>* VirtualMachine::get_agent_handoffs(
    const std::string& agent_name) const
{
  auto it = agent_handoffs_.find(agent_name);
  if (it == agent_handoffs_.end())
  {
    return nullptr;
  }
  return &it->second;
}

Value VirtualMachine::execute_handoff(const std::string& from_agent, const std::string& to_agent,
                                      const Value& context)
{
  // Find the handoff definition
  const auto* handoffs = get_agent_handoffs(from_agent);
  if (!handoffs)
  {
    throw std::runtime_error("No handoffs defined for agent: " + from_agent);
  }

  const HandoffDef* target_handoff = nullptr;
  for (const auto& h : *handoffs)
  {
    if (h.target_agent == to_agent)
    {
      target_handoff = &h;
      break;
    }
  }

  if (!target_handoff)
  {
    throw std::runtime_error("No handoff to agent '" + to_agent + "' from '" + from_agent + "'");
  }

  // Check if handoff is enabled (if is_enabled callback is defined)
  if (target_handoff->is_enabled)
  {
    Value enabled_result = call_function(target_handoff->is_enabled, {context}, false, "");
    if (!is_truthy(enabled_result))
    {
      throw std::runtime_error("Handoff to '" + to_agent + "' is currently disabled");
    }
  }

  // Apply input filter if defined
  Value filtered_context = context;
  if (target_handoff->input_filter)
  {
    filtered_context = call_function(target_handoff->input_filter, {context}, false, "");
  }

  // Call on_handoff callback if defined
  if (target_handoff->on_handoff)
  {
    call_function(target_handoff->on_handoff, {filtered_context}, false, "");
  }

  // Find the target agent and invoke it
  const Value* agent_value = find_global_value(globals_, to_agent);
  if (!agent_value || !agent_value->is_agent())
  {
    throw std::runtime_error("Target agent not found: " + to_agent);
  }

  // Call the target agent with the filtered context
  std::string query = filtered_context.is_string() ? to_std_string(filtered_context)
                                                   : value_to_string(filtered_context);

  // Get handoffs for the target agent (for potential chained handoffs)
  const auto* target_handoffs = get_agent_handoffs(to_agent);

  std::string response = call_agent_internal(to_agent, query, target_handoffs);
  return Value::String(response.c_str(), response.size());
}

void VirtualMachine::register_agent_card(const std::string& agent_name, const AgentCardDef& card)
{
  agent_cards_[agent_name] = card;
}

const VirtualMachine::AgentCardDef* VirtualMachine::get_agent_card(
    const std::string& agent_name) const
{
  auto it = agent_cards_.find(agent_name);
  if (it == agent_cards_.end())
  {
    return nullptr;
  }
  return &it->second;
}

nlohmann::json VirtualMachine::get_agent_card_json(const std::string& agent_name) const
{
  const auto* card = get_agent_card(agent_name);
  if (!card)
  {
    return nlohmann::json::object();
  }

  nlohmann::json result;
  result["name"] = agent_name;
  result["version"] = card->version;
  result["description"] = card->description;
  result["capabilities"] = card->capabilities;
  result["input_schema"] = card->input_schema;
  result["output_schema"] = card->output_schema;

  if (!card->endpoint_url.empty())
  {
    result["url"] = card->endpoint_url;
  }
  if (!card->authentication.empty())
  {
    result["authentication"] = {{"type", card->authentication}};
  }

  return result;
}

std::string VirtualMachine::create_task(const std::string& agent_name, const nlohmann::json& input)
{
  uint64_t counter = task_counter_.fetch_add(1, std::memory_order_relaxed);
  std::string task_id = "task_" + std::to_string(counter) + "_" + std::to_string(current_time_ms());

  TaskInstance task;
  task.id = task_id;
  task.status = TaskStatusVM::kPending;
  task.target_agent = agent_name;
  task.input = input;
  task.created_at = current_time_ms();

  tasks_[task_id] = std::move(task);
  return task_id;
}

void VirtualMachine::submit_task(const std::string& task_id)
{
  auto it = tasks_.find(task_id);
  if (it == tasks_.end())
  {
    throw std::runtime_error("Task not found: " + task_id);
  }

  TaskInstance& task = it->second;
  if (task.status != TaskStatusVM::kPending)
  {
    throw std::runtime_error("Task is not in pending state: " + task_id);
  }

  TaskStatusVM old_status = task.status;
  task.status = TaskStatusVM::kRunning;
  task.started_at = current_time_ms();

  // Call status change callback if defined
  if (task.on_status_change)
  {
    Value task_val = Value::String(task_id.c_str(), task_id.size());
    Value old_status_val = Value::String("pending", 7);
    Value new_status_val = Value::String("running", 7);
    call_function(task.on_status_change, {task_val, old_status_val, new_status_val}, false, "");
  }

  // In a full implementation, this would actually execute the task asynchronously
  // For now, we just mark it as running
}

VirtualMachine::TaskStatusVM VirtualMachine::get_task_status(const std::string& task_id) const
{
  auto it = tasks_.find(task_id);
  if (it == tasks_.end())
  {
    throw std::runtime_error("Task not found: " + task_id);
  }
  return it->second.status;
}

nlohmann::json VirtualMachine::get_task_result(const std::string& task_id) const
{
  auto it = tasks_.find(task_id);
  if (it == tasks_.end())
  {
    throw std::runtime_error("Task not found: " + task_id);
  }

  const TaskInstance& task = it->second;
  nlohmann::json result;
  result["id"] = task.id;
  result["status"] = [&]() {
    switch (task.status)
    {
      case TaskStatusVM::kPending:
        return "pending";
      case TaskStatusVM::kRunning:
        return "running";
      case TaskStatusVM::kCompleted:
        return "completed";
      case TaskStatusVM::kFailed:
        return "failed";
      case TaskStatusVM::kCancelled:
        return "cancelled";
    }
    return "unknown";
  }();
  result["output"] = task.output;

  if (!task.error_message.empty())
  {
    result["error"] = task.error_message;
  }

  return result;
}

void VirtualMachine::cancel_task(const std::string& task_id)
{
  auto it = tasks_.find(task_id);
  if (it == tasks_.end())
  {
    throw std::runtime_error("Task not found: " + task_id);
  }

  TaskInstance& task = it->second;
  if (task.status == TaskStatusVM::kCompleted || task.status == TaskStatusVM::kFailed ||
      task.status == TaskStatusVM::kCancelled)
  {
    return;  // Already in terminal state
  }

  task.status = TaskStatusVM::kCancelled;
  task.completed_at = current_time_ms();
}

VirtualMachine::TaskInstance* VirtualMachine::get_task(const std::string& task_id)
{
  auto it = tasks_.find(task_id);
  if (it == tasks_.end())
  {
    return nullptr;
  }
  return &it->second;
}

std::string VirtualMachine::get_task_status_string(const std::string& task_id) const
{
  auto it = tasks_.find(task_id);
  if (it == tasks_.end())
  {
    return "unknown";
  }
  switch (it->second.status)
  {
    case TaskStatusVM::kPending:
      return "pending";
    case TaskStatusVM::kRunning:
      return "running";
    case TaskStatusVM::kCompleted:
      return "completed";
    case TaskStatusVM::kFailed:
      return "failed";
    case TaskStatusVM::kCancelled:
      return "cancelled";
  }
  return "unknown";
}

bool VirtualMachine::run_guardrails(const std::vector<std::string>& guardrail_names,
                                     const std::string& handler_type, std::string& value)
{
  for (const auto& guard_name : guardrail_names)
  {
    // Look up the guard in guardchains_ first (it may be a guardchain)
    auto chain_it = guardchains_.find(guard_name);
    if (chain_it != guardchains_.end())
    {
      // It's a guardchain - run all guards in the chain
      for (const auto& chain_guard_name : chain_it->second.guards)
      {
        auto guard_it = guards_.find(chain_guard_name);
        if (guard_it == guards_.end())
        {
          continue;
        }
        const auto& guard = guard_it->second;
        for (const auto& handler : guard.handlers)
        {
          if (handler.type != handler_type || !handler.impl)
          {
            continue;
          }
          std::vector<Value> handler_args;
          if (!handler.parameters.empty())
          {
            handler_args.push_back(Value::String(value.c_str(), value.size()));
          }
          Value result = call_function(handler.impl, handler_args, false, chain_guard_name);
          if (result.is_string())
          {
            const auto output = to_std_string(result);
            if (output == "block")
            {
              return false;
            }
            value = output;
          }
        }
      }
    }
    else
    {
      // Try as a direct guard
      auto guard_it = guards_.find(guard_name);
      if (guard_it == guards_.end())
      {
        continue;
      }
      const auto& guard = guard_it->second;
      for (const auto& handler : guard.handlers)
      {
        if (handler.type != handler_type || !handler.impl)
        {
          continue;
        }
        std::vector<Value> handler_args;
        if (!handler.parameters.empty())
        {
          handler_args.push_back(Value::String(value.c_str(), value.size()));
        }
        Value result = call_function(handler.impl, handler_args, false, guard_name);
        if (result.is_string())
        {
          const auto output = to_std_string(result);
          if (output == "block")
          {
            return false;
          }
          value = output;
        }
      }
    }
  }
  return true;
}

void VirtualMachine::register_runner(const std::string& name, const RunnerDef& runner)
{
  runners_[name] = runner;
}

VirtualMachine::AgentLoopResult VirtualMachine::run_agent_loop(const std::string& runner_name,
                                                               const Value& input)
{
  auto it = runners_.find(runner_name);
  if (it == runners_.end())
  {
    AgentLoopResult result;
    result.error_message = "Runner not found: " + runner_name;
    return result;
  }

  const RunnerDef& runner = it->second;

  // Convert input to string for guardrail processing
  std::string input_str = input.is_string() ? to_std_string(input) : value_to_string(input);

  // Run input_guardrails before first agent call
  if (!runner.input_guardrails.empty())
  {
    output_stream() << "[Runner] Running input guardrails...\n";
    if (!run_guardrails(runner.input_guardrails, "on_tool_input", input_str))
    {
      AgentLoopResult result;
      result.error_message = "Input blocked by guardrail";
      result.completed = false;
      return result;
    }
  }

  // Create the filtered input value
  Value filtered_input = Value::String(input_str.c_str(), input_str.size());

  // Run the agent loop
  AgentLoopResult result = run_agent_loop(runner.entry_agent, filtered_input, runner.max_turns);

  // Run output_guardrails before returning final output
  if (!runner.output_guardrails.empty() && result.completed && result.final_output.is_string())
  {
    output_stream() << "[Runner] Running output guardrails...\n";
    std::string output_str = to_std_string(result.final_output);
    if (!run_guardrails(runner.output_guardrails, "on_tool_output", output_str))
    {
      result.error_message = "Output blocked by guardrail";
      result.completed = false;
      result.final_output = Value::Nil();
      return result;
    }
    // Update the final output with any transformations from guardrails
    result.final_output = Value::String(output_str.c_str(), output_str.size());
  }

  return result;
}

std::string VirtualMachine::call_agent_internal(const std::string& agent_name,
                                                 const std::string& query,
                                                 const std::vector<HandoffDef>* handoffs)
{
  // Find the agent
  const Value* agent_value = find_global_value(globals_, agent_name);
  if (!agent_value || !agent_value->is_agent())
  {
    return "Error: Agent not found: " + agent_name;
  }

  auto* agent = as_agent(*agent_value);

  // Get agent extensions
  const auto extension_it = agent_extensions_.find(agent_name);
  const AgentExtension extension =
      extension_it == agent_extensions_.end() ? AgentExtension{} : extension_it->second;

  // Create LLM provider config
  llm::ProviderConfig config;
  config.model = to_std_string(agent->model);
  config.endpoint = to_std_string(agent->endpoint);
  config.api_key = to_std_string(agent->api_key_env);
  config.temperature = agent->temperature;
  config.default_host =
      resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST", "http://localhost:11434");

  std::string provider_name = to_std_string(agent->provider);

  // Resolve API key from environment variable
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

  // Build system prompt with handoff tools
  std::string system_prompt = agent->system ? to_std_string(agent->system) : "";

  // NEW: Load and inject context from AGENTS.md file (Phase 6)
  if (agent->context_from)
  {
    std::string context_path = to_std_string(agent->context_from);
    std::ifstream context_file(context_path);
    if (context_file.is_open())
    {
      std::stringstream buffer;
      buffer << context_file.rdbuf();
      std::string context_content = buffer.str();
      if (!context_content.empty())
      {
        // Prepend context from AGENTS.md to system prompt
        std::string context_header = "## Project Context (from " + context_path + ")\n\n";
        if (!system_prompt.empty())
        {
          system_prompt = context_header + context_content + "\n\n---\n\n" + system_prompt;
        }
        else
        {
          system_prompt = context_header + context_content;
        }
      }
    }
    // Silently ignore if file doesn't exist - user may want optional context
  }

  // Add handoff tools to system prompt (OpenAI SDK style)
  if (handoffs && !handoffs->empty())
  {
    system_prompt += "\n\n## Available Handoff Tools\n";
    system_prompt += "You can transfer control to another agent by calling one of these tools:\n\n";
    for (const auto& h : *handoffs)
    {
      system_prompt += "- **" + h.tool_name + "**: " + h.description;
      if (!h.input_type.empty())
      {
        system_prompt += " (requires structured input of type: " + h.input_type + ")";
      }
      system_prompt += "\n";
    }
    system_prompt += "\nTo hand off, respond with: HANDOFF: <tool_name>\n";
    system_prompt += "For handoffs with structured input, use: HANDOFF: <tool_name> {\"field\": \"value\", ...}\n";
    system_prompt += "Example: HANDOFF: transfer_to_RefundAgent\n";
    system_prompt += "Example with input: HANDOFF: process_refund {\"order_id\": \"ORD-123\", \"reason\": \"damaged\"}\n";
  }

  // Apply reasoning strategy (v0.5.0)
  if (extension.reasoning_mode != ReasoningMode::kNone)
  {
    return apply_reasoning_strategy(agent, provider.get(), system_prompt, query,
                                    extension.reasoning_mode, extension.reasoning_config,
                                    handoffs);
  }

  // Build messages for LLM
  std::vector<llm::Message> messages;
  if (!system_prompt.empty())
  {
    messages.push_back({"system", system_prompt});
  }

  // Add context history if available
  if (agent->context)
  {
    for (const auto& message : agent->context->history)
    {
      messages.push_back({message.role, message.content});
    }
  }

  messages.push_back({"user", query});

  // Call the LLM
  const auto response_text = provider->chat(messages);

  // Update agent context
  if (!agent->context)
  {
    agent->context = new_context();
  }
  agent->context->history.push_back({"user", query});
  agent->context->history.push_back({"assistant", response_text});

  return response_text;
}

std::string VirtualMachine::call_agent_internal(const std::string& agent_name,
                                                 const std::string& query,
                                                 const std::vector<llm::MessageContent>& images,
                                                 const std::vector<HandoffDef>* handoffs)
{
  // Find the agent
  const Value* agent_value = find_global_value(globals_, agent_name);
  if (!agent_value || !agent_value->is_agent())
  {
    return "Error: Agent not found: " + agent_name;
  }

  auto* agent = as_agent(*agent_value);

  // Get agent extensions
  const auto extension_it = agent_extensions_.find(agent_name);
  const AgentExtension extension =
      extension_it == agent_extensions_.end() ? AgentExtension{} : extension_it->second;

  // Create LLM provider config
  llm::ProviderConfig config;
  config.model = to_std_string(agent->model);
  config.endpoint = to_std_string(agent->endpoint);
  config.api_key = to_std_string(agent->api_key_env);
  config.temperature = agent->temperature;
  config.default_host =
      resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST", "http://localhost:11434");

  std::string provider_name = to_std_string(agent->provider);

  // Resolve API key from environment variable
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

  // Build system prompt
  std::string system_prompt = agent->system ? to_std_string(agent->system) : "";

  // Build messages with multi-part content
  std::vector<llm::Message> messages;
  if (!system_prompt.empty())
  {
    messages.push_back({"system", system_prompt});
  }

  // Add context history if available
  if (agent->context)
  {
    for (const auto& message : agent->context->history)
    {
      messages.push_back({message.role, message.content});
    }
  }

  // Build user message with image content parts
  llm::Message user_msg;
  user_msg.role = "user";
  user_msg.content = query;
  user_msg.content_parts = images;
  messages.push_back(user_msg);

  // Call the LLM
  const auto response_text = provider->chat(messages);

  // Update agent context
  if (!agent->context)
  {
    agent->context = new_context();
  }
  agent->context->history.push_back({"user", query});
  agent->context->history.push_back({"assistant", response_text});

  return response_text;
}

VirtualMachine::AgentLoopResult VirtualMachine::run_agent_loop(const std::string& agent_name,
                                                               const Value& input,
                                                               std::size_t max_turns)
{
  AgentLoopResult result;
  result.final_agent = agent_name;
  result.start_time = current_time_ms();
  std::string current_agent = agent_name;
  std::string current_query = input.is_string() ? to_std_string(input) : value_to_string(input);

  for (std::size_t turn = 0; turn < max_turns; ++turn)
  {
    AgentLoopTraceEntry trace_entry;
    trace_entry.turn = turn;
    trace_entry.agent_name = current_agent;
    trace_entry.timestamp = current_time_ms();
    trace_entry.input = current_query;
    int64_t turn_start = trace_entry.timestamp;

    // Find the current agent
    const Value* agent_value = find_global_value(globals_, current_agent);
    if (!agent_value || !agent_value->is_agent())
    {
      result.error_message = "Agent not found: " + current_agent;
      result.completed = false;
      return result;
    }

    // Get handoffs for this agent
    const auto* handoffs = get_agent_handoffs(current_agent);

    // Call the agent with handoff tools
    std::string response = call_agent_internal(current_agent, current_query, handoffs);
    trace_entry.output = response;

    // Check for handoff request in response (format: "HANDOFF: tool_name" or "HANDOFF: tool_name {json}")
    bool performed_handoff = false;
    std::string handoff_target;

    // Look for handoff pattern
    const std::string handoff_prefix = "HANDOFF:";
    auto handoff_pos = response.find(handoff_prefix);
    if (handoff_pos != std::string::npos && handoffs)
    {
      // Extract tool name and optional structured input
      std::string remainder = response.substr(handoff_pos + handoff_prefix.length());

      // Trim leading whitespace
      remainder.erase(0, remainder.find_first_not_of(" \t\n\r"));

      // Extract tool name (up to whitespace or '{')
      std::string tool_name;
      std::string structured_input;
      auto json_start = remainder.find('{');
      auto space_pos = remainder.find_first_of(" \t\n\r");

      if (json_start != std::string::npos && (space_pos == std::string::npos || json_start < space_pos))
      {
        // JSON immediately after tool name (no space)
        tool_name = remainder.substr(0, json_start);
        // Extract JSON until matching '}'
        int brace_count = 0;
        std::size_t json_end = json_start;
        for (std::size_t i = json_start; i < remainder.size(); ++i)
        {
          if (remainder[i] == '{') ++brace_count;
          else if (remainder[i] == '}') --brace_count;
          if (brace_count == 0)
          {
            json_end = i + 1;
            break;
          }
        }
        structured_input = remainder.substr(json_start, json_end - json_start);
      }
      else if (space_pos != std::string::npos)
      {
        // Tool name followed by space, then possibly JSON
        tool_name = remainder.substr(0, space_pos);
        std::string after_tool = remainder.substr(space_pos);
        after_tool.erase(0, after_tool.find_first_not_of(" \t\n\r"));
        if (!after_tool.empty() && after_tool[0] == '{')
        {
          // Extract JSON
          int brace_count = 0;
          std::size_t json_end = 0;
          for (std::size_t i = 0; i < after_tool.size(); ++i)
          {
            if (after_tool[i] == '{') ++brace_count;
            else if (after_tool[i] == '}') --brace_count;
            if (brace_count == 0)
            {
              json_end = i + 1;
              break;
            }
          }
          structured_input = after_tool.substr(0, json_end);
        }
      }
      else
      {
        // Just tool name, no JSON
        tool_name = remainder;
      }

      // Trim trailing whitespace from tool name
      tool_name.erase(tool_name.find_last_not_of(" \t\n\r") + 1);

      // Find matching handoff
      for (const auto& h : *handoffs)
      {
        if (h.tool_name == tool_name)
        {
          // Check if handoff is enabled
          bool enabled = true;
          if (h.is_enabled)
          {
            Value context_val = Value::String(current_query.c_str(), current_query.size());
            Value enabled_result = call_function(h.is_enabled, {context_val}, false, "");
            enabled = is_truthy(enabled_result);
          }

          if (enabled)
          {
            // Determine input to pass to next agent
            std::string handoff_input = current_query;

            // If structured input was provided and handoff expects it, use it
            if (!structured_input.empty() && !h.input_type.empty())
            {
              output_stream() << "[Runner] Structured handoff input (" << h.input_type << "): "
                              << structured_input << "\n";
              handoff_input = structured_input;
            }

            // Apply input filter if defined
            std::string filtered_input = handoff_input;
            if (h.input_filter)
            {
              Value input_val = Value::String(handoff_input.c_str(), handoff_input.size());
              Value filtered_result = call_function(h.input_filter, {input_val}, false, "");
              if (filtered_result.is_string())
              {
                filtered_input = to_std_string(filtered_result);
              }
            }

            // Call on_handoff callback if defined
            if (h.on_handoff)
            {
              Value context_val = Value::String(handoff_input.c_str(), handoff_input.size());
              call_function(h.on_handoff, {context_val}, false, "");
            }

            // Perform the handoff
            handoff_target = h.target_agent;
            current_agent = handoff_target;
            current_query = filtered_input;
            performed_handoff = true;

            trace_entry.was_handoff = true;
            trace_entry.handoff_to = handoff_target;
            trace_entry.action = "handoff";

            output_stream() << "[Runner] Handoff: " << trace_entry.agent_name << " -> "
                            << handoff_target << "\n";
            break;
          }
        }
      }
    }

    // Calculate turn duration and set action
    trace_entry.duration_ms = current_time_ms() - turn_start;
    if (!trace_entry.was_handoff)
    {
      trace_entry.action = "response";
    }

    // Accumulate totals
    result.total_input_tokens += trace_entry.input_tokens;
    result.total_output_tokens += trace_entry.output_tokens;

    result.trace.push_back(std::move(trace_entry));
    result.total_turns = turn + 1;
    result.final_agent = current_agent;

    if (!performed_handoff)
    {
      // No handoff, loop is complete
      result.completed = true;
      result.final_output = Value::String(response.c_str(), response.size());
      break;
    }
  }

  if (result.total_turns >= max_turns && !result.completed)
  {
    result.error_message = "Max turns reached without completion";
    result.completed = false;
  }

  // Calculate total duration
  result.end_time = current_time_ms();
  result.total_duration_ms = result.end_time - result.start_time;

  return result;
}
void VirtualMachine::set_memory_backend(std::unique_ptr<MemoryBackend> backend)
{
  if (backend)
  {
    memory_backend_ = std::move(backend);
  }
}

// ============================================================================
// Voice Pipeline Operations
// ============================================================================

void VirtualMachine::register_voice_pipeline(const std::string& name, const VoicePipelineDef& def)
{
  voice_pipelines_[name] = def;
}

const VirtualMachine::VoicePipelineDef* VirtualMachine::get_voice_pipeline(
    const std::string& name) const
{
  auto it = voice_pipelines_.find(name);
  if (it == voice_pipelines_.end())
  {
    return nullptr;
  }
  return &it->second;
}

std::string VirtualMachine::transcribe_audio(const std::string& pipeline_name,
                                              const std::string& audio_path)
{
  const auto* def = get_voice_pipeline(pipeline_name);
  if (!def)
  {
    throw std::runtime_error("Unknown voice pipeline: " + pipeline_name);
  }
  voice::STTConfig config;
  config.provider = def->stt_provider;
  config.model = def->stt_model;
  config.endpoint = def->stt_endpoint;
  config.language = def->stt_language;
  config.format = def->stt_format;
  auto stt = voice::create_stt_provider(config);
  return stt->transcribe(audio_path);
}

std::string VirtualMachine::synthesize_speech(const std::string& pipeline_name,
                                               const std::string& text,
                                               const std::string& output_path)
{
  const auto* def = get_voice_pipeline(pipeline_name);
  if (!def)
  {
    throw std::runtime_error("Unknown voice pipeline: " + pipeline_name);
  }
  voice::TTSConfig config;
  config.provider = def->tts_provider;
  config.model = def->tts_model;
  config.voice = def->tts_voice;
  config.endpoint = def->tts_endpoint;
  config.format = def->tts_format;
  config.speed = def->tts_speed;
  config.instructions = def->tts_instructions;
  auto tts = voice::create_tts_provider(config);
  tts->synthesize(text, output_path);
  return output_path;
}
// ============================================================================
// Realtime Voice Operations
// ============================================================================

void VirtualMachine::register_realtime_voice(const std::string& name,
                                              const RealtimeVoiceDef& def)
{
  realtime_voices_[name] = def;
}

const VirtualMachine::RealtimeVoiceDef* VirtualMachine::get_realtime_voice(
    const std::string& name) const
{
  auto it = realtime_voices_.find(name);
  if (it == realtime_voices_.end())
  {
    return nullptr;
  }
  return &it->second;
}

std::string VirtualMachine::realtime_connect(const std::string& config_name)
{
  const auto* def = get_realtime_voice(config_name);
  if (!def)
  {
    throw std::runtime_error("Unknown realtime voice config: " + config_name);
  }

  voice::RealtimeVoiceConfig config;
  config.provider = def->provider;
  config.model = def->model;
  config.voice = def->voice;
  config.vad = def->vad;
  config.vad_threshold = def->vad_threshold;
  config.silence_duration_ms = def->silence_duration_ms;
  config.input_format = def->input_format;
  config.output_format = def->output_format;
  config.sample_rate = def->sample_rate;
  config.speed = def->speed;
  config.stt_endpoint = def->stt_endpoint;
  config.tts_endpoint = def->tts_endpoint;
  config.llm_endpoint = def->llm_endpoint;

  auto session = voice::create_realtime_session(config);
  session->connect();

  std::string session_id = "rt_session_" + std::to_string(next_session_id_++);
  realtime_sessions_[session_id] = std::move(session);
  return session_id;
}

std::shared_ptr<voice::RealtimeVoiceSession> VirtualMachine::get_realtime_session(
    const std::string& session_id)
{
  auto it = realtime_sessions_.find(session_id);
  if (it == realtime_sessions_.end())
  {
    return nullptr;
  }
  return it->second;
}
// =============================================================================
// Cognitive Feature Implementations (v0.5.0)
// =============================================================================

std::string VirtualMachine::apply_reasoning_strategy(
    ObjAgent* agent, llm::LLMProvider* provider,
    const std::string& system_prompt, const std::string& query,
    ReasoningMode mode, const ReasoningConfig& config,
    const std::vector<HandoffDef>* handoffs)
{
  auto build_messages = [&](const std::string& sys, const std::string& user_msg) -> std::vector<llm::Message>
  {
    std::vector<llm::Message> msgs;
    if (!sys.empty()) msgs.push_back({"system", sys});
    if (agent->context)
    {
      for (const auto& m : agent->context->history)
      {
        msgs.push_back({m.role, m.content});
      }
    }
    msgs.push_back({"user", user_msg});
    return msgs;
  };

  auto update_context = [&](const std::string& response)
  {
    if (!agent->context) agent->context = new_context();
    agent->context->history.push_back({"user", query});
    agent->context->history.push_back({"assistant", response});
  };

  std::string result;

  switch (mode)
  {
    case ReasoningMode::kChainOfThought:
    {
      // Prepend chain-of-thought instruction to system prompt
      std::string cot_system = system_prompt;
      cot_system += "\n\n## Reasoning Mode: Chain of Thought\n"
                    "Think step by step before giving your final answer. "
                    "Show your reasoning process clearly, then provide the final answer.\n"
                    "Format:\n"
                    "**Thinking:**\n<your step-by-step reasoning>\n\n"
                    "**Answer:**\n<your final answer>";
      auto msgs = build_messages(cot_system, query);
      result = provider->chat(msgs);
      break;
    }
    case ReasoningMode::kPlanAndExecute:
    {
      // Step 1: Generate a plan
      std::string plan_system = system_prompt;
      plan_system += "\n\n## Reasoning Mode: Plan and Execute\n"
                     "First, create a numbered plan of steps to answer the query. "
                     "Output ONLY the plan as a numbered list. Do not execute yet.";
      auto plan_msgs = build_messages(plan_system, query);
      std::string plan = provider->chat(plan_msgs);

      // Step 2: Execute the plan
      std::string exec_system = system_prompt;
      exec_system += "\n\n## Executing Plan\n"
                     "You previously created this plan:\n" + plan + "\n\n"
                     "Now execute each step and provide a comprehensive final answer.";
      auto exec_msgs = build_messages(exec_system, query);
      result = provider->chat(exec_msgs);
      break;
    }
    case ReasoningMode::kTreeOfThought:
    {
      // Generate N branches, score them, pick the best
      size_t branches = config.tree_branches > 0 ? config.tree_branches : 3;
      std::string branch_system = system_prompt;
      branch_system += "\n\n## Reasoning Mode: Tree of Thought\n"
                       "Generate " + std::to_string(branches) + " different approaches to answer this query. "
                       "For each approach, provide:\n"
                       "**Approach N:** <brief description>\n"
                       "**Reasoning:** <step-by-step reasoning>\n"
                       "**Confidence:** <0.0-1.0>\n"
                       "**Answer:** <answer for this approach>\n\n"
                       "After all approaches, select the best one and provide:\n"
                       "**Selected Approach:** N\n"
                       "**Final Answer:** <the chosen answer>";
      auto msgs = build_messages(branch_system, query);
      result = provider->chat(msgs);
      break;
    }
    case ReasoningMode::kSelfConsistency:
    {
      // Generate N responses and take majority/synthesize
      size_t samples = config.consistency_samples > 0 ? config.consistency_samples : 3;
      std::vector<std::string> responses;
      responses.reserve(samples);
      for (size_t i = 0; i < samples; ++i)
      {
        auto msgs = build_messages(system_prompt, query);
        responses.push_back(provider->chat(msgs));
      }

      // Synthesize: ask the LLM to pick the consensus
      std::string synthesis_system = system_prompt;
      synthesis_system += "\n\n## Self-Consistency Synthesis\n"
                          "You generated multiple responses to the same query. "
                          "Analyze them for consistency and provide the best answer.\n\n";
      for (size_t i = 0; i < responses.size(); ++i)
      {
        synthesis_system += "**Response " + std::to_string(i + 1) + ":**\n" + responses[i] + "\n\n";
      }
      synthesis_system += "Provide the most consistent and accurate final answer.";
      auto synth_msgs = build_messages(synthesis_system, "Synthesize the above responses into a final answer for: " + query);
      result = provider->chat(synth_msgs);
      break;
    }
    default:
    {
      // Fallback: standard call
      auto msgs = build_messages(system_prompt, query);
      result = provider->chat(msgs);
      break;
    }
  }

  update_context(result);
  return result;
}

std::string VirtualMachine::apply_reflection(
    ObjAgent* agent, llm::LLMProvider* provider,
    const std::string& agent_name, const std::string& query,
    const std::string& response, const ReflectionConfig& config)
{
  // Build evaluation prompt
  std::string eval_prompt = "You are a quality evaluator. Evaluate the following response.\n\n"
                            "**Query:** " + query + "\n\n"
                            "**Response:** " + response + "\n\n"
                            "Evaluate on these dimensions: ";
  for (size_t i = 0; i < config.dimensions.size(); ++i)
  {
    if (i > 0) eval_prompt += ", ";
    eval_prompt += config.dimensions[i];
  }
  eval_prompt += "\n\nRespond with valid JSON:\n"
                 "{\n";
  for (const auto& dim : config.dimensions)
  {
    eval_prompt += "  \"" + dim + "\": <0.0-1.0>,\n";
  }
  eval_prompt += "  \"overall_confidence\": <0.0-1.0>,\n"
                 "  \"reasoning\": \"<brief explanation>\"\n"
                 "}";

  std::vector<llm::Message> eval_msgs = {
    {"system", "You are a response quality evaluator. Always respond with valid JSON only."},
    {"user", eval_prompt}
  };

  std::string eval_result_str = provider->chat(eval_msgs);

  // Parse the evaluation result
  nlohmann::json eval_result;
  try
  {
    // Try to extract JSON from the response (may have markdown code blocks)
    auto json_start = eval_result_str.find('{');
    auto json_end = eval_result_str.rfind('}');
    if (json_start != std::string::npos && json_end != std::string::npos)
    {
      eval_result = nlohmann::json::parse(eval_result_str.substr(json_start, json_end - json_start + 1));
    }
  }
  catch (...)
  {
    // If parsing fails, create a default result
    eval_result = {{"overall_confidence", 0.5}, {"reasoning", "Could not parse evaluation"}};
  }

  // Store reflection result
  agent_reflection_results_[agent_name] = eval_result;

  double confidence = eval_result.value("overall_confidence", 0.5);
  if (confidence >= config.min_confidence)
  {
    return response;  // Response meets quality threshold
  }

  // Apply low-quality strategy
  switch (config.strategy)
  {
    case LowQualityStrategy::kRevise:
    {
      std::string revised = response;
      for (size_t rev = 0; rev < config.max_revisions; ++rev)
      {
        std::string revise_prompt = "Your previous response was evaluated and scored " +
                                     std::to_string(confidence) + " confidence.\n"
                                     "Evaluation: " + eval_result.dump() + "\n\n"
                                     "Original query: " + query + "\n"
                                     "Previous response: " + revised + "\n\n"
                                     "Please provide an improved response addressing the evaluation feedback.";
        std::vector<llm::Message> revise_msgs = {
          {"system", agent->system ? to_std_string(agent->system) : ""},
          {"user", revise_prompt}
        };
        revised = provider->chat(revise_msgs);

        // Re-evaluate
        eval_msgs[1].content = "You are a quality evaluator. Evaluate the following response.\n\n"
                               "**Query:** " + query + "\n\n"
                               "**Response:** " + revised + "\n\n"
                               "Respond with valid JSON with overall_confidence (0.0-1.0).";
        std::string re_eval_str = provider->chat(eval_msgs);
        try
        {
          auto js = re_eval_str.find('{');
          auto je = re_eval_str.rfind('}');
          if (js != std::string::npos && je != std::string::npos)
          {
            auto re_eval = nlohmann::json::parse(re_eval_str.substr(js, je - js + 1));
            confidence = re_eval.value("overall_confidence", 0.5);
            agent_reflection_results_[agent_name] = re_eval;
            if (confidence >= config.min_confidence) break;
          }
        }
        catch (...) {}
      }
      return revised;
    }
    case LowQualityStrategy::kRetry:
    {
      // Simply retry the original call
      std::vector<llm::Message> retry_msgs;
      if (agent->system) retry_msgs.push_back({"system", to_std_string(agent->system)});
      retry_msgs.push_back({"user", query});
      return provider->chat(retry_msgs);
    }
    case LowQualityStrategy::kEscalate:
    {
      if (!config.escalate_to.empty())
      {
        // Delegate to another agent
        return call_agent_internal(config.escalate_to, query);
      }
      return response;  // No escalation target
    }
    case LowQualityStrategy::kAcknowledge:
    default:
      return response + "\n\n[Note: This response scored " + std::to_string(confidence) +
             " confidence, below the threshold of " + std::to_string(config.min_confidence) + ".]";
  }
}

void VirtualMachine::trigger_learning_review(const std::string& agent_name,
                                              llm::LLMProvider* provider,
                                              const LearningConfig& config)
{
  // Load recent interactions from SQLite
  if (!memory_backend_) return;

  auto events = memory_backend_->search_events("learning_" + agent_name, "", 50);
  if (events.empty()) return;

  // Build review prompt
  std::string review_prompt = "You are a learning analyst. Review these recent interactions for agent '" +
                              agent_name + "' and extract lessons.\n\n";
  for (const auto& evt : events)
  {
    review_prompt += "---\n" + evt.data + "\n";
  }
  review_prompt += "\n---\nProvide your analysis as JSON:\n"
                   "{\n"
                   "  \"patterns\": [\"<pattern1>\", ...],\n"
                   "  \"strengths\": [\"<strength1>\", ...],\n"
                   "  \"weaknesses\": [\"<weakness1>\", ...],\n"
                   "  \"prompt_suggestion\": \"<suggested addition to system prompt>\",\n"
                   "  \"avg_quality\": <0.0-1.0>\n"
                   "}";

  std::vector<llm::Message> msgs = {
    {"system", "You are a learning analyst. Always respond with valid JSON only."},
    {"user", review_prompt}
  };

  std::string review_result = provider->chat(msgs);

  // Store review result
  MemoryEventRecord review_event;
  review_event.timestamp = current_time_ms();
  review_event.type = "learning_review";
  review_event.data = review_result;
  review_event.agent = agent_name;
  memory_backend_->store_event("learning_reviews_" + agent_name, review_event);
}

void VirtualMachine::apply_evolution(const std::string& agent_name,
                                      llm::LLMProvider* provider,
                                      const EvolutionConfig& config)
{
  // Load learning reviews
  if (!memory_backend_) return;

  auto reviews = memory_backend_->load_events("learning_reviews_" + agent_name);
  if (reviews.empty()) return;

  // Get current prompt
  const Value* agent_value = find_global_value(globals_, agent_name);
  if (!agent_value || !agent_value->is_agent()) return;
  auto* agent = as_agent(*agent_value);
  std::string current_prompt = agent->system ? to_std_string(agent->system) : "";

  // Check for evolved version
  auto evolved_it = agent_evolved_prompts_.find(agent_name);
  if (evolved_it != agent_evolved_prompts_.end())
  {
    current_prompt = evolved_it->second;
  }

  // Build evolution prompt
  std::string evo_prompt = "You are a prompt engineer. Based on performance data, evolve this system prompt.\n\n"
                           "**Current System Prompt:**\n" + current_prompt + "\n\n"
                           "**Core Identity (MUST preserve):**\n" + config.core_identity + "\n\n"
                           "**Mutable fields:** ";
  for (const auto& f : config.mutable_fields) evo_prompt += f + ", ";
  evo_prompt += "\n\n**Recent Reviews:**\n";
  for (const auto& r : reviews)
  {
    evo_prompt += r.data + "\n---\n";
  }
  evo_prompt += "\nProvide the evolved system prompt. It MUST contain the core identity. "
                "Only modify the mutable aspects. Output the prompt text only, no explanations.";

  std::vector<llm::Message> msgs = {
    {"system", "You are a prompt engineer. Output only the evolved prompt text."},
    {"user", evo_prompt}
  };

  std::string evolved_prompt = provider->chat(msgs);

  // Validate core identity is preserved
  if (!config.core_identity.empty() && evolved_prompt.find(config.core_identity) == std::string::npos)
  {
    // Core identity not found — prepend it
    evolved_prompt = config.core_identity + "\n\n" + evolved_prompt;
  }

  // Store evolved version
  size_t version = agent_evolution_versions_[agent_name] + 1;
  agent_evolution_versions_[agent_name] = version;
  agent_evolved_prompts_[agent_name] = evolved_prompt;

  // Persist to SQLite
  if (memory_backend_)
  {
    MemoryEventRecord evt;
    evt.timestamp = current_time_ms();
    evt.type = "prompt_evolution";
    nlohmann::json data;
    data["version"] = version;
    data["prompt"] = evolved_prompt;
    data["original"] = current_prompt;
    evt.data = data.dump();
    evt.agent = agent_name;
    memory_backend_->store_event("prompt_evolution_" + agent_name, evt);
  }
}

}  // namespace neamc::vm
