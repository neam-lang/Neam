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
#include <filesystem>
#include <unordered_map>

#include "neamc/llm/provider_factory.hpp"
#include "neamc/security/audit_log.hpp"
#include "neamc/security/behavioral_monitor.hpp"
#include "neamc/security/human_in_the_loop.hpp"
#include "neamc/security/injection_scanner.hpp"
#include "neamc/security/rate_limiter.hpp"
#include "neamc/vm/async/future.hpp"
#include "neamc/vm/external_skill.hpp"
#include "neamc/vm/struct_type.hpp"
#include "neamc/vm/sealed_type.hpp"
#include "neamc/vm/claw_agent_type.hpp"
#include "neamc/vm/forge_agent_type.hpp"
#include "neamc/vm/harness_types.hpp"  // v1.4.5 Phase 3-minimal
#include "neamc/vm/data_agent_types.hpp"
#include "neamc/vm/etl_agent_types.hpp"
#include "neamc/vm/migration_types.hpp"
#include "neamc/vm/dataops_types.hpp"
#include "neamc/vm/governance_types.hpp"
#include "neamc/vm/modeling_types.hpp"
#include "neamc/vm/analyst_types.hpp"
#include "neamc/vm/deploy_types.hpp"
#include "neamc/vm/datascientist_types.hpp"
#include "neamc/vm/causal_types.hpp"
#include "neamc/vm/mlops_types.hpp"
#include "neamc/vm/databa_types.hpp"
#include "neamc/vm/datatest_types.hpp"
#include "neamc/vm/dio_types.hpp"
#include "neamc/vm/neamos_types.hpp"
#include "neamc/vm/prod_types.hpp"
#include "neamc/vm/lab_types.hpp"
#include "neamc/vm/forge_loop.hpp"
#include "neamc/vm/session_manager.hpp"
#include "neamc/vm/context_builder.hpp"
#include "neamc/vm/compaction.hpp"
#include "neamc/vm/channels/cli_channel.hpp"
#include "neamc/vm/channels/http_channel.hpp"
#include "neamc/vm/knowledge.hpp"
#include "neamc/vm/memory_index.hpp"
#include "neamc/vm/schema.hpp"
#include "neamc/vm/value_hash.hpp"

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
  if (value.is_range()) return "range";
  if (value.is_set()) return "set";
  if (value.is_tuple()) return "tuple";
  if (value.is_option()) return "option";
  if (value.is_struct()) return "struct";
  if (value.is_struct_def()) return "struct_def";
  if (value.is_claw_agent()) return "claw_agent";
  if (value.is_forge_agent()) return "forge_agent";
  return "object";
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

// v0.8 Phase 5: Check if a type has a specific method in its impl table
bool has_impl_method(const std::unordered_map<std::string, ObjImplTable*>& impl_tables,
                     const std::string& type_name, const std::string& method_name)
{
  auto it = impl_tables.find(type_name);
  if (it == impl_tables.end()) return false;
  return it->second->methods.count(method_name) > 0;
}

// v0.8 Phase 5: Check if a type implements a trait (all required methods present)
bool has_trait_impl(const std::unordered_map<std::string, ObjImplTable*>& impl_tables,
                    const std::unordered_map<std::string, ObjTraitDef*>& trait_defs,
                    const std::string& type_name, const std::string& trait_name)
{
  auto trait_it = trait_defs.find(trait_name);
  if (trait_it == trait_defs.end()) return false;

  auto impl_it = impl_tables.find(type_name);
  if (impl_it == impl_tables.end()) return false;

  // Check that all required methods (no default) are present
  for (const auto& m : trait_it->second->methods)
  {
    if (!m.default_impl && impl_it->second->methods.count(m.name) == 0)
    {
      return false;
    }
  }
  return true;
}

// v0.8 Phase 7: Workspace path resolution helpers
namespace fs = std::filesystem;

std::string resolve_workspace_path(
    const std::unordered_map<std::string, ObjClawAgent*>& claw_agents,
    const std::string& rel_path)
{
  if (claw_agents.empty()) return "";
  auto* claw = claw_agents.begin()->second;
  if (claw->workspace.empty()) return "";

  // Reject path traversal
  if (rel_path.find("..") != std::string::npos) return "";

  fs::path ws(claw->workspace);
  fs::path full = ws / rel_path;

  // Verify canonical stays within workspace
  fs::path canonical_ws = fs::weakly_canonical(ws);
  fs::path canonical_full = fs::weakly_canonical(full);
  auto ws_str = canonical_ws.string();
  auto full_str = canonical_full.string();
  if (full_str.rfind(ws_str, 0) != 0) return "";

  return full.string();
}

void ensure_parent_dirs(const std::string& path)
{
  fs::path p(path);
  if (p.has_parent_path())
  {
    fs::create_directories(p.parent_path());
  }
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
    case OpCode::OP_DEFINE_EXTERN_SKILL:
      return "OP_DEFINE_EXTERN_SKILL";
    case OpCode::OP_DEFINE_MCP_SERVER:
      return "OP_DEFINE_MCP_SERVER";
    case OpCode::OP_ADOPT_MCP_TOOLS:
      return "OP_ADOPT_MCP_TOOLS";
    case OpCode::OP_SET_INDEX:
      return "OP_SET_INDEX";
    case OpCode::OP_GET_ITER:
      return "OP_GET_ITER";
    case OpCode::OP_FOR_ITER:
      return "OP_FOR_ITER";
    case OpCode::OP_BUILD_SET:
      return "OP_BUILD_SET";
    case OpCode::OP_BUILD_TUPLE:
      return "OP_BUILD_TUPLE";
    case OpCode::OP_CONTAINS:
      return "OP_CONTAINS";
    case OpCode::OP_FORMAT_STRING:
      return "OP_FORMAT_STRING";
    case OpCode::OP_UNPACK:
      return "OP_UNPACK";
    case OpCode::OP_UNPACK_REST:
      return "OP_UNPACK_REST";
    case OpCode::OP_SLICE:
      return "OP_SLICE";
    case OpCode::OP_DEFINE_STRUCT:
      return "OP_DEFINE_STRUCT";
    case OpCode::OP_IMPL_METHOD:
      return "OP_IMPL_METHOD";
    case OpCode::OP_CONSTRUCT_NAMED:
      return "OP_CONSTRUCT_NAMED";
    case OpCode::OP_COPY_WITH:
      return "OP_COPY_WITH";
    case OpCode::OP_SET_PROPERTY:
      return "OP_SET_PROPERTY";
    case OpCode::OP_SET_FIELD_OBSERVER:
      return "OP_SET_FIELD_OBSERVER";
    case OpCode::OP_DEFINE_CLAW_AGENT:
      return "OP_DEFINE_CLAW_AGENT";
    case OpCode::OP_DEFINE_FORGE_AGENT:
      return "OP_DEFINE_FORGE_AGENT";
    case OpCode::OP_DEFINE_CHANNEL:
      return "OP_DEFINE_CHANNEL";
    case OpCode::OP_WORKSPACE_READ:
      return "OP_WORKSPACE_READ";
    case OpCode::OP_WORKSPACE_WRITE:
      return "OP_WORKSPACE_WRITE";
    case OpCode::OP_MEMORY_SEARCH:
      return "OP_MEMORY_SEARCH";
    case OpCode::OP_SPAWN_AGENT:
      return "OP_SPAWN_AGENT";
    case OpCode::OP_FORGE_ITERATE:
      return "OP_FORGE_ITERATE";
    case OpCode::OP_VERIFY:
      return "OP_VERIFY";
    case OpCode::OP_COMPACT:
      return "OP_COMPACT";
    case OpCode::OP_FLUSH:
      return "OP_FLUSH";
    case OpCode::OP_SESSION_HISTORY:
      return "OP_SESSION_HISTORY";
    case OpCode::OP_FORGE_RUN:
      return "OP_FORGE_RUN";
    case OpCode::OP_DEFINE_DATA_AGENT:
      return "OP_DEFINE_DATA_AGENT";
    case OpCode::OP_DEFINE_SOURCE:
      return "OP_DEFINE_SOURCE";
    case OpCode::OP_DEFINE_SINK:
      return "OP_DEFINE_SINK";
    case OpCode::OP_DEFINE_SCHEMA:
      return "OP_DEFINE_SCHEMA";
    case OpCode::OP_DEFINE_COMPUTE:
      return "OP_DEFINE_COMPUTE";
    case OpCode::OP_DEFINE_QUALITY:
      return "OP_DEFINE_QUALITY";
    case OpCode::OP_DEFINE_GOVERNANCE:
      return "OP_DEFINE_GOVERNANCE";
    case OpCode::OP_DEFINE_CATALOG:
      return "OP_DEFINE_CATALOG";
    case OpCode::OP_DEFINE_ETL_AGENT:
      return "OP_DEFINE_ETL_AGENT";
    case OpCode::OP_DEFINE_MART:
      return "OP_DEFINE_MART";
    case OpCode::OP_DEFINE_SEMANTIC:
      return "OP_DEFINE_SEMANTIC";
    case OpCode::OP_SQL_TRANSPILE:
      return "OP_SQL_TRANSPILE";
    case OpCode::OP_SQL_PUSHDOWN:
      return "OP_SQL_PUSHDOWN";
    case OpCode::OP_NL2SQL:
      return "OP_NL2SQL";
    case OpCode::OP_AUTO_MODEL:
      return "OP_AUTO_MODEL";
    case OpCode::OP_SELF_HEAL:
      return "OP_SELF_HEAL";
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
  set_current_vm(this);
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

  // v0.8 Phase 5: Register built-in trait definitions
  register_builtin_traits();
}

VirtualMachine::~VirtualMachine()
{
  set_current_vm(this);  // Ensure we're current for cleanup
  free_objects(*this);
  if (get_current_vm() == this)
  {
    set_current_vm(nullptr);
  }
}

// v0.8 Phase 5: Create a minimal ObjFunction that returns nil (OP_NIL + OP_RETURN)
static ObjFunction* make_nil_function(const std::string& name, int arity)
{
  auto* fn = new_function();
  fn->arity = arity;
  fn->name = copy_string(name.c_str(), name.size());
  fn->chunk.write_op(OpCode::OP_NIL);
  fn->chunk.write_op(OpCode::OP_RETURN);
  return fn;
}

void VirtualMachine::register_builtin_traits()
{
  // Helper lambda: register a single trait with required + default methods
  struct MethodSpec
  {
    std::string name;
    int arity;            // includes 'self' parameter
    bool has_default;     // true = default (nil-returning), false = required
  };

  auto register_trait = [this](const std::string& trait_name,
                               const std::vector<MethodSpec>& methods) {
    auto* def = new_trait_def(trait_name);

    for (const auto& m : methods)
    {
      TraitMethodInfo info;
      info.name = m.name;
      if (m.has_default)
      {
        info.default_impl = make_nil_function(trait_name + "." + m.name, m.arity);
      }
      else
      {
        info.default_impl = nullptr;
      }
      def->methods.push_back(info);
    }

    trait_defs_[trait_name] = def;
    auto* name_str = copy_string(trait_name.c_str(), trait_name.size());
    globals_.set(name_str, Value::TraitDef(def));
  };

  // 1. Schedulable — heartbeat and cron scheduling for agents
  register_trait("Schedulable", {
    {"on_heartbeat",      1, false},   // required: (self)
    {"heartbeat_interval", 1, false},  // required: (self) -> number (ms)
    {"on_cron",           2, true},    // default:  (self, id) -> nil
  });

  // 2. Channelable — inter-agent messaging channels
  register_trait("Channelable", {
    {"channels",    1, false},   // required: (self) -> list of channel names
    {"on_message",  2, false},   // required: (self, msg)
  });

  // 3. Sandboxable — sandbox/isolation configuration
  register_trait("Sandboxable", {
    {"sandbox_config", 1, false},   // required: (self) -> config map
  });

  // 4. Monitorable — behavioral monitoring and anomaly detection
  register_trait("Monitorable", {
    {"baseline",    1, false},   // required: (self) -> baseline map
    {"on_anomaly",  2, false},   // required: (self, event)
  });

  // 5. Orchestrable — multi-agent orchestration callbacks
  register_trait("Orchestrable", {
    {"on_spawn",     2, true},   // default: (self, child) -> nil
    {"on_delegate",  2, true},   // default: (self, result) -> nil
  });

  // 6. Searchable — RAG/search configuration
  register_trait("Searchable", {
    {"search_config", 1, false},   // required: (self) -> config map
    {"on_index",      2, true},    // default:  (self, doc) -> nil
  });
}

void VirtualMachine::reset_for_reuse(ResetPolicy policy)
{
  // Minimal: stack + frames + emitted only. No GC. ~0.01ms
  stack_.clear();
  frames_.clear();
  emitted_.clear();

  if (policy == ResetPolicy::Minimal)
  {
    return;
  }

  // Standard: + gc_roots, skills, budgets, memory, knowledge. Run GC. ~0.05ms
  gc_roots_.clear();
  allowed_skills_.clear();
  budget_trackers_.clear();
  memory_stores_.clear();
  knowledge_bases_.clear();

  // Reset I/O and trace
  input_ = &std::cin;
  output_ = &std::cout;
  trace_logger_ = TraceLogger{};

  // Run GC to reclaim per-request objects
  set_current_vm(this);
  collect_garbage(*this);

  if (policy == ResetPolicy::Standard)
  {
    return;
  }

  // Full: + guards, guardchains, tools, policies, extensions, capabilities. ~0.1ms
  guards_.clear();
  guardchains_.clear();
  tools_.clear();
  policies_.clear();
  agent_extensions_.clear();
  entity_capabilities_.clear();
  budgets_.clear();

  if (policy == ResetPolicy::Full)
  {
    return;
  }

  // Complete: + globals, interned_strings, OOP tables, mcp_clients, channels, lanes. ~5ms
  globals_ = Table{};
  interned_strings_ = Table{};
  struct_defs_.clear();
  impl_tables_.clear();
  trait_defs_.clear();
  sealed_defs_.clear();
  mcp_clients_.clear();
  claw_agents_.clear();
  forge_agents_.clear();
  memory_indices_.clear();
  channel_registry_.clear();
  channel_adapters_.clear();
  if (lane_engine_) { lane_engine_->stop(); lane_engine_.reset(); }
  env_ = nullptr;

  // Re-register core natives and built-in traits after Complete reset
  register_core_natives(*this);
  register_builtin_traits();
  auto* env = new_env();
  auto* std_map = new_map({{"env", Value::Env(env)}});
  auto* std_name = copy_string("std", 3);
  globals_.set(std_name, Value::Map(std_map));
  env_ = env;
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
      ValueEqual eq;
      return eq(lhs, rhs);
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
        if (op == OpCode::OP_ADD)
        {
          if (lhs.is_string() && rhs.is_string())
          {
            stack_.push_back(concatenate(lhs, rhs));
          }
          else if (lhs.is_string())
          {
            auto rhs_str = value_to_string(rhs);
            auto rhs_val = Value::String(rhs_str.c_str(), rhs_str.size());
            stack_.push_back(concatenate(lhs, rhs_val));
          }
          else if (rhs.is_string())
          {
            auto lhs_str = value_to_string(lhs);
            auto lhs_val = Value::String(lhs_str.c_str(), lhs_str.size());
            stack_.push_back(concatenate(lhs_val, rhs));
          }
          else
          {
            stack_.push_back(binary_numeric_op(lhs, rhs, op));
          }
        }
        else if (op == OpCode::OP_MUL && lhs.is_string() && rhs.is_number())
        {
          auto* str = as_string(lhs);
          auto src = std::string(str->chars, str->length);
          int n = static_cast<int>(rhs.as_number());
          std::string result;
          result.reserve(src.size() * std::max(0, n));
          for (int i = 0; i < n; ++i)
          {
            result += src;
          }
          stack_.push_back(Value::String(result.c_str(), result.size()));
        }
        else if (op == OpCode::OP_MUL && lhs.is_number() && rhs.is_string())
        {
          auto* str = as_string(rhs);
          auto src = std::string(str->chars, str->length);
          int n = static_cast<int>(lhs.as_number());
          std::string result;
          result.reserve(src.size() * std::max(0, n));
          for (int i = 0; i < n; ++i)
          {
            result += src;
          }
          stack_.push_back(Value::String(result.c_str(), result.size()));
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
        // v0.7.1: Struct positional construction — Point(3, 4)
        else if (is_obj_type(callee, ObjType::OBJ_STRUCT_DEF))
        {
          auto* def = as_struct_def(callee);
          if (arg_count != def->field_names.size())
          {
            throw std::runtime_error(
                "Struct '" + def->name + "' expects " +
                std::to_string(def->field_names.size()) +
                " argument(s), got " + std::to_string(arg_count));
          }
          std::vector<Value> field_values(arg_count);
          for (std::size_t i = 0; i < arg_count; ++i)
          {
            field_values[arg_count - 1 - i] = pop();
          }
          // Remove callee from stack
          stack_.erase(stack_.begin() + static_cast<std::ptrdiff_t>(callee_index));
          auto* instance = new_struct(def, std::move(field_values));
          stack_.push_back(Value::Struct(instance));
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
        else if (is_obj_type(receiver, ObjType::OBJ_CLAW_AGENT))
        {
          auto* claw = as_claw_agent(receiver);
          if (key == "context")
            stack_.push_back(Value::Context(claw->context));
          else if (key == "provider" && claw->provider)
            stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(claw->provider)));
          else if (key == "model" && claw->model)
            stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(claw->model)));
          else if (key == "name" && claw->name)
            stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(claw->name)));
          else if (key == "skills" && claw->skills)
            stack_.push_back(Value::List(claw->skills));
          else if (key == "max_history_turns")
            stack_.push_back(Value::Number(static_cast<double>(claw->max_history_turns)));
          else if (key == "idle_reset_minutes")
            stack_.push_back(Value::Number(static_cast<double>(claw->idle_reset_minutes)));
          else if (key == "compaction")
            stack_.push_back(Value::String(claw->compaction.c_str(), claw->compaction.size()));
          else if (key == "workspace")
            stack_.push_back(Value::String(claw->workspace.c_str(), claw->workspace.size()));
          else if (key == "channel_names")
          {
            std::vector<Value> items;
            for (const auto& ch : claw->channel_names)
            {
              items.push_back(Value::String(ch.c_str(), ch.size()));
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (key == "lanes")
          {
            std::vector<Value> items;
            for (const auto& lane : claw->lanes)
            {
              std::unordered_map<std::string, Value> entries;
              entries["name"] = Value::String(lane.name.c_str(), lane.name.size());
              entries["concurrency"] = Value::Number(static_cast<double>(lane.concurrency));
              entries["priority"] = Value::String(lane.priority.c_str(), lane.priority.size());
              items.push_back(Value::Map(new_map(std::move(entries))));
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else
            throw std::runtime_error("Property error: unknown claw agent property '" + key + "'");
        }
        else if (is_obj_type(receiver, ObjType::OBJ_FORGE_AGENT))
        {
          auto* forge = as_forge_agent(receiver);
          if (key == "context")
            stack_.push_back(Value::Context(forge->context));
          else if (key == "provider" && forge->provider)
            stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(forge->provider)));
          else if (key == "model" && forge->model)
            stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(forge->model)));
          else if (key == "name" && forge->name)
            stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(forge->name)));
          else if (key == "skills" && forge->skills)
            stack_.push_back(Value::List(forge->skills));
          else if (key == "max_iterations")
            stack_.push_back(Value::Number(static_cast<double>(forge->loop_config.max_iterations)));
          else if (key == "max_cost")
            stack_.push_back(Value::Number(forge->loop_config.max_cost));
          else if (key == "checkpoint")
            stack_.push_back(Value::String(forge->checkpoint.c_str(), forge->checkpoint.size()));
          else if (key == "workspace")
            stack_.push_back(Value::String(forge->workspace.c_str(), forge->workspace.size()));
          else
            throw std::runtime_error("Property error: unknown forge agent property '" + key + "'");
        }
        // v0.7.0: Tuple positional access (.0, .1, ...)
        else if (is_obj_type(receiver, ObjType::OBJ_TUPLE))
        {
          auto* tuple = as_tuple(receiver);
          // Try numeric key
          bool is_numeric = !key.empty();
          for (char ch : key) { if (ch < '0' || ch > '9') { is_numeric = false; break; } }
          if (is_numeric)
          {
            auto idx = static_cast<std::size_t>(std::stoul(key));
            if (idx >= tuple->items.size())
            {
              throw std::runtime_error(
                  "Index out of range: tuple index " + key +
                  " for tuple of size " + std::to_string(tuple->items.size()));
            }
            stack_.push_back(tuple->items[idx]);
          }
          else
          {
            throw std::runtime_error(
                "Property error: unknown tuple property '" + key + "'");
          }
        }
        // v0.7.0: Option property access (.value, .has_value)
        else if (is_obj_type(receiver, ObjType::OBJ_OPTION))
        {
          auto* opt = as_option(receiver);
          if (key == "value")
          {
            stack_.push_back(opt->has_value ? opt->value : Value::Nil());
          }
          else if (key == "has_value")
          {
            stack_.push_back(Value::Bool(opt->has_value));
          }
          else
          {
            throw std::runtime_error(
                "Property error: unknown option property '" + key + "'");
          }
        }
        // v0.7.0: Range property access (.start, .end, .step)
        else if (is_obj_type(receiver, ObjType::OBJ_RANGE))
        {
          auto* range = as_range(receiver);
          if (key == "start") stack_.push_back(Value::Number(static_cast<double>(range->start)));
          else if (key == "end") stack_.push_back(Value::Number(static_cast<double>(range->end)));
          else if (key == "step") stack_.push_back(Value::Number(static_cast<double>(range->step)));
          else throw std::runtime_error("Property error: unknown range property '" + key + "'");
        }
        // v0.7.1: Struct field access
        else if (is_obj_type(receiver, ObjType::OBJ_STRUCT))
        {
          auto* obj = as_struct(receiver);
          int idx = obj->def->field_index(key);
          if (idx < 0)
          {
            throw std::runtime_error(
                "Property error: '" + key + "' not found on struct '" +
                obj->def->name + "'");
          }
          stack_.push_back(obj->fields[static_cast<std::size_t>(idx)]);
        }
        // v0.7.1 Phase 2: Sealed def property — unit variant access (Shape.Point)
        else if (is_obj_type(receiver, ObjType::OBJ_SEALED_DEF))
        {
          auto* def = as_sealed_def(receiver);
          auto tag_it = def->variant_index.find(key);
          if (tag_it != def->variant_index.end())
          {
            const auto& variant_info = def->variants[tag_it->second];
            if (variant_info.fields.empty())
            {
              // Unit variant — construct immediately
              auto* v = new_variant(def, tag_it->second, {});
              stack_.push_back(Value::Variant(v));
            }
            else
            {
              // Has fields — need to call with args, push a placeholder
              // This will be handled by OP_INVOKE (Shape.Circle(5))
              throw std::runtime_error(
                  "Variant '" + key + "' has fields — use " + def->name + "." + key + "(...) syntax");
            }
          }
          else
          {
            throw std::runtime_error(
                "Property error: '" + key + "' not found on sealed type '" + def->name + "'");
          }
        }
        // v0.7.1 Phase 2: Variant field access
        else if (is_obj_type(receiver, ObjType::OBJ_VARIANT))
        {
          auto* v = as_variant(receiver);
          const auto& info = v->sealed_def->variants[v->tag];
          bool found = false;
          for (const auto& field : info.fields)
          {
            if (field.name == key)
            {
              stack_.push_back(v->field_values[field.index]);
              found = true;
              break;
            }
          }
          if (!found)
          {
            throw std::runtime_error(
                "Property error: '" + key + "' not found on variant '" +
                info.name + "' of sealed type '" + v->sealed_def->name + "'");
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
          auto raw = static_cast<int64_t>(index_value.as_number());
          auto* list = as_list(base_value);
          auto len = static_cast<int64_t>(list->items.size());
          if (raw < 0) raw += len;
          if (raw < 0 || raw >= len)
          {
            throw std::runtime_error(
                "Index out of range: index " + std::to_string(raw) +
                " not valid for list of size " + std::to_string(len));
          }
          stack_.push_back(list->items[static_cast<std::size_t>(raw)]);
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
        else if (is_obj_type(base_value, ObjType::OBJ_STRING))
        {
          if (!index_value.is_number())
          {
            throw std::runtime_error(
                "Type error: string index must be a number, got " +
                value_type_name(index_value));
          }
          auto* str = as_string(base_value);
          auto raw = static_cast<int64_t>(index_value.as_number());
          auto len = static_cast<int64_t>(str->length);
          if (raw < 0) raw += len;
          if (raw < 0 || raw >= len)
          {
            throw std::runtime_error(
                "Index out of range: index " + std::to_string(raw) +
                " not valid for string of length " + std::to_string(len));
          }
          stack_.push_back(Value::String(str->chars + raw, 1));
        }
        else if (is_obj_type(base_value, ObjType::OBJ_TUPLE))
        {
          if (!index_value.is_number())
          {
            throw std::runtime_error(
                "Type error: tuple index must be a number, got " +
                value_type_name(index_value));
          }
          auto* tuple = as_tuple(base_value);
          auto raw = static_cast<int64_t>(index_value.as_number());
          auto len = static_cast<int64_t>(tuple->items.size());
          if (raw < 0) raw += len;
          if (raw < 0 || raw >= len)
          {
            throw std::runtime_error(
                "Index out of range: index " + std::to_string(raw) +
                " not valid for tuple of size " + std::to_string(len));
          }
          stack_.push_back(tuple->items[static_cast<std::size_t>(raw)]);
        }
        else
        {
          throw std::runtime_error(
              "Indexing is only supported on lists, maps, strings, and tuples");
        }
        break;
      }
      case OpCode::OP_SET_INDEX:
      {
        Value val = pop();
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
          auto raw = static_cast<int64_t>(index_value.as_number());
          auto* list = as_list(base_value);
          auto len = static_cast<int64_t>(list->items.size());
          if (raw < 0) raw += len;
          if (raw < 0 || raw >= len)
          {
            throw std::runtime_error(
                "Index out of range: index " + std::to_string(raw) +
                " not valid for list of size " + std::to_string(len));
          }
          list->items[static_cast<std::size_t>(raw)] = val;
          stack_.push_back(val);
        }
        else if (is_obj_type(base_value, ObjType::OBJ_MAP))
        {
          const std::string key = to_std_string(index_value);
          auto* map = as_map(base_value);
          map->entries[key] = val;
          stack_.push_back(val);
        }
        else
        {
          throw std::runtime_error(
              "Index assignment is only supported on lists and maps");
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
            // v0.8 Phase 5: Fall through to impl_tables_ for trait methods
            const std::string agent_type_name(agent->name->chars, agent->name->length);
            auto table_it = impl_tables_.find(agent_type_name);
            if (table_it != impl_tables_.end())
            {
              auto method_it = table_it->second->methods.find(method);
              if (method_it != table_it->second->methods.end())
              {
                auto* fn = method_it->second.function;
                std::size_t base_slot = stack_.size();
                stack_.push_back(receiver);  // self
                for (std::size_t i = 0; i < arg_count; ++i)
                {
                  stack_.push_back(args[i]);
                }
                frames_.push_back(CallFrame{&fn->chunk, fn, 0, base_slot, false, {}});
              }
              else
              {
                throw std::runtime_error(
                    "Method '" + method + "' not found on agent '" + agent_type_name + "'");
              }
            }
            else
            {
              throw std::runtime_error("Unknown agent method");
            }
          }
        }
        else if (is_obj_type(receiver, ObjType::OBJ_CLAW_AGENT))
        {
          auto* claw = as_claw_agent(receiver);
          if (method == "ask")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("ClawAgent.ask expects 1 argument");
            }
            std::string query = to_std_string(args[0]);
            const std::string claw_name(claw->name->chars, claw->name->length);

            // v0.8 Phase 3: Session-aware claw .ask()
            SessionManager sm;
            auto& session = sm.get_or_create(claw, "default");
            sm.append_message(claw, "default", "user", query);

            // Also keep context in sync for backward compat
            if (!claw->context)
            {
              claw->context = new_context();
            }
            claw->context->history.push_back({"user", query});

            // v0.6.9 D8: Agent recursion depth guard
            static thread_local int claw_depth = 0;
            struct ClawDepthGuard {
              ClawDepthGuard() { ++claw_depth; }
              ~ClawDepthGuard() { --claw_depth; }
            } claw_depth_guard;

            {
              int max_depth =
                  security::RateLimiter::instance().input_limits().max_agent_depth;
              if (claw_depth > max_depth)
              {
                security::AuditLogger::instance().log_tool_call(
                    security::TraceContext::current(), claw_name, "claw.ask",
                    "", 0, "error:depth_exceeded");
                std::string depth_err =
                    "Agent call depth exceeded (max " + std::to_string(max_depth) + ")";
                stack_.push_back(Value::String(depth_err.c_str(), depth_err.size()));
                break;
              }
            }

            // v0.6.9 D9: Kill switch
            if (security::BehavioralMonitor::instance().is_disabled(claw_name))
            {
              std::string disabled_err = "Agent '" + claw_name + "' is disabled";
              stack_.push_back(Value::String(disabled_err.c_str(), disabled_err.size()));
              break;
            }

            auto claw_exec_start = std::chrono::steady_clock::now();
            int claw_tool_call_count = 0;
            std::vector<std::string> claw_tools_used;

            // Build ProviderConfig
            llm::ProviderConfig config;
            config.model = to_std_string(claw->model);
            config.endpoint = to_std_string(claw->endpoint);
            config.api_key = to_std_string(claw->api_key_env);
            config.temperature = claw->temperature;
            config.default_host =
                resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST",
                                   "http://localhost:11434");
            std::string provider_name = to_std_string(claw->provider);

            // Resolve api_key env var
            if (!config.api_key.empty())
            {
              if (const char* env_val = std::getenv(config.api_key.c_str()))
                config.api_key = env_val;
              else
                config.api_key.clear();
            }

            auto provider = llm::create_provider(provider_name, config);

            // v0.8 Phase 3: Context building with token awareness
            ContextBuilder ctx_builder;
            AssembledContext assembled = ctx_builder.build(claw, session);

            // v0.8 Phase 3: Auto-compaction check
            if (claw->compaction == "auto")
            {
              CompactionEngine ce;
              if (ce.needs_compaction(claw, session))
              {
                auto summary = ce.compact(claw, session,
                    [&](const std::string& prompt) {
                      return provider->complete(prompt);
                    });
                if (!summary.empty())
                {
                  // Rebuild context after compaction
                  assembled = ctx_builder.build(claw, session);
                  // Persist compaction
                  sm.compact(claw, "default", summary);
                }
              }
            }

            // v0.8 Phase 7: Inject memory context into system prompt
            if (!claw->memory_search.empty() && claw->memory_search != "none")
            {
              auto mi_it = memory_indices_.find(claw_name);
              if (mi_it != memory_indices_.end())
              {
                auto mem_results = mi_it->second->search(query, 3);
                if (!mem_results.empty())
                {
                  std::string mem_section = "\n\n[Relevant memory context]\n";
                  for (const auto& mr : mem_results)
                  {
                    mem_section += "- " + mr.file_path + ": " + mr.chunk + "\n";
                  }
                  assembled.system_prompt += mem_section;
                }
              }
            }

            // Collect tool definitions from claw skills
            auto claw_collect_tools = [&]() -> std::vector<llm::ToolDefinition> {
              std::vector<llm::ToolDefinition> tool_defs;
              if (!claw->skills || claw->skills->items.empty())
              {
                return tool_defs;
              }
              for (const auto& skill_value : claw->skills->items)
              {
                ObjSkill* skill = nullptr;
                if (skill_value.is_skill())
                {
                  skill = as_skill(skill_value);
                }
                else if (skill_value.is_string())
                {
                  const std::string skill_name = to_std_string(skill_value);
                  const Value* resolved = find_global_value(globals_, skill_name);
                  if (resolved && resolved->is_skill())
                  {
                    skill = as_skill(*resolved);
                  }
                }
                if (!skill || !skill->name) continue;
                if (!skill->impl && !skill->external) continue;
                llm::ToolDefinition def;
                def.name = std::string(skill->name->chars, skill->name->length);
                def.description = skill->description
                    ? std::string(skill->description->chars, skill->description->length)
                    : "";
                def.input_schema = build_skill_schema(skill);
                if (skill->external &&
                    skill->external->binding == vm::SkillBinding::ClaudeBuiltin)
                {
                  def.type = skill->external->claude_tool_type;
                }
                tool_defs.push_back(std::move(def));
              }
              return tool_defs;
            };

            auto claw_find_skill = [&](const std::string& name) -> ObjSkill* {
              if (!claw->skills) return nullptr;
              for (const auto& skill_value : claw->skills->items)
              {
                ObjSkill* skill = nullptr;
                if (skill_value.is_skill())
                {
                  skill = as_skill(skill_value);
                }
                else if (skill_value.is_string())
                {
                  const std::string skill_name = to_std_string(skill_value);
                  if (skill_name != name) continue;
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

            auto json_to_value = [](const nlohmann::json& j) -> Value {
              if (j.is_null()) return Value::Nil();
              if (j.is_boolean()) return Value::Bool(j.get<bool>());
              if (j.is_number()) return Value::Number(j.get<double>());
              if (j.is_string())
              {
                const auto& s = j.get_ref<const std::string&>();
                return Value::String(s.c_str(), s.size());
              }
              const auto s = j.dump();
              return Value::String(s.c_str(), s.size());
            };

            // Build LLM messages from assembled context
            std::vector<llm::Message> messages;
            if (!assembled.system_prompt.empty())
            {
              messages.push_back({"system", assembled.system_prompt});
            }
            for (const auto& [role, content] : assembled.messages)
            {
              messages.push_back({role, content});
            }

            std::string final_response;

            // Check if claw has skills for tool calling
            if (claw->skills && !claw->skills->items.empty())
            {
              const auto tool_defs = claw_collect_tools();
              if (!tool_defs.empty())
              {
                const int max_tool_steps = get_config_int("NEAM_MAX_TOOL_STEPS", 25, 1, 1000);
                for (int step = 0; step < max_tool_steps; ++step)
                {
                  auto chat_resp = provider->chat_with_tools(messages, tool_defs, "auto");
                  trace_logger_.log_llm_output(claw_name, chat_resp.text);

                  if (!chat_resp.has_tool_calls())
                  {
                    final_response = chat_resp.text;
                    break;
                  }

                  // Add assistant response with tool calls
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

                    ObjSkill* skill = claw_find_skill(tc.name);
                    if (!skill || (!skill->impl && !skill->external))
                    {
                      result_content = "Unknown tool: " + tc.name;
                      is_error = true;
                    }
                    else
                    {
                      std::vector<Value> skill_args;
                      skill_args.reserve(skill->param_names.size());
                      for (const auto& param_name : skill->param_names)
                      {
                        if (tc.input.contains(param_name))
                          skill_args.push_back(json_to_value(tc.input.at(param_name)));
                        else
                          skill_args.push_back(Value::Nil());
                      }
                      try
                      {
                        if (skill->external)
                        {
                          McpClient* mcp_ptr = nullptr;
                          if (skill->external->binding == SkillBinding::McpTool)
                          {
                            auto mcp_it = mcp_clients_.find(skill->external->mcp_server_name);
                            if (mcp_it != mcp_clients_.end())
                              mcp_ptr = mcp_it->second.get();
                          }
                          result_content = dispatch_external_skill(skill, tc.input, mcp_ptr);
                        }
                        else
                        {
                          Value result = call_function(skill->impl, skill_args, true, tc.name);
                          result_content = value_to_string(result);
                        }
                      }
                      catch (const std::exception& e)
                      {
                        result_content = std::string("Tool error: ") + e.what();
                        is_error = true;
                      }
                    }

                    ++claw_tool_call_count;
                    claw_tools_used.push_back(tc.name);

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

                  if (step == max_tool_steps - 1)
                  {
                    final_response = "Max tool call steps reached without completion.";
                  }
                }
              }
              else
              {
                // Skills exist but none valid — plain chat
                final_response = provider->chat(messages);
              }
            }
            else
            {
              // No skills — plain chat
              final_response = provider->chat(messages);
            }

            // Append assistant response to session
            sm.append_message(claw, "default", "assistant", final_response);
            claw->context->history.push_back({"assistant", final_response});

            // v0.6.9 D9: Behavioral monitoring
            {
              auto elapsed_ms = static_cast<double>(
                  std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - claw_exec_start)
                      .count());
              auto& monitor = security::BehavioralMonitor::instance();
              monitor.record_request(claw_name, claw_tool_call_count,
                                     elapsed_ms, claw_tools_used);
            }

            trace_logger_.log_llm_output(claw_name, final_response);
            stack_.push_back(Value::String(final_response.c_str(), final_response.size()));
          }
          else if (method == "reset")
          {
            SessionManager sm;
            sm.reset(claw, "default");
            if (claw->context)
            {
              claw->context->history.clear();
            }
            stack_.push_back(Value::Nil());
          }
          else if (method == "history")
          {
            // v0.8 Phase 3: Return session history as list of maps
            SessionManager sm;
            auto& session = sm.get_or_create(claw, "default");
            auto* list = new_list(std::vector<Value>{});
            for (const auto& [role, content] : session.history)
            {
              auto* map = new_map({});
              map->entries["role"] = Value::String(role.c_str(), role.size());
              map->entries["content"] = Value::String(content.c_str(), content.size());
              list->items.push_back(Value::Map(map));
            }
            stack_.push_back(Value::List(list));
          }
          else
          {
            // Check impl_tables_ for trait methods on this claw agent
            std::string claw_name(claw->name->chars, claw->name->length);
            auto table_it = impl_tables_.find(claw_name);
            if (table_it != impl_tables_.end())
            {
              auto method_it = table_it->second->methods.find(method);
              if (method_it != table_it->second->methods.end())
              {
                auto* fn = method_it->second.function;
                std::size_t base_slot = stack_.size();
                stack_.push_back(receiver);  // self
                for (std::size_t i = 0; i < arg_count; ++i)
                {
                  stack_.push_back(args[i]);
                }
                frames_.push_back(CallFrame{&fn->chunk, fn, 0, base_slot, false, {}});
              }
              else
              {
                throw std::runtime_error(
                    "Method '" + method + "' not found on claw agent '" + claw_name + "'");
              }
            }
            else
            {
              throw std::runtime_error(
                  "Unknown method '" + method + "' on claw agent '" + claw_name + "'");
            }
          }
        }
        else if (is_obj_type(receiver, ObjType::OBJ_FORGE_AGENT))
        {
          auto* forge = as_forge_agent(receiver);
          if (method == "run")
          {
            // v0.8 Phase 4: Full iterative build-verify loop
            const std::string forge_name(forge->name->chars, forge->name->length);

            // Depth guard (same pattern as claw .ask())
            static thread_local int forge_depth = 0;
            struct ForgeDepthGuard {
              ForgeDepthGuard() { ++forge_depth; }
              ~ForgeDepthGuard() { --forge_depth; }
            } forge_depth_guard;

            {
              int max_depth =
                  security::RateLimiter::instance().input_limits().max_agent_depth;
              if (forge_depth > max_depth)
              {
                std::string err =
                    "Forge agent call depth exceeded (max " + std::to_string(max_depth) + ")";
                stack_.push_back(Value::String(err.c_str(), err.size()));
                break;
              }
            }

            // Kill switch
            if (security::BehavioralMonitor::instance().is_disabled(forge_name))
            {
              std::string err = "Agent '" + forge_name + "' is disabled";
              stack_.push_back(Value::String(err.c_str(), err.size()));
              break;
            }

            // Build ProviderConfig
            llm::ProviderConfig prov_config;
            prov_config.model = to_std_string(forge->model);
            prov_config.endpoint = to_std_string(forge->endpoint);
            prov_config.api_key = to_std_string(forge->api_key_env);
            prov_config.temperature = forge->temperature;
            prov_config.default_host =
                resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST",
                                   "http://localhost:11434");
            std::string provider_name = to_std_string(forge->provider);

            if (!prov_config.api_key.empty())
            {
              if (const char* env_val = std::getenv(prov_config.api_key.c_str()))
                prov_config.api_key = env_val;
              else
                prov_config.api_key.clear();
            }

            std::shared_ptr<llm::LLMProvider> provider;
            try
            {
              provider = llm::create_provider(provider_name, prov_config);
            }
            catch (const std::exception& e)
            {
              // If provider creation fails (e.g. no API key), return error outcome
              auto* result_map = new_map({});
              result_map->entries["outcome"] = Value::String("aborted", 7);
              result_map->entries["iterations"] = Value::Number(0);
              result_map->entries["total_cost"] = Value::Number(0.0);
              std::string msg = std::string("Provider error: ") + e.what();
              result_map->entries["message"] = Value::String(msg.c_str(), msg.size());
              stack_.push_back(Value::Map(result_map));
              break;
            }

            // Load plan tasks if plan_file is set
            std::vector<std::string> plan_tasks;
            std::vector<std::string> completed_tasks;
            std::string workspace = forge->workspace;
            const auto& loop_cfg = forge->loop_config;

            if (!loop_cfg.plan_file.empty())
            {
              std::string plan_path = loop_cfg.plan_file;
              if (!workspace.empty() && plan_path[0] != '/')
                plan_path = workspace + "/" + plan_path;
              plan_tasks = load_plan_tasks(plan_path);
            }

            if (!loop_cfg.progress_file.empty())
            {
              std::string progress_path = loop_cfg.progress_file;
              if (!workspace.empty() && progress_path[0] != '/')
                progress_path = workspace + "/" + progress_path;
              completed_tasks = load_completed_tasks(progress_path);
            }

            // Load prompt file content if set
            std::string prompt_content;
            if (!loop_cfg.prompt_file.empty())
            {
              std::string prompt_path = loop_cfg.prompt_file;
              if (!workspace.empty() && prompt_path[0] != '/')
                prompt_path = workspace + "/" + prompt_path;
              std::ifstream pf(prompt_path);
              if (pf)
              {
                std::ostringstream ss;
                ss << pf.rdbuf();
                prompt_content = ss.str();
              }
            }

            // Ensure context
            if (!forge->context)
              forge->context = new_context();

            // Collect tool definitions (same lambda pattern as claw)
            auto forge_collect_tools = [&]() -> std::vector<llm::ToolDefinition> {
              std::vector<llm::ToolDefinition> tool_defs;
              if (!forge->skills || forge->skills->items.empty())
                return tool_defs;
              for (const auto& skill_value : forge->skills->items)
              {
                ObjSkill* skill = nullptr;
                if (skill_value.is_skill())
                {
                  skill = as_skill(skill_value);
                }
                else if (skill_value.is_string())
                {
                  const std::string skill_name = to_std_string(skill_value);
                  const Value* resolved = find_global_value(globals_, skill_name);
                  if (resolved && resolved->is_skill())
                    skill = as_skill(*resolved);
                }
                if (!skill || !skill->name) continue;
                if (!skill->impl && !skill->external) continue;
                llm::ToolDefinition def;
                def.name = std::string(skill->name->chars, skill->name->length);
                def.description = skill->description
                    ? std::string(skill->description->chars, skill->description->length)
                    : "";
                def.input_schema = build_skill_schema(skill);
                if (skill->external &&
                    skill->external->binding == vm::SkillBinding::ClaudeBuiltin)
                {
                  def.type = skill->external->claude_tool_type;
                }
                tool_defs.push_back(std::move(def));
              }
              return tool_defs;
            };

            auto forge_find_skill = [&](const std::string& name) -> ObjSkill* {
              if (!forge->skills) return nullptr;
              for (const auto& skill_value : forge->skills->items)
              {
                ObjSkill* skill = nullptr;
                if (skill_value.is_skill())
                  skill = as_skill(skill_value);
                else if (skill_value.is_string())
                {
                  const std::string skill_name = to_std_string(skill_value);
                  if (skill_name != name) continue;
                  const Value* resolved = find_global_value(globals_, skill_name);
                  if (resolved && resolved->is_skill())
                    skill = as_skill(*resolved);
                }
                if (skill && skill->name &&
                    std::string(skill->name->chars, skill->name->length) == name)
                  return skill;
              }
              return nullptr;
            };

            auto json_to_value = [](const nlohmann::json& j) -> Value {
              if (j.is_null()) return Value::Nil();
              if (j.is_boolean()) return Value::Bool(j.get<bool>());
              if (j.is_number()) return Value::Number(j.get<double>());
              if (j.is_string())
              {
                const auto& s = j.get_ref<const std::string&>();
                return Value::String(s.c_str(), s.size());
              }
              const auto s = j.dump();
              return Value::String(s.c_str(), s.size());
            };

            LoopOutcome outcome;
            outcome.kind = LoopOutcomeKind::MaxIterations;
            const int max_iters = loop_cfg.max_iterations;
            const double max_cost = loop_cfg.max_cost;
            double total_cost = 0.0;
            std::string feedback;

            // Task index for plan-based iteration
            std::size_t task_idx = 0;
            // Skip already-completed tasks
            for (std::size_t i = 0; i < plan_tasks.size(); ++i)
            {
              bool done = false;
              for (const auto& ct : completed_tasks)
              {
                if (ct == plan_tasks[i]) { done = true; break; }
              }
              if (!done) { task_idx = i; break; }
              if (i + 1 == plan_tasks.size()) task_idx = plan_tasks.size();
            }

            const int max_tool_steps = get_config_int("NEAM_MAX_TOOL_STEPS", 25, 1, 1000);

            for (int iter = 1; iter <= max_iters; ++iter)
            {
              // Budget check
              if (max_cost > 0.0 && total_cost >= max_cost)
              {
                outcome.kind = LoopOutcomeKind::BudgetExhausted;
                outcome.iterations = iter - 1;
                outcome.total_cost = total_cost;
                outcome.message = "Budget exhausted at $" + std::to_string(total_cost);
                break;
              }

              // Allocate ObjLoopContext
              auto* loop_ctx = new_loop_context();
              loop_ctx->forge_agent = forge;
              loop_ctx->iteration = iter;
              loop_ctx->total_cost = total_cost;
              loop_ctx->feedback = feedback;

              // Select current task
              std::string current_task;
              if (!plan_tasks.empty() && task_idx < plan_tasks.size())
              {
                current_task = plan_tasks[task_idx];
              }
              loop_ctx->current_task = current_task;

              // Build LLM messages
              std::vector<llm::Message> messages;
              std::string system_str = to_std_string(forge->system);
              if (!system_str.empty())
                messages.push_back({"system", system_str});

              if (!prompt_content.empty())
                messages.push_back({"user", prompt_content});

              // Build iteration prompt
              std::string iter_prompt = "Iteration " + std::to_string(iter) +
                  " of " + std::to_string(max_iters) + ".";
              if (!current_task.empty())
                iter_prompt += " Current task: " + current_task;
              if (!feedback.empty())
                iter_prompt += " Feedback from previous iteration: " + feedback;

              messages.push_back({"user", iter_prompt});

              // Tool-calling loop (same pattern as claw .ask())
              std::string llm_response;
              const auto tool_defs = forge_collect_tools();

              if (!tool_defs.empty())
              {
                for (int step = 0; step < max_tool_steps; ++step)
                {
                  auto chat_resp = provider->chat_with_tools(messages, tool_defs, "auto");

                  if (!chat_resp.has_tool_calls())
                  {
                    llm_response = chat_resp.text;
                    break;
                  }

                  // Add assistant response with tool calls
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

                    ObjSkill* skill = forge_find_skill(tc.name);
                    if (!skill || (!skill->impl && !skill->external))
                    {
                      result_content = "Unknown tool: " + tc.name;
                      is_error = true;
                    }
                    else
                    {
                      std::vector<Value> skill_args;
                      skill_args.reserve(skill->param_names.size());
                      for (const auto& param_name : skill->param_names)
                      {
                        if (tc.input.contains(param_name))
                          skill_args.push_back(json_to_value(tc.input.at(param_name)));
                        else
                          skill_args.push_back(Value::Nil());
                      }
                      try
                      {
                        if (skill->external)
                        {
                          McpClient* mcp_ptr = nullptr;
                          if (skill->external->binding == SkillBinding::McpTool)
                          {
                            auto mcp_it = mcp_clients_.find(skill->external->mcp_server_name);
                            if (mcp_it != mcp_clients_.end())
                              mcp_ptr = mcp_it->second.get();
                          }
                          result_content = dispatch_external_skill(skill, tc.input, mcp_ptr);
                        }
                        else
                        {
                          Value result = call_function(skill->impl, skill_args, true, tc.name);
                          result_content = value_to_string(result);
                        }
                      }
                      catch (const std::exception& e)
                      {
                        result_content = std::string("Tool error: ") + e.what();
                        is_error = true;
                      }
                    }

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

                  if (step == max_tool_steps - 1)
                    llm_response = "Max tool call steps reached.";
                }
              }
              else
              {
                // No tools — plain chat
                llm_response = provider->chat(messages);
              }

              // Budget: estimate cost from response length (chars/4 token heuristic)
              double iter_cost = static_cast<double>(llm_response.size()) / 4.0 * 0.00001;
              total_cost += iter_cost;

              // Call verify function if set
              bool verified = true;
              std::string verify_feedback;
              bool should_abort = false;

              if (forge->verify_fn)
              {
                // Build ctx value for verify callback
                loop_ctx->total_cost = total_cost;
                std::vector<Value> verify_args;
                verify_args.push_back(Value::LoopContext(loop_ctx));

                try
                {
                  Value verify_result = call_function(forge->verify_fn, verify_args, false, "verify");

                  if (verify_result.is_bool())
                  {
                    verified = verify_result.as_bool();
                    if (!verified)
                      verify_feedback = "Verification failed";
                  }
                  else if (verify_result.is_nil())
                  {
                    verified = false;
                    verify_feedback = "Verification returned nil";
                  }
                  else if (verify_result.is_string())
                  {
                    std::string vs = to_std_string(verify_result);
                    std::string vs_lower = vs;
                    std::transform(vs_lower.begin(), vs_lower.end(), vs_lower.begin(),
                        [](unsigned char c) { return std::tolower(c); });
                    if (vs_lower.find("abort") != std::string::npos)
                    {
                      should_abort = true;
                      verified = false;
                      verify_feedback = vs;
                    }
                    else if (vs_lower.find("retry") != std::string::npos ||
                             vs_lower.find("fail") != std::string::npos)
                    {
                      verified = false;
                      verify_feedback = vs;
                    }
                    else
                    {
                      // Truthy non-special string — pass
                      verified = true;
                    }
                  }
                  else
                  {
                    // Other truthy value — pass
                    verified = true;
                  }
                }
                catch (const std::exception& e)
                {
                  verified = false;
                  verify_feedback = std::string("Verify error: ") + e.what();
                }
              }

              if (should_abort)
              {
                outcome.kind = LoopOutcomeKind::Aborted;
                outcome.iterations = iter;
                outcome.total_cost = total_cost;
                outcome.message = verify_feedback;

                // Record learning
                if (!loop_cfg.learnings_file.empty() && !workspace.empty())
                {
                  std::string lf = loop_cfg.learnings_file;
                  if (lf[0] != '/') lf = workspace + "/" + lf;
                  append_learning(lf, iter, current_task, "aborted", verify_feedback);
                }
                break;
              }

              if (verified)
              {
                // Checkpoint
                if (!forge->checkpoint.empty() && !workspace.empty())
                  forge_checkpoint(forge->checkpoint, workspace, iter, current_task);

                // Mark task done
                if (!current_task.empty() && !loop_cfg.progress_file.empty() && !workspace.empty())
                {
                  std::string pf = loop_cfg.progress_file;
                  if (pf[0] != '/') pf = workspace + "/" + pf;
                  mark_task_done(pf, iter, current_task);
                }

                // Record learning
                if (!loop_cfg.learnings_file.empty() && !workspace.empty())
                {
                  std::string lf = loop_cfg.learnings_file;
                  if (lf[0] != '/') lf = workspace + "/" + lf;
                  append_learning(lf, iter, current_task, "completed", llm_response.substr(0, 200));
                }

                feedback.clear();

                // If no plan_file, single task → completed
                if (plan_tasks.empty())
                {
                  outcome.kind = LoopOutcomeKind::Completed;
                  outcome.iterations = iter;
                  outcome.total_cost = total_cost;
                  outcome.message = "Completed in " + std::to_string(iter) + " iteration(s)";
                  break;
                }

                // Advance to next task
                ++task_idx;
                if (task_idx >= plan_tasks.size())
                {
                  outcome.kind = LoopOutcomeKind::Completed;
                  outcome.iterations = iter;
                  outcome.total_cost = total_cost;
                  outcome.message = "All " + std::to_string(plan_tasks.size()) + " tasks completed";
                  break;
                }
              }
              else
              {
                // Retry with feedback
                feedback = verify_feedback.empty() ? "Verification failed" : verify_feedback;

                // Record learning
                if (!loop_cfg.learnings_file.empty() && !workspace.empty())
                {
                  std::string lf = loop_cfg.learnings_file;
                  if (lf[0] != '/') lf = workspace + "/" + lf;
                  append_learning(lf, iter, current_task, "retry", feedback);
                }
              }

              // If we've used all iterations
              if (iter == max_iters)
              {
                outcome.kind = LoopOutcomeKind::MaxIterations;
                outcome.iterations = iter;
                outcome.total_cost = total_cost;
                outcome.message = "Reached max iterations (" + std::to_string(max_iters) + ")";
              }
            }

            // Return outcome as Neam map
            auto* result_map = new_map({});
            const char* ok_str = outcome_kind_str(outcome.kind);
            result_map->entries["outcome"] = Value::String(ok_str, std::strlen(ok_str));
            result_map->entries["iterations"] = Value::Number(static_cast<double>(outcome.iterations));
            result_map->entries["total_cost"] = Value::Number(outcome.total_cost);
            result_map->entries["message"] = Value::String(outcome.message.c_str(), outcome.message.size());
            stack_.push_back(Value::Map(result_map));
          }
          else if (method == "ask")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("ForgeAgent.ask expects 1 argument");
            }
            std::string query = to_std_string(args[0]);
            if (!forge->context)
            {
              forge->context = new_context();
            }
            forge->context->history.push_back({"user", query});

            // Build ProviderConfig
            llm::ProviderConfig config;
            config.model = to_std_string(forge->model);
            config.endpoint = to_std_string(forge->endpoint);
            config.api_key = to_std_string(forge->api_key_env);
            config.temperature = forge->temperature;
            config.default_host =
                resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST",
                                   "http://localhost:11434");
            std::string provider_name = to_std_string(forge->provider);

            // Resolve api_key env var
            if (!config.api_key.empty())
            {
              if (const char* env_val = std::getenv(config.api_key.c_str()))
                config.api_key = env_val;
              else
                config.api_key.clear();
            }

            auto provider = llm::create_provider(provider_name, config);

            // Build messages
            std::vector<llm::Message> messages;
            std::string system_str = to_std_string(forge->system);
            if (!system_str.empty())
              messages.push_back({"system", system_str});
            for (const auto& msg : forge->context->history)
            {
              messages.push_back({msg.role, msg.content});
            }

            std::string reply = provider->chat(messages);
            forge->context->history.push_back({"assistant", reply});
            stack_.push_back(Value::String(reply.c_str(), reply.size()));
          }
          else if (method == "reset")
          {
            if (forge->context)
            {
              forge->context->history.clear();
            }
            stack_.push_back(Value::Nil());
          }
          else
          {
            // Check impl_tables_ for trait methods on this forge agent
            std::string forge_name(forge->name->chars, forge->name->length);
            auto table_it = impl_tables_.find(forge_name);
            if (table_it != impl_tables_.end())
            {
              auto method_it = table_it->second->methods.find(method);
              if (method_it != table_it->second->methods.end())
              {
                auto* fn = method_it->second.function;
                std::size_t base_slot = stack_.size();
                stack_.push_back(receiver);  // self
                for (std::size_t i = 0; i < arg_count; ++i)
                {
                  stack_.push_back(args[i]);
                }
                frames_.push_back(CallFrame{&fn->chunk, fn, 0, base_slot, false, {}});
              }
              else
              {
                throw std::runtime_error(
                    "Method '" + method + "' not found on forge agent '" + forge_name + "'");
              }
            }
            else
            {
              throw std::runtime_error(
                  "Unknown method '" + method + "' on forge agent '" + forge_name + "'");
            }
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
          else if (method == "contains")
          {
            if (arg_count != 1)
              throw std::runtime_error("list.contains expects 1 argument");
            bool found = false;
            ValueEqual eq;
            for (const auto& item : list->items)
            {
              if (eq(item, args[0])) { found = true; break; }
            }
            stack_.push_back(Value::Bool(found));
          }
          else if (method == "is_empty")
          {
            stack_.push_back(Value::Bool(list->items.empty()));
          }
          else if (method == "first")
          {
            if (list->items.empty())
              stack_.push_back(Value::Option(new_option(false, Value::Nil())));
            else
              stack_.push_back(Value::Option(new_option(true, list->items.front())));
          }
          else if (method == "last")
          {
            if (list->items.empty())
              stack_.push_back(Value::Option(new_option(false, Value::Nil())));
            else
              stack_.push_back(Value::Option(new_option(true, list->items.back())));
          }
          else if (method == "take")
          {
            if (arg_count != 1) throw std::runtime_error("list.take expects 1 argument");
            auto n = static_cast<std::size_t>(std::max(0.0, args[0].as_number()));
            n = std::min(n, list->items.size());
            std::vector<Value> items(list->items.begin(), list->items.begin() + static_cast<std::ptrdiff_t>(n));
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "skip")
          {
            if (arg_count != 1) throw std::runtime_error("list.skip expects 1 argument");
            auto n = static_cast<std::size_t>(std::max(0.0, args[0].as_number()));
            n = std::min(n, list->items.size());
            std::vector<Value> items(list->items.begin() + static_cast<std::ptrdiff_t>(n), list->items.end());
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "any")
          {
            if (arg_count != 1) throw std::runtime_error("list.any expects 1 argument");
            bool result = false;
            for (const auto& item : list->items)
            {
              Value call_args[] = {item};
              if (is_truthy(call_native(args[0], 1, call_args))) { result = true; break; }
            }
            stack_.push_back(Value::Bool(result));
          }
          else if (method == "all")
          {
            if (arg_count != 1) throw std::runtime_error("list.all expects 1 argument");
            bool result = true;
            for (const auto& item : list->items)
            {
              Value call_args[] = {item};
              if (!is_truthy(call_native(args[0], 1, call_args))) { result = false; break; }
            }
            stack_.push_back(Value::Bool(result));
          }
          else if (method == "count")
          {
            if (arg_count == 0)
            {
              stack_.push_back(Value::Number(static_cast<double>(list->items.size())));
            }
            else if (arg_count == 1)
            {
              int cnt = 0;
              for (const auto& item : list->items)
              {
                Value call_args[] = {item};
                if (is_truthy(call_native(args[0], 1, call_args))) ++cnt;
              }
              stack_.push_back(Value::Number(static_cast<double>(cnt)));
            }
            else
            {
              throw std::runtime_error("list.count expects 0-1 arguments");
            }
          }
          else if (method == "flatten")
          {
            std::vector<Value> items;
            for (const auto& item : list->items)
            {
              if (item.is_list())
              {
                auto* sub = as_list(item);
                items.insert(items.end(), sub->items.begin(), sub->items.end());
              }
              else
              {
                items.push_back(item);
              }
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "flat_map")
          {
            if (arg_count != 1) throw std::runtime_error("list.flat_map expects 1 argument");
            std::vector<Value> items;
            for (const auto& item : list->items)
            {
              Value call_args[] = {item};
              Value result = call_native(args[0], 1, call_args);
              if (result.is_list())
              {
                auto* sub = as_list(result);
                items.insert(items.end(), sub->items.begin(), sub->items.end());
              }
              else
              {
                items.push_back(result);
              }
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "zip")
          {
            if (arg_count != 1) throw std::runtime_error("list.zip expects 1 argument");
            if (!args[0].is_list()) throw std::runtime_error("list.zip expects list argument");
            auto* other = as_list(args[0]);
            auto len = std::min(list->items.size(), other->items.size());
            std::vector<Value> items;
            items.reserve(len);
            for (std::size_t idx = 0; idx < len; ++idx)
            {
              std::vector<Value> pair = {list->items[idx], other->items[idx]};
              items.push_back(Value::Tuple(new_tuple(std::move(pair))));
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "enumerate")
          {
            std::vector<Value> items;
            items.reserve(list->items.size());
            for (std::size_t idx = 0; idx < list->items.size(); ++idx)
            {
              std::vector<Value> pair = {Value::Number(static_cast<double>(idx)), list->items[idx]};
              items.push_back(Value::Tuple(new_tuple(std::move(pair))));
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "unique")
          {
            std::unordered_set<Value, ValueHash, ValueEqual> seen;
            std::vector<Value> items;
            for (const auto& item : list->items)
            {
              if (seen.insert(item).second)
              {
                items.push_back(item);
              }
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "chunk")
          {
            if (arg_count != 1) throw std::runtime_error("list.chunk expects 1 argument");
            auto n = static_cast<std::size_t>(std::max(1.0, args[0].as_number()));
            std::vector<Value> chunks;
            for (std::size_t idx = 0; idx < list->items.size(); idx += n)
            {
              auto end = std::min(idx + n, list->items.size());
              std::vector<Value> chunk(list->items.begin() + static_cast<std::ptrdiff_t>(idx),
                                       list->items.begin() + static_cast<std::ptrdiff_t>(end));
              chunks.push_back(Value::List(new_list(std::move(chunk))));
            }
            stack_.push_back(Value::List(new_list(std::move(chunks))));
          }
          else if (method == "sort_by")
          {
            if (arg_count != 1) throw std::runtime_error("list.sort_by expects 1 argument");
            std::vector<Value> sorted_items = list->items;
            auto fn = args[0];
            std::sort(sorted_items.begin(), sorted_items.end(),
                      [&](const Value& a, const Value& b)
                      {
                        Value ca[] = {a};
                        Value cb[] = {b};
                        Value va = call_native(fn, 1, ca);
                        Value vb = call_native(fn, 1, cb);
                        if (va.is_number() && vb.is_number())
                          return va.as_number() < vb.as_number();
                        return value_to_string(va) < value_to_string(vb);
                      });
            stack_.push_back(Value::List(new_list(std::move(sorted_items))));
          }
          else if (method == "min")
          {
            if (list->items.empty())
            {
              stack_.push_back(Value::Option(new_option(false, Value::Nil())));
            }
            else
            {
              Value best = list->items[0];
              for (std::size_t idx = 1; idx < list->items.size(); ++idx)
              {
                if (list->items[idx].is_number() && best.is_number() &&
                    list->items[idx].as_number() < best.as_number())
                  best = list->items[idx];
              }
              stack_.push_back(best);
            }
          }
          else if (method == "max")
          {
            if (list->items.empty())
            {
              stack_.push_back(Value::Option(new_option(false, Value::Nil())));
            }
            else
            {
              Value best = list->items[0];
              for (std::size_t idx = 1; idx < list->items.size(); ++idx)
              {
                if (list->items[idx].is_number() && best.is_number() &&
                    list->items[idx].as_number() > best.as_number())
                  best = list->items[idx];
              }
              stack_.push_back(best);
            }
          }
          else if (method == "sum")
          {
            double total = 0;
            for (const auto& item : list->items)
            {
              if (item.is_number()) total += item.as_number();
            }
            stack_.push_back(Value::Number(total));
          }
          else if (method == "mean")
          {
            if (list->items.empty())
            {
              stack_.push_back(Value::Number(0));
            }
            else
            {
              double total = 0;
              int cnt = 0;
              for (const auto& item : list->items)
              {
                if (item.is_number()) { total += item.as_number(); ++cnt; }
              }
              stack_.push_back(Value::Number(cnt > 0 ? total / cnt : 0));
            }
          }
          else if (method == "join")
          {
            if (arg_count != 1) throw std::runtime_error("list.join expects 1 argument");
            std::string sep = to_std_string(args[0]);
            std::string result;
            for (std::size_t idx = 0; idx < list->items.size(); ++idx)
            {
              if (idx > 0) result += sep;
              result += value_to_string(list->items[idx]);
            }
            stack_.push_back(Value::String(result.c_str(), result.size()));
          }
          else if (method == "to_set")
          {
            std::unordered_set<Value, ValueHash, ValueEqual> items;
            for (const auto& item : list->items) items.insert(item);
            stack_.push_back(Value::Set(new_set(std::move(items))));
          }
          else if (method == "index_of")
          {
            if (arg_count != 1) throw std::runtime_error("list.index_of expects 1 argument");
            ValueEqual eq;
            int found_idx = -1;
            for (std::size_t idx = 0; idx < list->items.size(); ++idx)
            {
              if (eq(list->items[idx], args[0])) { found_idx = static_cast<int>(idx); break; }
            }
            stack_.push_back(Value::Number(static_cast<double>(found_idx)));
          }
          else if (method == "for_each")
          {
            if (arg_count != 1) throw std::runtime_error("list.for_each expects 1 argument");
            for (const auto& item : list->items)
            {
              Value call_args[] = {item};
              call_native(args[0], 1, call_args);
            }
            stack_.push_back(Value::Nil());
          }
          else if (method == "group_by")
          {
            if (arg_count != 1) throw std::runtime_error("list.group_by expects 1 argument");
            std::unordered_map<std::string, std::vector<Value>> groups;
            std::vector<std::string> order;
            for (const auto& item : list->items)
            {
              Value call_args[] = {item};
              Value key = call_native(args[0], 1, call_args);
              std::string key_str = value_to_string(key);
              if (groups.find(key_str) == groups.end()) order.push_back(key_str);
              groups[key_str].push_back(item);
            }
            std::unordered_map<std::string, Value> result_map;
            for (const auto& k : order)
            {
              auto items_copy = groups[k];
              result_map[k] = Value::List(new_list(std::move(items_copy)));
            }
            stack_.push_back(Value::Map(new_map(std::move(result_map))));
          }
          else if (method == "slice")
          {
            if (arg_count < 1 || arg_count > 2) throw std::runtime_error("list.slice expects 1-2 arguments");
            auto sz = static_cast<int64_t>(list->items.size());
            auto start = static_cast<int64_t>(args[0].as_number());
            if (start < 0) start = std::max<int64_t>(0, sz + start);
            auto end_idx = (arg_count == 2) ? static_cast<int64_t>(args[1].as_number()) : sz;
            if (end_idx < 0) end_idx = std::max<int64_t>(0, sz + end_idx);
            start = std::max<int64_t>(0, std::min(start, sz));
            end_idx = std::max<int64_t>(0, std::min(end_idx, sz));
            if (start >= end_idx)
              stack_.push_back(Value::List(new_list({})));
            else
            {
              std::vector<Value> items(list->items.begin() + start, list->items.begin() + end_idx);
              stack_.push_back(Value::List(new_list(std::move(items))));
            }
          }
          else
          {
            throw std::runtime_error("Unknown list method: " + method);
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
          else if (method == "len")
          {
            stack_.push_back(Value::Number(static_cast<double>(map->entries.size())));
          }
          else if (method == "is_empty")
          {
            stack_.push_back(Value::Bool(map->entries.empty()));
          }
          else if (method == "get_or")
          {
            if (arg_count != 2) throw std::runtime_error("map.get_or expects 2 arguments");
            std::string key = to_std_string(args[0]);
            auto it = map->entries.find(key);
            stack_.push_back(it != map->entries.end() ? it->second : args[1]);
          }
          else if (method == "merge")
          {
            if (arg_count != 1) throw std::runtime_error("map.merge expects 1 argument");
            if (!args[0].is_map()) throw std::runtime_error("map.merge expects map argument");
            auto* other = as_map(args[0]);
            std::unordered_map<std::string, Value> merged = map->entries;
            for (const auto& entry : other->entries)
            {
              merged[entry.first] = entry.second;
            }
            stack_.push_back(Value::Map(new_map(std::move(merged))));
          }
          else if (method == "map_values")
          {
            if (arg_count != 1) throw std::runtime_error("map.map_values expects 1 argument");
            std::unordered_map<std::string, Value> result;
            for (const auto& entry : map->entries)
            {
              Value call_args[] = {entry.second};
              result[entry.first] = call_native(args[0], 1, call_args);
            }
            stack_.push_back(Value::Map(new_map(std::move(result))));
          }
          else if (method == "map_keys")
          {
            if (arg_count != 1) throw std::runtime_error("map.map_keys expects 1 argument");
            std::unordered_map<std::string, Value> result;
            for (const auto& entry : map->entries)
            {
              Value key_val = Value::String(entry.first.c_str(), entry.first.size());
              Value call_args[] = {key_val};
              Value new_key = call_native(args[0], 1, call_args);
              result[value_to_string(new_key)] = entry.second;
            }
            stack_.push_back(Value::Map(new_map(std::move(result))));
          }
          else if (method == "filter")
          {
            if (arg_count != 1) throw std::runtime_error("map.filter expects 1 argument");
            std::unordered_map<std::string, Value> result;
            for (const auto& entry : map->entries)
            {
              Value key_val = Value::String(entry.first.c_str(), entry.first.size());
              Value call_args[] = {key_val, entry.second};
              if (is_truthy(call_native(args[0], 2, call_args)))
              {
                result[entry.first] = entry.second;
              }
            }
            stack_.push_back(Value::Map(new_map(std::move(result))));
          }
          else if (method == "find")
          {
            if (arg_count != 1) throw std::runtime_error("map.find expects 1 argument");
            bool found = false;
            for (const auto& entry : map->entries)
            {
              Value key_val = Value::String(entry.first.c_str(), entry.first.size());
              Value call_args[] = {key_val, entry.second};
              if (is_truthy(call_native(args[0], 2, call_args)))
              {
                std::unordered_map<std::string, Value> pair;
                pair["key"] = key_val;
                pair["value"] = entry.second;
                stack_.push_back(Value::Map(new_map(std::move(pair))));
                found = true;
                break;
              }
            }
            if (!found) stack_.push_back(Value::Nil());
          }
          else if (method == "for_each")
          {
            if (arg_count != 1) throw std::runtime_error("map.for_each expects 1 argument");
            for (const auto& entry : map->entries)
            {
              Value key_val = Value::String(entry.first.c_str(), entry.first.size());
              Value call_args[] = {key_val, entry.second};
              call_native(args[0], 2, call_args);
            }
            stack_.push_back(Value::Nil());
          }
          else if (method == "count")
          {
            if (arg_count == 0)
            {
              stack_.push_back(Value::Number(static_cast<double>(map->entries.size())));
            }
            else if (arg_count == 1)
            {
              int cnt = 0;
              for (const auto& entry : map->entries)
              {
                Value key_val = Value::String(entry.first.c_str(), entry.first.size());
                Value call_args[] = {key_val, entry.second};
                if (is_truthy(call_native(args[0], 2, call_args))) ++cnt;
              }
              stack_.push_back(Value::Number(static_cast<double>(cnt)));
            }
            else
            {
              throw std::runtime_error("map.count expects 0-1 arguments");
            }
          }
          else if (method == "to_list")
          {
            std::vector<Value> items;
            items.reserve(map->entries.size());
            for (const auto& entry : map->entries)
            {
              std::vector<Value> pair = {
                Value::String(entry.first.c_str(), entry.first.size()),
                entry.second
              };
              items.push_back(Value::Tuple(new_tuple(std::move(pair))));
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else
          {
            throw std::runtime_error("Unknown map method: " + method);
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
          else if (method == "len")
          {
            stack_.push_back(Value::Number(static_cast<double>(base.size())));
          }
          else if (method == "is_empty")
          {
            stack_.push_back(Value::Bool(base.empty()));
          }
          else if (method == "chars")
          {
            std::vector<Value> items;
            items.reserve(base.size());
            for (char c : base)
            {
              items.push_back(Value::String(&c, 1));
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "repeat")
          {
            if (arg_count != 1) throw std::runtime_error("string.repeat expects 1 argument");
            auto n = static_cast<int>(args[0].as_number());
            std::string result;
            result.reserve(base.size() * std::max(0, n));
            for (int j = 0; j < n; ++j) result += base;
            stack_.push_back(Value::String(result.c_str(), result.size()));
          }
          else if (method == "pad_left")
          {
            if (arg_count < 1 || arg_count > 2) throw std::runtime_error("string.pad_left expects 1-2 arguments");
            auto width = static_cast<std::size_t>(args[0].as_number());
            char pad_char = (arg_count == 2) ? to_std_string(args[1])[0] : ' ';
            std::string result = base;
            while (result.size() < width) result = pad_char + result;
            stack_.push_back(Value::String(result.c_str(), result.size()));
          }
          else if (method == "pad_right")
          {
            if (arg_count < 1 || arg_count > 2) throw std::runtime_error("string.pad_right expects 1-2 arguments");
            auto width = static_cast<std::size_t>(args[0].as_number());
            char pad_char = (arg_count == 2) ? to_std_string(args[1])[0] : ' ';
            std::string result = base;
            while (result.size() < width) result += pad_char;
            stack_.push_back(Value::String(result.c_str(), result.size()));
          }
          else if (method == "match")
          {
            if (arg_count != 1) throw std::runtime_error("string.match expects 1 argument");
            std::string pattern = to_std_string(args[0]);
            try
            {
              std::regex re(pattern);
              std::smatch match_result;
              if (std::regex_search(base, match_result, re))
              {
                stack_.push_back(Value::String(match_result[0].str().c_str(), match_result[0].str().size()));
              }
              else
              {
                stack_.push_back(Value::Nil());
              }
            }
            catch (const std::regex_error& ex)
            {
              throw std::runtime_error(std::string("Invalid regex: ") + ex.what());
            }
          }
          else if (method == "match_all")
          {
            if (arg_count != 1) throw std::runtime_error("string.match_all expects 1 argument");
            std::string pattern = to_std_string(args[0]);
            try
            {
              std::regex re(pattern);
              std::vector<Value> matches;
              auto begin = std::sregex_iterator(base.begin(), base.end(), re);
              auto end_it = std::sregex_iterator();
              for (auto it = begin; it != end_it; ++it)
              {
                auto m = it->str();
                matches.push_back(Value::String(m.c_str(), m.size()));
              }
              stack_.push_back(Value::List(new_list(std::move(matches))));
            }
            catch (const std::regex_error& ex)
            {
              throw std::runtime_error(std::string("Invalid regex: ") + ex.what());
            }
          }
          else if (method == "is_numeric")
          {
            bool numeric = !base.empty();
            bool has_dot = false;
            for (std::size_t idx = 0; idx < base.size(); ++idx)
            {
              char c = base[idx];
              if (c == '-' && idx == 0) continue;
              if (c == '.' && !has_dot) { has_dot = true; continue; }
              if (!std::isdigit(static_cast<unsigned char>(c))) { numeric = false; break; }
            }
            stack_.push_back(Value::Bool(numeric));
          }
          else if (method == "to_number")
          {
            try
            {
              double num = std::stod(base);
              stack_.push_back(Value::Number(num));
            }
            catch (...)
            {
              stack_.push_back(Value::Nil());
            }
          }
          else if (method == "reverse")
          {
            std::string reversed(base.rbegin(), base.rend());
            stack_.push_back(Value::String(reversed.c_str(), reversed.size()));
          }
          else if (method == "count")
          {
            if (arg_count != 1) throw std::runtime_error("string.count expects 1 argument");
            std::string sub = to_std_string(args[0]);
            int cnt = 0;
            std::size_t pos = 0;
            if (!sub.empty())
            {
              while ((pos = base.find(sub, pos)) != std::string::npos)
              {
                ++cnt;
                pos += sub.size();
              }
            }
            stack_.push_back(Value::Number(static_cast<double>(cnt)));
          }
          else if (method == "index_of")
          {
            if (arg_count != 1) throw std::runtime_error("string.index_of expects 1 argument");
            std::string sub = to_std_string(args[0]);
            auto pos = base.find(sub);
            stack_.push_back(Value::Number(pos == std::string::npos ? -1.0 : static_cast<double>(pos)));
          }
          else if (method == "lines")
          {
            std::vector<Value> items;
            std::size_t start = 0;
            while (start <= base.size())
            {
              auto pos = base.find('\n', start);
              auto len = (pos == std::string::npos) ? base.size() - start : pos - start;
              std::string line = base.substr(start, len);
              items.push_back(Value::String(line.c_str(), line.size()));
              if (pos == std::string::npos) break;
              start = pos + 1;
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "words")
          {
            std::vector<Value> items;
            std::size_t start = 0;
            while (start < base.size())
            {
              while (start < base.size() && std::isspace(static_cast<unsigned char>(base[start]))) ++start;
              if (start >= base.size()) break;
              std::size_t end_pos = start;
              while (end_pos < base.size() && !std::isspace(static_cast<unsigned char>(base[end_pos]))) ++end_pos;
              std::string word = base.substr(start, end_pos - start);
              items.push_back(Value::String(word.c_str(), word.size()));
              start = end_pos;
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else
          {
            throw std::runtime_error("Unknown string method: " + method);
          }
        }
        // v0.7.0: Range methods
        else if (is_obj_type(receiver, ObjType::OBJ_RANGE))
        {
          auto* range = as_range(receiver);
          if (method == "len")
          {
            int64_t len = 0;
            if (range->step > 0 && range->start < range->end)
            {
              len = (range->end - range->start + range->step - 1) / range->step;
            }
            else if (range->step < 0 && range->start > range->end)
            {
              len = (range->start - range->end + (-range->step) - 1) / (-range->step);
            }
            stack_.push_back(Value::Number(static_cast<double>(len)));
          }
          else if (method == "contains")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("range.contains() expects 1 argument");
            }
            bool found = false;
            if (args[0].is_number())
            {
              auto val = static_cast<int64_t>(args[0].as_number());
              if (range->step > 0)
              {
                found = val >= range->start && val < range->end && ((val - range->start) % range->step) == 0;
              }
              else if (range->step < 0)
              {
                found = val <= range->start && val > range->end && ((range->start - val) % (-range->step)) == 0;
              }
            }
            stack_.push_back(Value::Bool(found));
          }
          else if (method == "to_list")
          {
            std::vector<Value> items;
            if (range->step > 0)
            {
              for (int64_t i = range->start; i < range->end; i += range->step)
              {
                items.push_back(Value::Number(static_cast<double>(i)));
              }
            }
            else if (range->step < 0)
            {
              for (int64_t i = range->start; i > range->end; i += range->step)
              {
                items.push_back(Value::Number(static_cast<double>(i)));
              }
            }
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "reverse")
          {
            int64_t len = 0;
            if (range->step > 0 && range->start < range->end)
            {
              len = (range->end - range->start + range->step - 1) / range->step;
            }
            else if (range->step < 0 && range->start > range->end)
            {
              len = (range->start - range->end + (-range->step) - 1) / (-range->step);
            }
            if (len == 0)
            {
              stack_.push_back(Value::Range(new_range(0, 0, 1)));
            }
            else
            {
              int64_t new_start = range->start + (len - 1) * range->step;
              int64_t new_end = range->start - range->step;
              stack_.push_back(Value::Range(new_range(new_start, new_end, -range->step)));
            }
          }
          else
          {
            throw std::runtime_error("Unknown range method '" + method + "'");
          }
        }
        // v0.7.0: Set methods
        else if (is_obj_type(receiver, ObjType::OBJ_SET))
        {
          auto* set = as_set(receiver);
          if (method == "add")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("set.add() expects 1 argument");
            }
            set->items.insert(args[0]);
            stack_.push_back(receiver);
          }
          else if (method == "remove")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("set.remove() expects 1 argument");
            }
            set->items.erase(args[0]);
            stack_.push_back(receiver);
          }
          else if (method == "contains")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("set.contains() expects 1 argument");
            }
            stack_.push_back(Value::Bool(set->items.count(args[0]) > 0));
          }
          else if (method == "len")
          {
            stack_.push_back(Value::Number(static_cast<double>(set->items.size())));
          }
          else if (method == "is_empty")
          {
            stack_.push_back(Value::Bool(set->items.empty()));
          }
          else if (method == "union")
          {
            if (arg_count != 1 || !args[0].is_set())
            {
              throw std::runtime_error("set.union() expects a set argument");
            }
            auto* other = as_set(args[0]);
            std::unordered_set<Value, ValueHash, ValueEqual> result(set->items);
            for (const auto& item : other->items)
            {
              result.insert(item);
            }
            stack_.push_back(Value::Set(new_set(std::move(result))));
          }
          else if (method == "intersection")
          {
            if (arg_count != 1 || !args[0].is_set())
            {
              throw std::runtime_error("set.intersection() expects a set argument");
            }
            auto* other = as_set(args[0]);
            std::unordered_set<Value, ValueHash, ValueEqual> result;
            for (const auto& item : set->items)
            {
              if (other->items.count(item) > 0)
              {
                result.insert(item);
              }
            }
            stack_.push_back(Value::Set(new_set(std::move(result))));
          }
          else if (method == "difference")
          {
            if (arg_count != 1 || !args[0].is_set())
            {
              throw std::runtime_error("set.difference() expects a set argument");
            }
            auto* other = as_set(args[0]);
            std::unordered_set<Value, ValueHash, ValueEqual> result;
            for (const auto& item : set->items)
            {
              if (other->items.count(item) == 0)
              {
                result.insert(item);
              }
            }
            stack_.push_back(Value::Set(new_set(std::move(result))));
          }
          else if (method == "symmetric_diff")
          {
            if (arg_count != 1 || !args[0].is_set())
            {
              throw std::runtime_error("set.symmetric_diff() expects a set argument");
            }
            auto* other = as_set(args[0]);
            std::unordered_set<Value, ValueHash, ValueEqual> result;
            for (const auto& item : set->items)
            {
              if (other->items.count(item) == 0) result.insert(item);
            }
            for (const auto& item : other->items)
            {
              if (set->items.count(item) == 0) result.insert(item);
            }
            stack_.push_back(Value::Set(new_set(std::move(result))));
          }
          else if (method == "is_subset")
          {
            if (arg_count != 1 || !args[0].is_set())
            {
              throw std::runtime_error("set.is_subset() expects a set argument");
            }
            auto* other = as_set(args[0]);
            bool subset = true;
            for (const auto& item : set->items)
            {
              if (other->items.count(item) == 0)
              {
                subset = false;
                break;
              }
            }
            stack_.push_back(Value::Bool(subset));
          }
          else if (method == "is_superset")
          {
            if (arg_count != 1 || !args[0].is_set())
            {
              throw std::runtime_error("set.is_superset() expects a set argument");
            }
            auto* other = as_set(args[0]);
            bool superset = true;
            for (const auto& item : other->items)
            {
              if (set->items.count(item) == 0)
              {
                superset = false;
                break;
              }
            }
            stack_.push_back(Value::Bool(superset));
          }
          else if (method == "to_list")
          {
            std::vector<Value> items(set->items.begin(), set->items.end());
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else if (method == "clear")
          {
            set->items.clear();
            stack_.push_back(receiver);
          }
          else if (method == "map")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("set.map() expects 1 argument");
            }
            std::unordered_set<Value, ValueHash, ValueEqual> result;
            for (const auto& item : set->items)
            {
              { Value fn_arg = item; result.insert(call_native(args[0], 1, &fn_arg)); }
            }
            stack_.push_back(Value::Set(new_set(std::move(result))));
          }
          else if (method == "filter")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("set.filter() expects 1 argument");
            }
            std::unordered_set<Value, ValueHash, ValueEqual> result;
            for (const auto& item : set->items)
            {
              Value fn_arg = item; Value pred = call_native(args[0], 1, &fn_arg);
              if (is_truthy(pred))
              {
                result.insert(item);
              }
            }
            stack_.push_back(Value::Set(new_set(std::move(result))));
          }
          else
          {
            throw std::runtime_error("Unknown set method '" + method + "'");
          }
        }
        // v0.7.0: Tuple methods
        else if (is_obj_type(receiver, ObjType::OBJ_TUPLE))
        {
          auto* tuple = as_tuple(receiver);
          if (method == "len")
          {
            stack_.push_back(Value::Number(static_cast<double>(tuple->items.size())));
          }
          else if (method == "contains")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("tuple.contains() expects 1 argument");
            }
            bool found = false;
            for (const auto& item : tuple->items)
            {
              if (values_equal(item, args[0]))
              {
                found = true;
                break;
              }
            }
            stack_.push_back(Value::Bool(found));
          }
          else if (method == "to_list")
          {
            stack_.push_back(Value::List(new_list(std::vector<Value>(tuple->items))));
          }
          else if (method == "index_of")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("tuple.index_of() expects 1 argument");
            }
            double idx = -1;
            for (std::size_t i = 0; i < tuple->items.size(); ++i)
            {
              if (values_equal(tuple->items[i], args[0]))
              {
                idx = static_cast<double>(i);
                break;
              }
            }
            stack_.push_back(Value::Number(idx));
          }
          else
          {
            throw std::runtime_error("Unknown tuple method '" + method + "'");
          }
        }
        // v0.7.0: Option methods
        else if (is_obj_type(receiver, ObjType::OBJ_OPTION))
        {
          auto* opt = as_option(receiver);
          if (method == "is_some")
          {
            stack_.push_back(Value::Bool(opt->has_value));
          }
          else if (method == "is_none")
          {
            stack_.push_back(Value::Bool(!opt->has_value));
          }
          else if (method == "unwrap")
          {
            if (!opt->has_value)
            {
              throw std::runtime_error("Called unwrap() on None");
            }
            stack_.push_back(opt->value);
          }
          else if (method == "unwrap_or")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("unwrap_or() expects 1 argument");
            }
            stack_.push_back(opt->has_value ? opt->value : args[0]);
          }
          else if (method == "map")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("option.map() expects 1 argument");
            }
            if (opt->has_value)
            {
              Value fn_arg = opt->value; Value mapped = call_native(args[0], 1, &fn_arg);
              stack_.push_back(Value::Option(new_option(true, mapped)));
            }
            else
            {
              stack_.push_back(Value::Option(new_option(false, Value::Nil())));
            }
          }
          else if (method == "and_then")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("option.and_then() expects 1 argument");
            }
            if (opt->has_value)
            {
              { Value fn_arg = opt->value; stack_.push_back(call_native(args[0], 1, &fn_arg)); }
            }
            else
            {
              stack_.push_back(Value::Option(new_option(false, Value::Nil())));
            }
          }
          else if (method == "or_else")
          {
            if (arg_count != 1)
            {
              throw std::runtime_error("option.or_else() expects 1 argument");
            }
            if (opt->has_value)
            {
              stack_.push_back(receiver);
            }
            else
            {
              stack_.push_back(call_native(args[0], 0, nullptr));
            }
          }
          else if (method == "to_list")
          {
            std::vector<Value> items;
            if (opt->has_value) items.push_back(opt->value);
            stack_.push_back(Value::List(new_list(std::move(items))));
          }
          else
          {
            throw std::runtime_error("Unknown option method '" + method + "'");
          }
        }
        // v0.7.1: Struct instance method dispatch
        else if (is_obj_type(receiver, ObjType::OBJ_STRUCT))
        {
          auto* obj = as_struct(receiver);
          auto table_it = impl_tables_.find(obj->def->name);
          if (table_it == impl_tables_.end())
          {
            throw std::runtime_error(
                "No impl block for struct '" + obj->def->name + "'");
          }
          auto method_it = table_it->second->methods.find(method);
          if (method_it == table_it->second->methods.end())
          {
            throw std::runtime_error(
                "Method '" + method + "' not found on struct '" +
                obj->def->name + "'");
          }
          auto& entry = method_it->second;
          if (entry.is_static)
          {
            throw std::runtime_error(
                "Cannot call static method '" + method +
                "' on instance. Use " + obj->def->name + "." + method + "() instead.");
          }
          auto* fn = entry.function;
          // Instance method: self is first arg. Push self + args onto stack.
          // fn->arity includes self.
          if (fn->arity != static_cast<int>(arg_count) + 1)
          {
            throw std::runtime_error(
                "Method '" + method + "' expects " +
                std::to_string(fn->arity - 1) +
                " argument(s), got " + std::to_string(arg_count));
          }
          // Push receiver (self) then args
          std::size_t base_slot = stack_.size();
          stack_.push_back(receiver);
          for (std::size_t i = 0; i < arg_count; ++i)
          {
            stack_.push_back(args[i]);
          }
          frames_.push_back(CallFrame{&fn->chunk, fn, 0, base_slot, false, {}});
        }
        // v0.7.1: Static method dispatch — Type.method()
        else if (is_obj_type(receiver, ObjType::OBJ_STRUCT_DEF))
        {
          auto* def = as_struct_def(receiver);
          auto table_it = impl_tables_.find(def->name);
          if (table_it == impl_tables_.end())
          {
            throw std::runtime_error(
                "No impl block for struct '" + def->name + "'");
          }
          auto method_it = table_it->second->methods.find(method);
          if (method_it == table_it->second->methods.end())
          {
            throw std::runtime_error(
                "Static method '" + method + "' not found on struct '" +
                def->name + "'");
          }
          auto& entry = method_it->second;
          if (!entry.is_static)
          {
            throw std::runtime_error(
                "Method '" + method + "' is not static on struct '" +
                def->name + "'");
          }
          auto* fn = entry.function;
          if (fn->arity != static_cast<int>(arg_count))
          {
            throw std::runtime_error(
                "Static method '" + method + "' expects " +
                std::to_string(fn->arity) +
                " argument(s), got " + std::to_string(arg_count));
          }
          std::size_t base_slot = stack_.size();
          for (std::size_t i = 0; i < arg_count; ++i)
          {
            stack_.push_back(args[i]);
          }
          frames_.push_back(CallFrame{&fn->chunk, fn, 0, base_slot, false, {}});
        }
        // v0.7.1 Phase 2: Sealed type — Shape.Circle(5) or Shape.Point
        else if (is_obj_type(receiver, ObjType::OBJ_SEALED_DEF))
        {
          auto* def = as_sealed_def(receiver);
          auto tag_it = def->variant_index.find(method);
          if (tag_it != def->variant_index.end())
          {
            // Construct variant with args
            auto tag = tag_it->second;
            const auto& variant_info = def->variants[tag];
            if (variant_info.fields.size() != arg_count)
            {
              throw std::runtime_error(
                  "Variant '" + method + "' expects " +
                  std::to_string(variant_info.fields.size()) +
                  " field(s), got " + std::to_string(arg_count));
            }
            auto* v = new_variant(def, tag, std::vector<Value>(args.begin(), args.begin() + static_cast<std::ptrdiff_t>(arg_count)));
            stack_.push_back(Value::Variant(v));
          }
          else
          {
            // Try impl table methods on sealed type
            auto table_it = impl_tables_.find(def->name);
            if (table_it != impl_tables_.end())
            {
              auto method_it = table_it->second->methods.find(method);
              if (method_it != table_it->second->methods.end())
              {
                auto* fn = method_it->second.function;
                std::size_t base_slot = stack_.size();
                for (std::size_t i = 0; i < arg_count; ++i)
                {
                  stack_.push_back(args[i]);
                }
                frames_.push_back(CallFrame{&fn->chunk, fn, 0, base_slot, false, {}});
              }
              else
              {
                throw std::runtime_error(
                    "No variant or method '" + method + "' on sealed type '" + def->name + "'");
              }
            }
            else
            {
              throw std::runtime_error(
                  "No variant '" + method + "' on sealed type '" + def->name + "'");
            }
          }
        }
        // v0.7.1 Phase 2: Variant method dispatch (via sealed type's impl table)
        else if (is_obj_type(receiver, ObjType::OBJ_VARIANT))
        {
          auto* v = as_variant(receiver);
          auto table_it = impl_tables_.find(v->sealed_def->name);
          if (table_it != impl_tables_.end())
          {
            auto method_it = table_it->second->methods.find(method);
            if (method_it != table_it->second->methods.end())
            {
              auto& entry = method_it->second;
              auto* fn = entry.function;
              std::size_t base_slot = stack_.size();
              stack_.push_back(receiver);  // self
              for (std::size_t i = 0; i < arg_count; ++i)
              {
                stack_.push_back(args[i]);
              }
              frames_.push_back(CallFrame{&fn->chunk, fn, 0, base_slot, false, {}});
            }
            else
            {
              throw std::runtime_error(
                  "Method '" + method + "' not found on sealed type '" + v->sealed_def->name + "'");
            }
          }
          else
          {
            throw std::runtime_error(
                "No impl block for sealed type '" + v->sealed_def->name + "'");
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
      // =====================================================================
      // v0.7.0: Data types & data processing opcodes
      // =====================================================================
      case OpCode::OP_GET_ITER:
      {
        Value collection = pop();
        auto* iter = new_iter_state();
        iter->source = collection;
        if (is_obj_type(collection, ObjType::OBJ_LIST))
        {
          auto* list = as_list(collection);
          iter->position = 0;
          iter->end = static_cast<int64_t>(list->items.size());
          iter->step = 1;
        }
        else if (is_obj_type(collection, ObjType::OBJ_RANGE))
        {
          auto* range = as_range(collection);
          iter->position = range->start;
          iter->end = range->end;
          iter->step = range->step;
        }
        else if (is_obj_type(collection, ObjType::OBJ_MAP))
        {
          auto* map = as_map(collection);
          for (const auto& entry : map->entries)
          {
            std::unordered_map<std::string, Value> pair_map;
            pair_map["key"] = Value::String(entry.first.c_str(), entry.first.size());
            pair_map["value"] = entry.second;
            iter->snapshot.push_back(Value::Map(new_map(std::move(pair_map))));
          }
          iter->position = 0;
          iter->end = static_cast<int64_t>(iter->snapshot.size());
          iter->step = 1;
        }
        else if (is_obj_type(collection, ObjType::OBJ_SET))
        {
          auto* set = as_set(collection);
          for (const auto& item : set->items)
          {
            iter->snapshot.push_back(item);
          }
          iter->position = 0;
          iter->end = static_cast<int64_t>(iter->snapshot.size());
          iter->step = 1;
        }
        else if (is_obj_type(collection, ObjType::OBJ_STRING))
        {
          auto* str = as_string(collection);
          for (std::size_t i = 0; i < str->length; ++i)
          {
            iter->snapshot.push_back(Value::String(str->chars + i, 1));
          }
          iter->position = 0;
          iter->end = static_cast<int64_t>(iter->snapshot.size());
          iter->step = 1;
        }
        else if (is_obj_type(collection, ObjType::OBJ_TUPLE))
        {
          auto* tuple = as_tuple(collection);
          iter->position = 0;
          iter->end = static_cast<int64_t>(tuple->items.size());
          iter->step = 1;
        }
        else
        {
          throw std::runtime_error(
              "Type error: cannot iterate over " + value_type_name(collection));
        }
        stack_.push_back(Value::IterState(iter));
        break;
      }
      case OpCode::OP_FOR_ITER:
      {
        const auto exit_offset = read_short(code, frame.ip);
        Value& iter_val = stack_.back();
        auto* iter = as_iter_state(iter_val);
        bool done = false;
        if (iter->step > 0)
        {
          done = iter->position >= iter->end;
        }
        else if (iter->step < 0)
        {
          done = iter->position <= iter->end;
        }
        else
        {
          done = true;
        }
        if (done)
        {
          frame.ip += exit_offset;
        }
        else
        {
          Value next_val;
          if (!iter->snapshot.empty())
          {
            next_val = iter->snapshot[static_cast<std::size_t>(iter->position)];
          }
          else if (is_obj_type(iter->source, ObjType::OBJ_RANGE))
          {
            next_val = Value::Number(static_cast<double>(iter->position));
          }
          else if (is_obj_type(iter->source, ObjType::OBJ_LIST))
          {
            auto* list = as_list(iter->source);
            next_val = list->items[static_cast<std::size_t>(iter->position)];
          }
          else if (is_obj_type(iter->source, ObjType::OBJ_TUPLE))
          {
            auto* tuple = as_tuple(iter->source);
            next_val = tuple->items[static_cast<std::size_t>(iter->position)];
          }
          iter->position += iter->step;
          stack_.push_back(next_val);
        }
        break;
      }
      case OpCode::OP_BUILD_SET:
      {
        const auto count = read_short(code, frame.ip);
        std::unordered_set<Value, ValueHash, ValueEqual> items;
        for (int i = count - 1; i >= 0; --i)
        {
          items.insert(stack_[stack_.size() - 1 - static_cast<std::size_t>(i)]);
        }
        for (uint16_t i = 0; i < count; ++i)
        {
          pop();
        }
        stack_.push_back(Value::Set(new_set(std::move(items))));
        break;
      }
      case OpCode::OP_BUILD_TUPLE:
      {
        const auto count = read_short(code, frame.ip);
        std::vector<Value> items;
        items.reserve(count);
        auto base = stack_.size() - count;
        for (uint16_t i = 0; i < count; ++i)
        {
          items.push_back(stack_[base + i]);
        }
        for (uint16_t i = 0; i < count; ++i)
        {
          pop();
        }
        stack_.push_back(Value::Tuple(new_tuple(std::move(items))));
        break;
      }
      case OpCode::OP_CONTAINS:
      {
        Value collection = pop();
        Value element = pop();
        bool found = false;
        if (is_obj_type(collection, ObjType::OBJ_LIST))
        {
          auto* list = as_list(collection);
          for (const auto& item : list->items)
          {
            if (values_equal(item, element))
            {
              found = true;
              break;
            }
          }
        }
        else if (is_obj_type(collection, ObjType::OBJ_SET))
        {
          auto* set = as_set(collection);
          found = set->items.count(element) > 0;
        }
        else if (is_obj_type(collection, ObjType::OBJ_MAP))
        {
          auto* map = as_map(collection);
          if (element.is_string())
          {
            auto key = to_std_string(element);
            found = map->entries.count(key) > 0;
          }
        }
        else if (is_obj_type(collection, ObjType::OBJ_STRING))
        {
          if (element.is_string())
          {
            auto haystack = to_std_string(collection);
            auto needle = to_std_string(element);
            found = haystack.find(needle) != std::string::npos;
          }
        }
        else if (is_obj_type(collection, ObjType::OBJ_RANGE))
        {
          if (element.is_number())
          {
            auto* range = as_range(collection);
            auto val = static_cast<int64_t>(element.as_number());
            if (range->step > 0)
            {
              found = val >= range->start && val < range->end && ((val - range->start) % range->step) == 0;
            }
            else if (range->step < 0)
            {
              found = val <= range->start && val > range->end && ((range->start - val) % (-range->step)) == 0;
            }
          }
        }
        else if (is_obj_type(collection, ObjType::OBJ_TUPLE))
        {
          auto* tuple = as_tuple(collection);
          for (const auto& item : tuple->items)
          {
            if (values_equal(item, element))
            {
              found = true;
              break;
            }
          }
        }
        else
        {
          throw std::runtime_error(
              "Type error: 'in' not supported for " + value_type_name(collection));
        }
        stack_.push_back(Value::Bool(found));
        break;
      }
      case OpCode::OP_FORMAT_STRING:
      {
        const auto part_count = read_short(code, frame.ip);
        std::string result;
        auto base = stack_.size() - part_count;
        for (uint16_t i = 0; i < part_count; ++i)
        {
          result += value_to_string(stack_[base + i]);
        }
        for (uint16_t i = 0; i < part_count; ++i)
        {
          pop();
        }
        stack_.push_back(Value::String(result.c_str(), result.size()));
        break;
      }
      case OpCode::OP_UNPACK:
      {
        const auto count = read_short(code, frame.ip);
        Value collection = pop();
        if (is_obj_type(collection, ObjType::OBJ_LIST))
        {
          auto* list = as_list(collection);
          if (list->items.size() != count)
          {
            throw std::runtime_error(
                "Unpack error: expected " + std::to_string(count) +
                " values, got " + std::to_string(list->items.size()));
          }
          for (uint16_t i = 0; i < count; ++i)
          {
            stack_.push_back(list->items[i]);
          }
        }
        else if (is_obj_type(collection, ObjType::OBJ_TUPLE))
        {
          auto* tuple = as_tuple(collection);
          if (tuple->items.size() != count)
          {
            throw std::runtime_error(
                "Unpack error: expected " + std::to_string(count) +
                " values, got " + std::to_string(tuple->items.size()));
          }
          for (uint16_t i = 0; i < count; ++i)
          {
            stack_.push_back(tuple->items[i]);
          }
        }
        else
        {
          throw std::runtime_error(
              "Unpack error: can only unpack lists and tuples");
        }
        break;
      }
      case OpCode::OP_UNPACK_REST:
      {
        const auto before = read_short(code, frame.ip);
        const auto after = read_short(code, frame.ip);
        Value collection = pop();
        std::vector<Value>* items = nullptr;
        if (is_obj_type(collection, ObjType::OBJ_LIST))
        {
          items = &as_list(collection)->items;
        }
        else if (is_obj_type(collection, ObjType::OBJ_TUPLE))
        {
          items = &as_tuple(collection)->items;
        }
        else
        {
          throw std::runtime_error(
              "Unpack error: can only unpack lists and tuples with rest");
        }
        auto total = static_cast<uint16_t>(items->size());
        if (total < before + after)
        {
          throw std::runtime_error(
              "Unpack error: not enough values (need at least " +
              std::to_string(before + after) + ", got " + std::to_string(total) + ")");
        }
        for (uint16_t i = 0; i < before; ++i)
        {
          stack_.push_back((*items)[i]);
        }
        std::vector<Value> rest_items(items->begin() + before,
                                       items->end() - after);
        stack_.push_back(Value::List(new_list(std::move(rest_items))));
        for (uint16_t i = 0; i < after; ++i)
        {
          stack_.push_back((*items)[total - after + i]);
        }
        break;
      }
      case OpCode::OP_SLICE:
      {
        const uint8_t flags = code[frame.ip++];
        bool has_step = (flags & 0x04) != 0;
        bool has_end = (flags & 0x02) != 0;
        bool has_start = (flags & 0x01) != 0;
        Value step_val = has_step ? pop() : Value::Number(1);
        Value end_val = has_end ? pop() : Value::Nil();
        Value start_val = has_start ? pop() : Value::Nil();
        Value base_val = pop();
        int64_t step = has_step ? static_cast<int64_t>(step_val.as_number()) : 1;
        if (step == 0)
        {
          throw std::runtime_error("Slice step cannot be zero");
        }
        if (is_obj_type(base_val, ObjType::OBJ_LIST))
        {
          auto* list = as_list(base_val);
          auto len = static_cast<int64_t>(list->items.size());
          int64_t start = has_start ? static_cast<int64_t>(start_val.as_number()) : (step > 0 ? 0 : len - 1);
          int64_t end = has_end ? static_cast<int64_t>(end_val.as_number()) : (step > 0 ? len : -len - 1);
          if (start < 0) start += len;
          if (end < 0) end += len;
          start = std::max(int64_t(0), std::min(start, len));
          end = std::max(int64_t(0), std::min(end, len));
          std::vector<Value> result;
          if (step > 0)
          {
            for (int64_t i = start; i < end; i += step)
            {
              result.push_back(list->items[static_cast<std::size_t>(i)]);
            }
          }
          else
          {
            for (int64_t i = start; i > end; i += step)
            {
              if (i >= 0 && i < len)
                result.push_back(list->items[static_cast<std::size_t>(i)]);
            }
          }
          stack_.push_back(Value::List(new_list(std::move(result))));
        }
        else if (is_obj_type(base_val, ObjType::OBJ_STRING))
        {
          auto* str = as_string(base_val);
          auto len = static_cast<int64_t>(str->length);
          int64_t start = has_start ? static_cast<int64_t>(start_val.as_number()) : (step > 0 ? 0 : len - 1);
          int64_t end = has_end ? static_cast<int64_t>(end_val.as_number()) : (step > 0 ? len : -len - 1);
          if (start < 0) start += len;
          if (end < 0) end += len;
          start = std::max(int64_t(0), std::min(start, len));
          end = std::max(int64_t(0), std::min(end, len));
          std::string result;
          if (step > 0)
          {
            for (int64_t i = start; i < end; i += step)
            {
              result += str->chars[static_cast<std::size_t>(i)];
            }
          }
          else
          {
            for (int64_t i = start; i > end; i += step)
            {
              if (i >= 0 && i < len)
                result += str->chars[static_cast<std::size_t>(i)];
            }
          }
          stack_.push_back(Value::String(result.c_str(), result.size()));
        }
        else if (is_obj_type(base_val, ObjType::OBJ_TUPLE))
        {
          auto* tuple = as_tuple(base_val);
          auto len = static_cast<int64_t>(tuple->items.size());
          int64_t start = has_start ? static_cast<int64_t>(start_val.as_number()) : (step > 0 ? 0 : len - 1);
          int64_t end = has_end ? static_cast<int64_t>(end_val.as_number()) : (step > 0 ? len : -len - 1);
          if (start < 0) start += len;
          if (end < 0) end += len;
          start = std::max(int64_t(0), std::min(start, len));
          end = std::max(int64_t(0), std::min(end, len));
          std::vector<Value> result;
          if (step > 0)
          {
            for (int64_t i = start; i < end; i += step)
            {
              result.push_back(tuple->items[static_cast<std::size_t>(i)]);
            }
          }
          else
          {
            for (int64_t i = start; i > end; i += step)
            {
              if (i >= 0 && i < len)
                result.push_back(tuple->items[static_cast<std::size_t>(i)]);
            }
          }
          stack_.push_back(Value::Tuple(new_tuple(std::move(result))));
        }
        else
        {
          throw std::runtime_error(
              "Slice error: cannot slice " + value_type_name(base_val));
        }
        break;
      }
      // ---- v0.7.1: OOP opcodes ----
      case OpCode::OP_DEFINE_STRUCT:
      {
        const auto name_index = read_short(code, frame.ip);
        const auto field_count = code[frame.ip++];
        const auto is_mutable = code[frame.ip++] != 0;
        const std::string name = to_std_string(constants[name_index]);
        std::vector<std::string> field_names;
        // Field names are on stack in order (pushed first = bottom)
        std::size_t base = stack_.size() - field_count;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          field_names.push_back(to_std_string(stack_[base + i]));
        }
        // Pop field names
        stack_.resize(base);
        auto* def = new_struct_def(name, field_names, is_mutable);
        struct_defs_[name] = def;
        // Register the struct def as a global so Point(...) calls work via OP_CALL
        auto* name_str = copy_string(name.c_str(), name.size());
        globals().set(name_str, Value::StructDef(def));
        break;
      }
      case OpCode::OP_IMPL_METHOD:
      {
        const auto type_name_index = read_short(code, frame.ip);
        const auto method_name_index = read_short(code, frame.ip);
        const auto is_static = code[frame.ip++] != 0;
        const std::string type_name = to_std_string(constants[type_name_index]);
        const std::string method_name = to_std_string(constants[method_name_index]);
        // Function is on top of stack
        Value fn_val = pop();
        auto* fn = as_function(fn_val);
        // Get or create impl table for this type
        auto& table = impl_tables_[type_name];
        if (!table)
        {
          table = new_impl_table();
          table->type_name = type_name;
        }
        MethodEntry entry;
        entry.function = fn;
        entry.is_static = is_static;
        table->methods[method_name] = entry;
        break;
      }
      case OpCode::OP_CONSTRUCT_NAMED:
      {
        const auto type_name_index = read_short(code, frame.ip);
        const auto field_count = code[frame.ip++];
        const std::string type_name = to_std_string(constants[type_name_index]);
        auto def_it = struct_defs_.find(type_name);
        if (def_it == struct_defs_.end())
        {
          throw std::runtime_error("Unknown struct type: " + type_name);
        }
        auto* def = def_it->second;
        // Stack has pairs: [name0, val0, name1, val1, ...]
        std::vector<Value> field_values(def->field_names.size(), Value::Nil());
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];
          int idx = def->field_index(fname);
          if (idx < 0)
          {
            throw std::runtime_error(
                "Unknown field '" + fname + "' in struct '" + type_name + "'");
          }
          field_values[static_cast<std::size_t>(idx)] = fval;
        }
        stack_.resize(base);
        auto* instance = new_struct(def, std::move(field_values));
        stack_.push_back(Value::Struct(instance));
        break;
      }
      case OpCode::OP_COPY_WITH:
      {
        const auto override_count = code[frame.ip++];
        // Stack: [struct, name0, val0, name1, val1, ...]
        std::size_t pair_base = stack_.size() - override_count * 2;
        std::size_t struct_idx = pair_base - 1;
        Value struct_val = stack_[struct_idx];
        if (!struct_val.is_struct())
        {
          throw std::runtime_error(
              "copy-with requires a struct, got " + value_type_name(struct_val));
        }
        auto* original = as_struct(struct_val);
        std::vector<std::pair<std::string, Value>> overrides;
        for (std::size_t i = 0; i < override_count; ++i)
        {
          std::string fname = to_std_string(stack_[pair_base + i * 2]);
          Value fval = stack_[pair_base + i * 2 + 1];
          overrides.push_back({std::move(fname), fval});
        }
        stack_.resize(struct_idx);
        auto* copy = struct_copy_with(original, overrides);
        stack_.push_back(Value::Struct(copy));
        break;
      }
      case OpCode::OP_SET_PROPERTY:
      {
        const auto name_index = read_short(code, frame.ip);
        const std::string field_name = to_std_string(constants[name_index]);
        // Stack: [object, value]
        Value value = pop();
        Value receiver = pop();
        if (!receiver.is_struct())
        {
          throw std::runtime_error(
              "Cannot set property '" + field_name + "' on " + value_type_name(receiver));
        }
        auto* obj = as_struct(receiver);

        // v0.7.1 Phase 5: Run guard handler if present
        auto guard_it = obj->def->guard_handlers.find(field_name);
        if (guard_it != obj->def->guard_handlers.end())
        {
          auto guard_result = call_function(guard_it->second,
              {receiver, value}, false, "");
          if (!is_truthy(guard_result))
          {
            throw std::runtime_error(
                "Guard failed for field '" + field_name + "' on " + obj->def->name);
          }
        }

        // v0.7.1 Phase 5: Run willSet handler if present
        auto ws_it = obj->def->will_set_handlers.find(field_name);
        if (ws_it != obj->def->will_set_handlers.end())
        {
          call_function(ws_it->second, {receiver, value}, false, "");
        }

        obj->set_field(field_name, value);

        // v0.7.1 Phase 5: Run didSet handler if present
        auto ds_it = obj->def->did_set_handlers.find(field_name);
        if (ds_it != obj->def->did_set_handlers.end())
        {
          call_function(ds_it->second, {receiver}, false, "");
        }

        stack_.push_back(value);  // Assignment expression yields the value
        break;
      }
      // ---- v0.7.1 Phase 2: trait + sealed + match opcodes ----
      case OpCode::OP_DEFINE_TRAIT:
      {
        const auto name_index = read_short(code, frame.ip);
        const auto required_count = code[frame.ip++];
        const auto default_count = code[frame.ip++];
        const std::string name = to_std_string(constants[name_index]);

        auto* def = new_trait_def(name);

        // Pop required method names (on top of stack)
        for (int i = required_count - 1; i >= 0; --i)
        {
          std::string method_name = to_std_string(pop());
          TraitMethodInfo info;
          info.name = std::move(method_name);
          info.default_impl = nullptr;
          def->methods.insert(def->methods.begin(), info);
        }

        // Pop default method functions (below required names on stack)
        for (int i = default_count - 1; i >= 0; --i)
        {
          Value fn_val = pop();
          auto* fn = as_function(fn_val);
          TraitMethodInfo info;
          info.name = std::string(fn->name->chars, fn->name->length);
          // Strip prefix "TraitName." from function name
          auto dot_pos = info.name.find('.');
          if (dot_pos != std::string::npos)
          {
            info.name = info.name.substr(dot_pos + 1);
          }
          info.default_impl = fn;
          def->methods.insert(def->methods.begin(), info);
        }

        trait_defs_[name] = def;
        auto* name_str = copy_string(name.c_str(), name.size());
        globals().set(name_str, Value::TraitDef(def));
        break;
      }
      case OpCode::OP_IMPL_TRAIT:
      {
        const auto trait_name_index = read_short(code, frame.ip);
        const auto type_name_index = read_short(code, frame.ip);
        const auto is_static = code[frame.ip++] != 0;
        const std::string trait_name = to_std_string(constants[trait_name_index]);
        const std::string type_name = to_std_string(constants[type_name_index]);

        Value fn_val = pop();
        auto* fn = as_function(fn_val);

        // Extract method name from function name (format: "TypeName.method_name")
        std::string fn_full_name(fn->name->chars, fn->name->length);
        std::string method_name = fn_full_name;
        auto dot_pos = fn_full_name.find('.');
        if (dot_pos != std::string::npos)
        {
          method_name = fn_full_name.substr(dot_pos + 1);
        }

        // Get or create impl table for this type
        auto& table = impl_tables_[type_name];
        if (!table)
        {
          table = new_impl_table();
          table->type_name = type_name;
        }

        MethodEntry entry;
        entry.function = fn;
        entry.is_static = is_static;
        table->methods[method_name] = entry;

        // Also register default methods from the trait that weren't explicitly provided
        if (!trait_name.empty())
        {
          auto trait_it = trait_defs_.find(trait_name);
          if (trait_it != trait_defs_.end())
          {
            auto* trait_def = trait_it->second;
            for (const auto& tm : trait_def->methods)
            {
              if (tm.default_impl && table->methods.find(tm.name) == table->methods.end())
              {
                MethodEntry default_entry;
                default_entry.function = tm.default_impl;
                default_entry.is_static = false;
                table->methods[tm.name] = default_entry;
              }
            }
          }
        }
        break;
      }
      case OpCode::OP_DEFINE_SEALED:
      {
        const auto name_index = read_short(code, frame.ip);
        const auto variant_count = code[frame.ip++];
        const std::string name = to_std_string(constants[name_index]);

        auto* def = new_sealed_def(name);

        // Pop variant definitions in reverse from stack.
        // Stack layout per variant: [variant_name, field1_name, ..., fieldN_name, field_count]
        // We need to read them in reverse order.
        std::vector<VariantInfo> variants(variant_count);
        for (int v = variant_count - 1; v >= 0; --v)
        {
          double fc = pop().as_number();
          auto field_count = static_cast<int>(fc);
          std::vector<VariantFieldInfo> fields(static_cast<std::size_t>(field_count));
          for (int f = field_count - 1; f >= 0; --f)
          {
            std::string field_name = to_std_string(pop());
            fields[static_cast<std::size_t>(f)] = VariantFieldInfo{
                std::move(field_name), static_cast<uint16_t>(f)};
          }
          std::string variant_name = to_std_string(pop());
          variants[static_cast<std::size_t>(v)] = VariantInfo{
              std::move(variant_name), std::move(fields), static_cast<uint16_t>(v)};
        }

        for (std::size_t i = 0; i < variants.size(); ++i)
        {
          def->variant_index[variants[i].name] = static_cast<uint16_t>(i);
        }
        def->variants = std::move(variants);

        sealed_defs_[name] = def;
        auto* name_str = copy_string(name.c_str(), name.size());
        globals().set(name_str, Value::SealedDef(def));
        break;
      }
      case OpCode::OP_CONSTRUCT_VARIANT:
      {
        const auto sealed_name_index = read_short(code, frame.ip);
        const auto variant_name_index = read_short(code, frame.ip);
        const auto arg_count = code[frame.ip++];
        const std::string sealed_name = to_std_string(constants[sealed_name_index]);
        const std::string variant_name = to_std_string(constants[variant_name_index]);

        auto def_it = sealed_defs_.find(sealed_name);
        if (def_it == sealed_defs_.end())
        {
          throw std::runtime_error("Unknown sealed type: " + sealed_name);
        }
        auto* def = def_it->second;
        auto tag_it = def->variant_index.find(variant_name);
        if (tag_it == def->variant_index.end())
        {
          throw std::runtime_error(
              "Unknown variant '" + variant_name + "' in sealed type '" + sealed_name + "'");
        }

        std::vector<Value> field_values;
        std::size_t base = stack_.size() - arg_count;
        for (std::size_t i = 0; i < arg_count; ++i)
        {
          field_values.push_back(stack_[base + i]);
        }
        stack_.resize(base);

        auto* v = new_variant(def, tag_it->second, std::move(field_values));
        stack_.push_back(Value::Variant(v));
        break;
      }
      case OpCode::OP_MATCH_VARIANT:
      {
        const auto variant_name_index = read_short(code, frame.ip);
        const auto bind_count = code[frame.ip++];
        // Skip offset not used here — compiler uses JUMP_IF_FALSE instead
        (void)bind_count;
        const std::string variant_name = to_std_string(constants[variant_name_index]);

        // Stack: [..., subject_dup, variant_name_const]
        pop();  // pop the variant name constant (already read from operand)

        Value subject = pop();  // pop the DUP'd subject

        if (subject.is_variant())
        {
          auto* v = as_variant(subject);
          auto tag_it = v->sealed_def->variant_index.find(variant_name);
          if (tag_it != v->sealed_def->variant_index.end() && v->tag == tag_it->second)
          {
            // Match! Push field values as bindings, then push true
            for (const auto& fv : v->field_values)
            {
              stack_.push_back(fv);
            }
            stack_.push_back(Value::Bool(true));
            break;
          }
        }

        // No match — push false
        stack_.push_back(Value::Bool(false));
        break;
      }
      case OpCode::OP_MATCH_START:
      case OpCode::OP_MATCH_WILDCARD:
      case OpCode::OP_MATCH_END:
        // These are placeholders — actual logic is compiled via JUMP/JUMP_IF_FALSE
        break;

      case OpCode::OP_SET_FIELD_OBSERVER:
      {
        const auto type_name_index = read_short(code, frame.ip);
        const auto field_name_index = read_short(code, frame.ip);
        const auto kind = code[frame.ip++];
        const std::string type_name = to_std_string(constants[type_name_index]);
        const std::string field_name = to_std_string(constants[field_name_index]);

        Value fn_val = pop();
        auto* fn = as_function(fn_val);

        // Find the struct def
        auto it = struct_defs_.find(type_name);
        if (it != struct_defs_.end())
        {
          auto* def = it->second;
          if (kind == 0)
            def->will_set_handlers[field_name] = fn;
          else if (kind == 1)
            def->did_set_handlers[field_name] = fn;
          else if (kind == 2)
            def->guard_handlers[field_name] = fn;
        }
        break;
      }
      // v0.8: Claw agent — create typed ObjClawAgent
      case OpCode::OP_DEFINE_CLAW_AGENT:
      {
        Value semantic_memory_value = pop();  // semantic_memory map or nil
        Value lanes_value = pop();            // lanes list
        Value channels_value = pop();         // channels list
        Value session_value = pop();          // session config map
        Value workspace_value = pop();        // workspace or nil
        Value env_value = pop();
        Value budget_value = pop();
        Value policy_value = pop();
        Value guardchains_value = pop();
        Value knowledge_value = pop();
        Value skills_value = pop();
        Value system_value = pop();
        Value temperature_value = pop();
        Value api_key_env_value = pop();
        Value endpoint_value = pop();
        Value model_value = pop();
        Value provider_value = pop();
        Value name_value = pop();

        // Create typed claw agent object
        auto* claw = new_claw_agent();
        claw->name = as_string(name_value);
        claw->provider = as_string(provider_value);
        claw->model = as_string(model_value);
        claw->endpoint = endpoint_value.is_nil() ? nullptr : as_string(endpoint_value);
        claw->api_key_env = api_key_env_value.is_nil() ? nullptr : as_string(api_key_env_value);
        claw->system = system_value.is_nil() ? nullptr : as_string(system_value);
        claw->temperature = temperature_value.is_nil() ? 0.0 : temperature_value.as_number();
        claw->skills = as_list(skills_value);
        claw->connected_knowledge = as_list(knowledge_value);
        claw->context = new_context();

        // Parse session config
        if (session_value.is_map())
        {
          auto* session_map = as_map(session_value);
          claw->idle_reset_minutes = static_cast<int>(map_number_value(session_map, "idle_reset_minutes"));
          if (claw->idle_reset_minutes == 0) claw->idle_reset_minutes = 60;
          claw->daily_reset_hour = static_cast<int>(map_number_value(session_map, "daily_reset_hour"));
          if (claw->daily_reset_hour == 0) claw->daily_reset_hour = 4;
          int mht = static_cast<int>(map_number_value(session_map, "max_history_turns"));
          claw->max_history_turns = (mht > 0) ? mht : 100;
          std::string comp = map_string_value(session_map, "compaction");
          if (!comp.empty()) claw->compaction = comp;
        }

        // Parse channels list
        if (channels_value.is_list())
        {
          for (const auto& ch : as_list(channels_value)->items)
          {
            if (ch.is_string())
              claw->channel_names.push_back(to_std_string(ch));
          }
        }

        // Parse lanes list
        if (lanes_value.is_list())
        {
          for (const auto& lane_val : as_list(lanes_value)->items)
          {
            if (lane_val.is_map())
            {
              auto* lane_map = as_map(lane_val);
              LaneConfig lc;
              lc.name = map_string_value(lane_map, "name");
              int conc = static_cast<int>(map_number_value(lane_map, "concurrency"));
              if (conc > 0) lc.concurrency = conc;
              std::string prio = map_string_value(lane_map, "priority");
              if (!prio.empty()) lc.priority = prio;
              claw->lanes.push_back(std::move(lc));
            }
          }
        }

        // Parse semantic memory config
        if (semantic_memory_value.is_map())
        {
          auto* mem_map = as_map(semantic_memory_value);
          claw->memory_backend = map_string_value(mem_map, "backend");
          claw->memory_search = map_string_value(mem_map, "search");
          claw->flush_on_compact = map_bool_value(mem_map, "flush_on_compact", false);
        }

        // Workspace
        if (workspace_value.is_string())
          claw->workspace = to_std_string(workspace_value);

        // Store in globals and registry
        std::string agent_name(claw->name->chars, claw->name->length);
        globals_.set(claw->name, Value::ClawAgent(claw));
        claw_agents_[agent_name] = claw;

        // Also register AgentExtension for backward compat (guards, policy, budget, env)
        AgentExtension extension;
        extension.agent_type = "claw";
        for (const auto& guard : as_list(guardchains_value)->items)
        {
          extension.guardchains.push_back(to_std_string(guard));
        }
        if (policy_value.is_string())
          extension.policy = to_std_string(policy_value);
        if (budget_value.is_string())
          extension.budget = to_std_string(budget_value);
        if (env_value.is_string())
          extension.env = to_std_string(env_value);
        agent_extensions_[agent_name] = std::move(extension);

        // v0.8 Phase 6: Initialize lane engine from claw agent lanes
        if (!claw->lanes.empty())
        {
          if (!lane_engine_)
            lane_engine_ = std::make_unique<LaneQueueEngine>();
          for (const auto& lane : claw->lanes)
          {
            lane_engine_->configure_lane(lane.name, lane.concurrency, 0);
          }
        }

        // v0.8 Phase 7: Initialize memory index if workspace + memory configured
        if (!claw->workspace.empty() &&
            claw->memory_backend != "none" && !claw->memory_backend.empty() &&
            claw->memory_search != "none" && !claw->memory_search.empty())
        {
          auto idx = std::make_unique<MemoryIndex>(
              claw->workspace, claw->memory_search);
          idx->load();
          idx->reindex_changed();
          idx->save();
          memory_indices_[agent_name] = std::move(idx);
        }

        break;
      }
      // v0.8: Forge agent — create typed ObjForgeAgent
      case OpCode::OP_DEFINE_FORGE_AGENT:
      {
        Value checkpoint_value = pop();  // checkpoint string or nil
        Value verify_value = pop();      // verify function
        Value loop_value = pop();        // loop config map
        Value workspace_value = pop();   // workspace or nil
        Value env_value = pop();
        Value budget_value = pop();
        Value policy_value = pop();
        Value guardchains_value = pop();
        Value skills_value = pop();
        Value system_value = pop();
        Value temperature_value = pop();
        Value api_key_env_value = pop();
        Value endpoint_value = pop();
        Value model_value = pop();
        Value provider_value = pop();
        Value name_value = pop();

        // Create typed forge agent object
        auto* forge = new_forge_agent();
        forge->name = as_string(name_value);
        forge->provider = as_string(provider_value);
        forge->model = as_string(model_value);
        forge->endpoint = endpoint_value.is_nil() ? nullptr : as_string(endpoint_value);
        forge->api_key_env = api_key_env_value.is_nil() ? nullptr : as_string(api_key_env_value);
        forge->system = system_value.is_nil() ? nullptr : as_string(system_value);
        forge->temperature = temperature_value.is_nil() ? 0.0 : temperature_value.as_number();
        forge->skills = as_list(skills_value);
        forge->context = new_context();

        // Parse loop config
        if (loop_value.is_map())
        {
          auto* loop_map = as_map(loop_value);
          int mi = static_cast<int>(map_number_value(loop_map, "max_iterations"));
          if (mi > 0) forge->loop_config.max_iterations = mi;
          double mc = map_number_value(loop_map, "max_cost");
          if (mc > 0.0) forge->loop_config.max_cost = mc;
          int mt = static_cast<int>(map_number_value(loop_map, "max_tokens"));
          if (mt > 0) forge->loop_config.max_tokens = mt;
          forge->loop_config.prompt_file = map_string_value(loop_map, "prompt_file");
          forge->loop_config.plan_file = map_string_value(loop_map, "plan_file");
          forge->loop_config.progress_file = map_string_value(loop_map, "progress_file");
          forge->loop_config.learnings_file = map_string_value(loop_map, "learnings_file");
        }

        // Verify function
        if (verify_value.is_function())
          forge->verify_fn = as_function(verify_value);

        // Checkpoint
        if (checkpoint_value.is_string())
          forge->checkpoint = to_std_string(checkpoint_value);

        // Workspace
        if (workspace_value.is_string())
          forge->workspace = to_std_string(workspace_value);

        // Store in globals and registry
        std::string agent_name(forge->name->chars, forge->name->length);
        globals_.set(forge->name, Value::ForgeAgent(forge));
        forge_agents_[agent_name] = forge;

        // Also register AgentExtension for backward compat
        AgentExtension extension;
        extension.agent_type = "forge";
        for (const auto& guard : as_list(guardchains_value)->items)
        {
          extension.guardchains.push_back(to_std_string(guard));
        }
        if (policy_value.is_string())
          extension.policy = to_std_string(policy_value);
        if (budget_value.is_string())
          extension.budget = to_std_string(budget_value);
        if (env_value.is_string())
          extension.env = to_std_string(env_value);
        agent_extensions_[agent_name] = std::move(extension);
        break;
      }
      // v0.9: Schema definition
      case OpCode::OP_DEFINE_SCHEMA:
      {
        Value version_value = pop();
        Value fields_value = pop();
        Value name_value = pop();

        auto* schema = new_schema_obj();
        schema->name = as_string(name_value);
        if (fields_value.is_map())
          schema->fields = as_map(fields_value);
        if (!version_value.is_nil())
          schema->version = static_cast<int>(version_value.as_number());

        std::string schema_name(schema->name->chars, schema->name->length);
        globals_.set(schema->name, Value::ObjVal(reinterpret_cast<Obj*>(schema)));
        break;
      }
      // v0.9: Source definition
      case OpCode::OP_DEFINE_SOURCE:
      {
        Value partition_by_value = pop();
        Value mode_value = pop();
        Value classification_value = pop();
        Value schema_ref_value = pop();
        Value refresh_value = pop();
        Value format_value = pop();
        Value connection_value = pop();
        Value type_value = pop();
        Value name_value = pop();

        auto* source = new_source();
        source->name = as_string(name_value);
        source->source_type = as_string(type_value);
        source->connection = as_string(connection_value);
        source->format = format_value.is_nil() ? nullptr : as_string(format_value);
        source->refresh = refresh_value.is_nil() ? nullptr : as_string(refresh_value);
        source->schema_ref = schema_ref_value.is_nil() ? nullptr : as_string(schema_ref_value);
        source->classification = classification_value.is_nil() ? nullptr : as_string(classification_value);
        source->mode = mode_value.is_nil() ? nullptr : as_string(mode_value);
        source->partition_by = partition_by_value.is_list() ? as_list(partition_by_value) : nullptr;

        std::string src_name(source->name->chars, source->name->length);
        globals_.set(source->name, Value::ObjVal(reinterpret_cast<Obj*>(source)));
        break;
      }
      // v0.9: Sink definition
      case OpCode::OP_DEFINE_SINK:
      {
        Value compute_ref_value = pop();
        Value schema_ref_value = pop();
        Value batch_size_value = pop();
        Value write_mode_value = pop();
        Value format_value = pop();
        Value connection_value = pop();
        Value type_value = pop();
        Value name_value = pop();

        auto* sink = new_sink();
        sink->name = as_string(name_value);
        sink->sink_type = as_string(type_value);
        sink->connection = as_string(connection_value);
        sink->format = format_value.is_nil() ? nullptr : as_string(format_value);
        sink->write_mode = write_mode_value.is_nil() ? nullptr : as_string(write_mode_value);
        sink->batch_size = batch_size_value.is_nil() ? 0 : static_cast<int>(batch_size_value.as_number());
        sink->schema_ref = schema_ref_value.is_nil() ? nullptr : as_string(schema_ref_value);
        sink->compute_ref = compute_ref_value.is_nil() ? nullptr : as_string(compute_ref_value);

        globals_.set(sink->name, Value::ObjVal(reinterpret_cast<Obj*>(sink)));
        break;
      }
      // v0.9: Quality gate definition
      case OpCode::OP_DEFINE_QUALITY:
      {
        Value on_violation_value = pop();
        Value anomaly_threshold_value = pop();
        Value drift_detection_value = pop();
        Value uniqueness_value = pop();
        Value completeness_value = pop();
        Value freshness_value = pop();
        Value name_value = pop();

        auto* quality = new_quality_gate();
        quality->name = as_string(name_value);
        quality->freshness = freshness_value.is_nil() ? nullptr : as_string(freshness_value);
        quality->completeness = completeness_value.is_nil() ? -1.0 : completeness_value.as_number();
        quality->uniqueness = uniqueness_value.is_list() ? as_list(uniqueness_value) : nullptr;
        quality->drift_detection = !drift_detection_value.is_nil() && drift_detection_value.as_bool();
        quality->anomaly_threshold = anomaly_threshold_value.is_nil() ? 0.0 : anomaly_threshold_value.as_number();
        quality->on_violation = on_violation_value.is_nil() ? nullptr : as_string(on_violation_value);

        globals_.set(quality->name, Value::ObjVal(reinterpret_cast<Obj*>(quality)));
        break;
      }
      // v0.9: Compute engine definition
      case OpCode::OP_DEFINE_COMPUTE:
      {
        Value config_value = pop();
        Value engine_value = pop();
        Value name_value = pop();

        auto* compute = new_compute_engine();
        compute->name = as_string(name_value);
        compute->engine = as_string(engine_value);
        compute->config = config_value.is_map() ? as_map(config_value) : nullptr;

        globals_.set(compute->name, Value::ObjVal(reinterpret_cast<Obj*>(compute)));
        break;
      }
      // v0.9: Governance policy definition
      case OpCode::OP_DEFINE_GOVERNANCE:
      {
        Value body_value = pop();
        Value name_value = pop();

        auto* gov = new_governance_policy();
        gov->name = as_string(name_value);
        gov->body = body_value.is_map() ? as_map(body_value) : nullptr;

        globals_.set(gov->name, Value::ObjVal(reinterpret_cast<Obj*>(gov)));
        break;
      }
      // v0.9: Catalog definition
      case OpCode::OP_DEFINE_CATALOG:
      {
        Value discovery_value = pop();
        Value register_value = pop();
        Value engine_value = pop();
        Value name_value = pop();

        auto* catalog = new_catalog_obj();
        catalog->name = as_string(name_value);
        catalog->engine = as_string(engine_value);
        catalog->register_opts = register_value.is_map() ? as_map(register_value) : nullptr;
        catalog->discovery = !discovery_value.is_nil() && discovery_value.as_bool();

        globals_.set(catalog->name, Value::ObjVal(reinterpret_cast<Obj*>(catalog)));
        break;
      }
      // v0.9: Data agent definition
      case OpCode::OP_DEFINE_DATA_AGENT:
      {
        Value agent_md_value = pop();
        Value pipeline_value = pop();
        Value autonomy_value = pop();
        Value purpose_value = pop();
        Value role_value = pop();
        Value lineage_value = pop();
        Value catalog_ref_value = pop();
        Value governance_ref_value = pop();
        Value compute_value = pop();
        Value quality_ref_value = pop();
        Value schema_ref_value = pop();
        Value sinks_value = pop();
        Value sources_value = pop();
        Value env_value = pop();
        Value budget_value = pop();
        Value policy_value = pop();
        Value guardchains_value = pop();
        Value skills_value = pop();
        Value system_value = pop();
        Value temperature_value = pop();
        Value api_key_env_value = pop();
        Value endpoint_value = pop();
        Value model_value = pop();
        Value provider_value = pop();
        Value name_value = pop();

        auto* agent = new_data_agent();
        agent->name = as_string(name_value);
        agent->provider = as_string(provider_value);
        agent->model = as_string(model_value);
        agent->endpoint = endpoint_value.is_nil() ? nullptr : as_string(endpoint_value);
        agent->api_key_env = api_key_env_value.is_nil() ? nullptr : as_string(api_key_env_value);
        agent->system = system_value.is_nil() ? nullptr : as_string(system_value);
        agent->temperature = temperature_value.is_nil() ? 0.0 : temperature_value.as_number();
        agent->skills = as_list(skills_value);
        agent->guardchains = as_list(guardchains_value);
        agent->context = new_context();
        agent->sources = as_list(sources_value);
        agent->sinks = as_list(sinks_value);
        agent->schema_ref = schema_ref_value.is_nil() ? nullptr : as_string(schema_ref_value);
        agent->quality_ref = quality_ref_value.is_nil() ? nullptr : as_string(quality_ref_value);
        agent->compute_config = compute_value.is_map() ? as_map(compute_value) : nullptr;
        agent->governance_ref = governance_ref_value.is_nil() ? nullptr : as_string(governance_ref_value);
        agent->catalog_ref = catalog_ref_value.is_nil() ? nullptr : as_string(catalog_ref_value);
        agent->lineage = !lineage_value.is_nil() && lineage_value.as_bool();
        agent->role = role_value.is_nil() ? nullptr : as_string(role_value);
        agent->purpose = purpose_value.is_nil() ? nullptr : as_string(purpose_value);
        agent->autonomy = autonomy_value.is_nil() ? nullptr : as_string(autonomy_value);
        agent->pipeline_config = pipeline_value.is_map() ? as_map(pipeline_value) : nullptr;
        agent->agent_md_path = agent_md_value.is_nil() ? nullptr : as_string(agent_md_value);

        std::string agent_name(agent->name->chars, agent->name->length);
        globals_.set(agent->name, Value::ObjVal(reinterpret_cast<Obj*>(agent)));

        // Register AgentExtension for backward compat
        AgentExtension extension;
        extension.agent_type = "data";
        if (guardchains_value.is_list())
        {
          for (const auto& guard : as_list(guardchains_value)->items)
          {
            extension.guardchains.push_back(to_std_string(guard));
          }
        }
        if (policy_value.is_string())
          extension.policy = to_std_string(policy_value);
        if (budget_value.is_string())
          extension.budget = to_std_string(budget_value);
        if (env_value.is_string())
          extension.env = to_std_string(env_value);
        agent_extensions_[agent_name] = std::move(extension);
        break;
      }
      // v0.9.1: ETL Agent definition
      case OpCode::OP_DEFINE_ETL_AGENT:
      {
        const auto field_count = code[frame.ip++];
        auto* agent = new_etl_agent();

        // Pop field_count * 2 values (key-value pairs) from the stack
        // Fields are pushed in order, so we need to read from the bottom
        std::size_t base = stack_.size() - field_count * 2;

        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];

          if (fname == "name")
            agent->name = as_string(fval);
          else if (fname == "provider")
            agent->provider = as_string(fval);
          else if (fname == "model")
            agent->model = as_string(fval);
          else if (fname == "system")
            agent->system = as_string(fval);
          else if (fname == "temperature")
            agent->temperature = fval.as_number();
          else if (fname == "endpoint")
            agent->endpoint = as_string(fval);
          else if (fname == "api_key_env")
            agent->api_key_env = as_string(fval);
          else if (fname == "warehouse")
          {
            std::string wh_name = to_std_string(fval);
            // Look up warehouse compute engine — will be resolved later if not found
            agent->warehouse = nullptr;  // resolved at run time
          }
          else if (fname == "model_type")
            agent->model_type = to_std_string(fval);
          else if (fname == "self_heal")
            agent->self_heal_enabled = fval.as_bool();
          else if (fname == "on_failure")
            agent->on_failure = to_std_string(fval);
          else if (fname == "has_layers")
          {
            // flag only — layer config handled by staging_prefix etc.
          }
          else if (fname == "staging_prefix")
            agent->layer_config.staging_prefix = to_std_string(fval);
          else if (fname == "staging_materialization")
            agent->layer_config.staging_materialization = to_std_string(fval);
          else if (fname == "integration_prefix")
            agent->layer_config.integration_prefix = to_std_string(fval);
          else if (fname == "integration_materialization")
            agent->layer_config.integration_materialization = to_std_string(fval);
          else if (fname == "mart_count")
          {
            // Just informational; marts are registered separately via OP_DEFINE_MART
          }
          else if (fname == "incremental_strategy")
            agent->incremental.strategy = to_std_string(fval);
          else if (fname == "incremental_key")
            agent->incremental.key = to_std_string(fval);
          else if (fname == "incremental_lookback")
            agent->incremental.lookback = to_std_string(fval);
          else if (fname == "incremental_on_schema_change")
            agent->incremental.on_schema_change = to_std_string(fval);
          else if (fname == "auto_model_enabled")
            agent->auto_model_enabled = fval.as_bool();
          else if (fname == "auto_model_methodology")
            agent->auto_model_methodology = to_std_string(fval);
          else if (fname == "auto_model_approval")
            agent->auto_model_approval = to_std_string(fval);
          else if (fname == "lineage")
            agent->lineage = fval.as_bool();
          else if (fname == "role")
            agent->role = as_string(fval);
          else if (fname == "purpose")
            agent->purpose = as_string(fval);
          else if (fname == "autonomy")
            agent->autonomy = as_string(fval);
          else if (fname == "quality")
            agent->quality_ref = as_string(fval);
          else if (fname == "governance")
            agent->governance_ref = as_string(fval);
          else if (fname == "catalog")
            agent->catalog_ref = as_string(fval);
          else if (fname == "semantic")
          {
            std::string sem_name = to_std_string(fval);
            agent->semantic = nullptr;  // resolved at run time
          }
          else if (fname == "budget")
          {
            // Store budget name for later resolution
          }
          else if (fname == "agent_type")
          {
            // informational: "etl"
          }
          else if (fname == "sources" || fname == "sinks" || fname == "skills" ||
                   fname == "connected_knowledge" || fname == "guardchains")
          {
            // Stored as comma-separated string for Phase 0-2; list resolution at runtime
          }
          else if (fname == "compute_default")
          {
            // stored for later resolution
          }
        }

        stack_.resize(base);

        if (!agent->name) {
          throw std::runtime_error("ETL agent missing name");
        }

        // Register agent
        std::string agent_name(agent->name->chars, agent->name->length);
        globals_.set(agent->name, Value::ObjVal(reinterpret_cast<Obj*>(agent)));

        // Register AgentExtension for backward compat
        AgentExtension extension;
        extension.agent_type = "etl";
        agent_extensions_[agent_name] = std::move(extension);
        break;
      }

      // v0.9.1: Mart definition
      case OpCode::OP_DEFINE_MART:
      {
        const auto field_count = code[frame.ip++];
        auto* mart = new_mart();

        std::size_t base = stack_.size() - field_count * 2;

        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];

          if (fname == "name")
            mart->name = to_std_string(fval);
          else if (fname == "grain")
            mart->grain = to_std_string(fval);
          else if (fname == "materialization")
            mart->materialization = to_std_string(fval);
          // Comma-separated list fields
          else if (fname == "facts" || fname == "dimensions" || fname == "measures" || fname == "conformed")
          {
            // Stored as comma-separated string; split at runtime
          }
          else if (fname == "scd")
          {
            // Stored as comma-separated "dim:type" pairs; parsed at runtime
          }
        }

        stack_.resize(base);

        auto* name_str = copy_string(mart->name.c_str(), mart->name.size());
        globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(mart)));
        break;
      }

      // v0.9.1: Semantic layer definition
      case OpCode::OP_DEFINE_SEMANTIC:
      {
        const auto field_count = code[frame.ip++];
        auto* semantic = new_semantic_layer();

        std::size_t base = stack_.size() - field_count * 2;

        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];

          if (fname == "name")
            semantic->name = to_std_string(fval);
          else if (fname == "fiscal_year_start")
            semantic->fiscal_year_start = to_std_string(fval);
          else if (fname == "week_start")
            semantic->week_start = to_std_string(fval);
          else if (fname == "default_timezone")
            semantic->default_timezone = to_std_string(fval);
          // Metrics, entities, and synonyms are serialized inline with counts
          // Full deserialization handled in later phases
        }

        stack_.resize(base);

        auto* name_str = copy_string(semantic->name.c_str(), semantic->name.size());
        globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(semantic)));
        break;
      }

      // v0.9.1: Stub handlers for Phase 3-10 opcodes (push nil for now)
      case OpCode::OP_SQL_TRANSPILE:
      case OpCode::OP_SQL_PUSHDOWN:
      case OpCode::OP_NL2SQL:
      case OpCode::OP_AUTO_MODEL:
      case OpCode::OP_SELF_HEAL:
      {
        // Phase 3-10 runtime features — stub for now
        // Pop any arguments and push nil
        pop();  // first arg
        pop();  // second arg
        stack_.push_back(Value::Nil());
        break;
      }
      // v0.9.2: Migration Agent definition
      case OpCode::OP_DEFINE_MIGRATION_AGENT:
      {
        const auto field_count = code[frame.ip++];
        auto* agent = new_migration_agent();

        std::size_t base = stack_.size() - field_count * 2;

        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];

          if (fname == "name")
            agent->name = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "agent_type")
            { /* skip — used for type tracking */ }
          else if (fname == "provider")
            agent->provider = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "model")
            agent->model = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "system")
            agent->system = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "temperature")
            agent->temperature = fval.as_number();
          else if (fname == "budget")
            { /* resolved at runtime */ }
          else if (fname == "skills")
            { /* resolved at runtime */ }
          else if (fname == "source")
            agent->source_name = to_std_string(fval);
          else if (fname == "target")
            agent->target_name = to_std_string(fval);
          else if (fname == "staging")
            agent->staging_name = to_std_string(fval);
          else if (fname == "strategy")
            agent->strategy = to_std_string(fval);
          else if (fname == "waves")
          {
            auto j = nlohmann::json::parse(to_std_string(fval));
            agent->wave_plan = new_wave_plan();
            agent->wave_plan->generation_mode = j.value("mode", "auto");
            agent->wave_plan->max_tables_per_wave = j.value("max_tables_per_wave", 50);
            agent->wave_plan->max_parallel_extractions = j.value("max_parallel_extractions", 4);
          }
          else if (fname == "movement")
            agent->movement_config = nlohmann::json::parse(to_std_string(fval));
          else if (fname == "schema_translation")
            agent->schema_translation_config = nlohmann::json::parse(to_std_string(fval));
          else if (fname == "validation")
            agent->validation_config = nlohmann::json::parse(to_std_string(fval));
          else if (fname == "cutover")
            agent->cutover_config = nlohmann::json::parse(to_std_string(fval));
          else if (fname == "self_heal")
            agent->self_heal_config = nlohmann::json::parse(to_std_string(fval));
          else if (fname == "assessment")
            agent->assessment_config = nlohmann::json::parse(to_std_string(fval));
          else if (fname == "governance")
          {
            auto gj = nlohmann::json::parse(to_std_string(fval));
            auto* gov = new_governance_mig();
            gov->name = to_std_string(stack_[base]) + "_governance";
            gov->preserve_classification = gj.value("preserve_classification", false);
            gov->pii_detection = gj.value("pii_detection", false);
            gov->log_all_sql = gj.value("log_all_sql", false);
            gov->log_all_data_movement = gj.value("log_all_data_movement", false);
            gov->audit_retention = gj.value("audit_retention", "7y");
            agent->governance_obj = gov;
          }
          // DataAgent inherited fields
          else if (fname == "role")
            agent->role = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "purpose")
            agent->purpose = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "autonomy")
            agent->autonomy = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "agent_md")
            agent->agent_md_path = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
        }

        stack_.resize(base);

        // Initialize schema map
        agent->schema_map_obj = new_schema_map();

        // Set initial lifecycle phase
        agent->phase = MigrationPhase::ASSESS;
        agent->migration_started = std::chrono::system_clock::now();

        if (!agent->name) {
          throw std::runtime_error("Migration agent missing name");
        }

        // Register agent
        std::string agent_name(agent->name->chars, agent->name->length);
        globals_.set(agent->name, Value::ObjVal(reinterpret_cast<Obj*>(agent)));

        // Register AgentExtension
        AgentExtension extension;
        extension.agent_type = "migration";
        agent_extensions_[agent_name] = std::move(extension);
        break;
      }

      // v0.9.2: Migration phase opcodes (Phase 3-10 runtime)
      // These are invoked via the native function API (migration_assess, etc.).
      // The opcodes handle the case where the compiler emits them directly
      // with the agent value on the stack.
      case OpCode::OP_MIGRATION_ASSESS:
      case OpCode::OP_MIGRATION_PLAN_WAVES:
      case OpCode::OP_MIGRATION_TRANSLATE_SCHEMA:
      case OpCode::OP_MIGRATION_MOVE_DATA:
      case OpCode::OP_MIGRATION_RECONCILE:
      case OpCode::OP_MIGRATION_CUTOVER:
      case OpCode::OP_MIGRATION_ROLLBACK:
      case OpCode::OP_MIGRATION_SELF_HEAL:
      {
        // Pop agent value from stack, dispatch to engine
        Value agent_val = pop();
        if (agent_val.is_obj() && agent_val.as_obj()->type == ObjType::OBJ_MIGRATION_AGENT) {
          auto* agent = static_cast<ObjMigrationAgent*>(agent_val.as_obj());
          auto opcode = static_cast<OpCode>(code[frame.ip - 1]);
          switch (opcode) {
            case OpCode::OP_MIGRATION_ASSESS:
              agent->phase = MigrationPhase::PLAN;
              stack_.push_back(Value::Map(new_map({
                {"total_objects", Value::Number(static_cast<double>(agent->objects.size()))}
              })));
              break;
            case OpCode::OP_MIGRATION_PLAN_WAVES:
              if (!agent->wave_plan) agent->wave_plan = new_wave_plan();
              stack_.push_back(Value::ObjVal(agent->wave_plan));
              break;
            case OpCode::OP_MIGRATION_TRANSLATE_SCHEMA:
              if (!agent->schema_map_obj) agent->schema_map_obj = new_schema_map();
              stack_.push_back(Value::ObjVal(agent->schema_map_obj));
              break;
            case OpCode::OP_MIGRATION_MOVE_DATA:
              agent->phase = MigrationPhase::EXECUTE;
              stack_.push_back(Value::Bool(true));
              break;
            case OpCode::OP_MIGRATION_RECONCILE: {
              agent->phase = MigrationPhase::VALIDATE;
              auto* result = new_reconciliation_result();
              result->overall_passed = true;
              stack_.push_back(Value::ObjVal(result));
              break;
            }
            case OpCode::OP_MIGRATION_CUTOVER:
              agent->phase = MigrationPhase::CUTOVER;
              stack_.push_back(Value::Bool(true));
              break;
            case OpCode::OP_MIGRATION_ROLLBACK:
              agent->phase = MigrationPhase::ROLLBACK;
              stack_.push_back(Value::Bool(true));
              break;
            case OpCode::OP_MIGRATION_SELF_HEAL:
              agent->total_remediations++;
              stack_.push_back(Value::Bool(true));
              break;
            default:
              stack_.push_back(Value::Nil());
              break;
          }
        } else {
          stack_.push_back(Value::Nil());
        }
        break;
      }

      // v0.9.3: Scheduler definition
      case OpCode::OP_DEFINE_SCHEDULER:
      {
        const auto field_count = code[frame.ip++];
        auto* obj = new_scheduler_obj();

        std::size_t base = stack_.size() - field_count * 2;

        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];

          if (fname == "name")
            obj->name = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "type")
            obj->scheduler_type = to_std_string(fval);
          else if (fname == "connection")
            obj->connection = to_std_string(fval);
          else if (fname == "credentials")
            obj->credentials = to_std_string(fval);
          else if (fname == "poll_interval")
            obj->poll_interval = to_std_string(fval);
          else if (fname == "timezone")
            obj->timezone = to_std_string(fval);
          else if (fname == "datacenter")
            obj->datacenter = to_std_string(fval);
          else if (fname == "host")
            obj->host = to_std_string(fval);
          else if (fname == "filters")
          {
            // comma-separated list
            std::string raw = to_std_string(fval);
            std::istringstream iss(raw);
            std::string item;
            while (std::getline(iss, item, ',')) obj->filters.push_back(item);
          }
        }

        stack_.resize(base);

        if (obj->name) {
          std::string sname(obj->name->chars, obj->name->length);
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }
      // v0.9.3: Audit table definition
      case OpCode::OP_DEFINE_AUDIT_TABLE:
      {
        const auto field_count = code[frame.ip++];
        auto* obj = new_audit_table_obj();

        std::size_t base = stack_.size() - field_count * 2;

        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];

          if (fname == "name")
            obj->name = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "source")
            obj->source_ref = to_std_string(fval);
          else if (fname == "table")
            obj->table_name = to_std_string(fval);
          else if (fname == "column_map")
            obj->column_map_json = nlohmann::json::parse(to_std_string(fval));
          else if (fname == "poll_interval")
            obj->poll_interval = to_std_string(fval);
          else if (fname == "lookback_window")
            obj->lookback_window = to_std_string(fval);
          else if (fname == "retention_analysis")
            obj->retention_analysis = to_std_string(fval);
          else if (fname == "anomalies")
          {
            auto aj = nlohmann::json::parse(to_std_string(fval));
            obj->row_count_drop_threshold = aj.value("row_count_drop", 0.20);
            obj->row_count_spike_threshold = aj.value("row_count_spike", 3.0);
            obj->duration_spike_threshold = aj.value("duration_spike", 2.0);
            obj->failure_rate_threshold = aj.value("failure_rate", 0.05);
            obj->zero_rows_consecutive_threshold = aj.value("zero_rows_consecutive", 3);
          }
        }

        stack_.resize(base);

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }
      // v0.9.3: Log source definition
      case OpCode::OP_DEFINE_LOG_SOURCE:
      {
        const auto field_count = code[frame.ip++];
        auto* obj = new_log_source_obj();

        std::size_t base = stack_.size() - field_count * 2;

        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];

          if (fname == "name")
            obj->name = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "type")
            obj->source_type = to_std_string(fval);
          else if (fname == "connection")
            obj->connection = to_std_string(fval);
          else if (fname == "credentials")
            obj->credentials = to_std_string(fval);
          else if (fname == "views")
          {
            std::string raw = to_std_string(fval);
            std::istringstream iss(raw);
            std::string item;
            while (std::getline(iss, item, ',')) obj->views.push_back(item);
          }
          else if (fname == "log_file")
            obj->log_file = to_std_string(fval);
          else if (fname == "log_format")
            obj->log_format = to_std_string(fval);
          else if (fname == "poll_interval")
            obj->poll_interval = to_std_string(fval);
          else if (fname == "lookback_window")
            obj->lookback_window = to_std_string(fval);
          else if (fname == "alerts")
            obj->alerts_json = nlohmann::json::parse(to_std_string(fval));
        }

        stack_.resize(base);

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }
      // v0.9.3: Platform monitor definition
      case OpCode::OP_DEFINE_PLATFORM:
      {
        const auto field_count = code[frame.ip++];
        auto* obj = new_platform_monitor();

        std::size_t base = stack_.size() - field_count * 2;

        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];

          if (fname == "name")
            obj->name = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "type")
            obj->platform_type = to_std_string(fval);
          else if (fname == "connection")
            obj->connection = to_std_string(fval);
          else if (fname == "credentials")
            obj->credentials = to_std_string(fval);
          else if (fname == "database")
            obj->database = to_std_string(fval);
          else if (fname == "health_checks")
            obj->health_checks_json = nlohmann::json::parse(to_std_string(fval));
          else if (fname == "finops")
            obj->finops_json = nlohmann::json::parse(to_std_string(fval));
        }

        stack_.resize(base);

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }
      // v0.9.3: Incident policy definition
      case OpCode::OP_DEFINE_INCIDENT_POLICY:
      {
        const auto field_count = code[frame.ip++];
        auto* obj = new_incident_policy();

        std::size_t base = stack_.size() - field_count * 2;

        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];

          if (fname == "name")
            obj->name = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "severity")
            obj->severity_rules_json = nlohmann::json::parse(to_std_string(fval));
          else if (fname == "auto_heal")
            obj->auto_heal_json = nlohmann::json::parse(to_std_string(fval));
        }

        stack_.resize(base);

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }
      // v0.9.3: Correlation definition
      case OpCode::OP_DEFINE_CORRELATION:
      {
        const auto field_count = code[frame.ip++];
        auto* obj = new_correlation_obj();

        std::size_t base = stack_.size() - field_count * 2;

        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];

          if (fname == "name")
            obj->name = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "scope")
            obj->scope_json = nlohmann::json::parse(to_std_string(fval));
          else if (fname == "time_window")
            obj->time_window = to_std_string(fval);
          else if (fname == "sla")
            obj->sla_json = nlohmann::json::parse(to_std_string(fval));
          else if (fname == "dependencies")
            obj->dependencies_json = nlohmann::json::parse(to_std_string(fval));
        }

        stack_.resize(base);

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }
      // v0.9.3: DataOps agent definition
      case OpCode::OP_DEFINE_DATAOPS_AGENT:
      {
        const auto field_count = code[frame.ip++];
        auto* agent = new_dataops_agent();

        std::size_t base = stack_.size() - field_count * 2;

        for (std::size_t i = 0; i < field_count; ++i)
        {
          std::string fname = to_std_string(stack_[base + i * 2]);
          Value fval = stack_[base + i * 2 + 1];

          if (fname == "name")
            agent->name = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "agent_type")
            { /* skip — used for type tracking */ }
          else if (fname == "provider")
            agent->provider = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "model")
            agent->model = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "system")
            agent->system = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "temperature")
            agent->temperature = fval.as_number();
          else if (fname == "budget")
            { /* resolved at runtime */ }
          else if (fname == "skills")
            { /* resolved at runtime */ }
          else if (fname == "guardchains")
            { /* resolved at runtime */ }
          else if (fname == "endpoint")
            agent->endpoint = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "api_key_env")
            agent->api_key_env = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          // DataAgent inherited fields
          else if (fname == "role")
            agent->role = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "purpose")
            agent->purpose = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "autonomy")
            agent->autonomy = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          else if (fname == "agent_md")
            agent->agent_md_path = copy_string(to_std_string(fval).c_str(), to_std_string(fval).size());
          // DataOps-specific references
          else if (fname == "platforms")
          {
            std::string raw = to_std_string(fval);
            std::istringstream iss(raw);
            std::string item;
            while (std::getline(iss, item, ',')) agent->platform_refs.push_back(item);
          }
          else if (fname == "schedulers")
          {
            std::string raw = to_std_string(fval);
            std::istringstream iss(raw);
            std::string item;
            while (std::getline(iss, item, ',')) agent->scheduler_refs.push_back(item);
          }
          else if (fname == "audit_tables")
          {
            std::string raw = to_std_string(fval);
            std::istringstream iss(raw);
            std::string item;
            while (std::getline(iss, item, ',')) agent->audit_table_refs.push_back(item);
          }
          else if (fname == "log_sources")
          {
            std::string raw = to_std_string(fval);
            std::istringstream iss(raw);
            std::string item;
            while (std::getline(iss, item, ',')) agent->log_source_refs.push_back(item);
          }
          else if (fname == "correlations")
          {
            std::string raw = to_std_string(fval);
            std::istringstream iss(raw);
            std::string item;
            while (std::getline(iss, item, ',')) agent->correlation_refs.push_back(item);
          }
          else if (fname == "incident_policy")
            agent->incident_policy_ref = to_std_string(fval);
          else if (fname == "mode")
            agent->mode = to_std_string(fval);
          else if (fname == "reports")
            agent->reports_json = nlohmann::json::parse(to_std_string(fval));
        }

        stack_.resize(base);

        // Set initial state
        agent->phase = DataOpsPhase::IDLE;
        agent->started_at = std::chrono::steady_clock::now();

        if (!agent->name) {
          throw std::runtime_error("DataOps agent missing name");
        }

        // Register agent
        std::string agent_name(agent->name->chars, agent->name->length);
        globals_.set(agent->name, Value::ObjVal(reinterpret_cast<Obj*>(agent)));

        // Register AgentExtension
        AgentExtension extension;
        extension.agent_type = "dataops";
        agent_extensions_[agent_name] = std::move(extension);
        break;
      }

      // v0.9.3: DataOps runtime opcodes (stubs — dispatched via native functions)
      case OpCode::OP_DATAOPS_START_MONITOR:
      case OpCode::OP_DATAOPS_STOP_MONITOR:
      case OpCode::OP_DATAOPS_TRIAGE:
      case OpCode::OP_DATAOPS_INVESTIGATE:
      case OpCode::OP_DATAOPS_REMEDIATE:
      {
        Value agent_val = pop();
        if (agent_val.is_obj() && agent_val.as_obj()->type == ObjType::OBJ_DATAOPS_AGENT) {
          auto* agent = static_cast<ObjDataOpsAgent*>(agent_val.as_obj());
          auto opcode = static_cast<OpCode>(code[frame.ip - 1]);
          switch (opcode) {
            case OpCode::OP_DATAOPS_START_MONITOR:
              agent->phase = DataOpsPhase::MONITORING;
              agent->monitoring_active = true;
              stack_.push_back(Value::Bool(true));
              break;
            case OpCode::OP_DATAOPS_STOP_MONITOR:
              agent->monitoring_active = false;
              agent->phase = DataOpsPhase::IDLE;
              stack_.push_back(Value::Bool(true));
              break;
            case OpCode::OP_DATAOPS_TRIAGE:
              agent->phase = DataOpsPhase::TRIAGING;
              stack_.push_back(Value::Bool(true));
              break;
            case OpCode::OP_DATAOPS_INVESTIGATE:
              agent->phase = DataOpsPhase::INVESTIGATING;
              stack_.push_back(Value::Bool(true));
              break;
            case OpCode::OP_DATAOPS_REMEDIATE:
              agent->phase = DataOpsPhase::REMEDIATING;
              agent->remediations_today++;
              stack_.push_back(Value::Bool(true));
              break;
            default:
              stack_.push_back(Value::Nil());
              break;
          }
        } else {
          stack_.push_back(Value::Nil());
        }
        break;
      }

      // ═══ v0.9.4 Governance Agent VM handlers ═══

      case OpCode::OP_DEFINE_CATALOG_SOURCE:
      case OpCode::OP_DEFINE_GOV_CATALOG:
      case OpCode::OP_DEFINE_GLOSSARY:
      case OpCode::OP_DEFINE_CLASSIFICATION_POLICY:
      case OpCode::OP_DEFINE_ACCESS_POLICY:
      case OpCode::OP_DEFINE_QUALITY_POLICY:
      case OpCode::OP_DEFINE_LINEAGE_POLICY:
      case OpCode::OP_DEFINE_COMPLIANCE_POLICY:
      case OpCode::OP_DEFINE_LIFECYCLE_POLICY:
      case OpCode::OP_DEFINE_DATA_PRODUCT:
      case OpCode::OP_DEFINE_CONTRACT_POLICY:
      case OpCode::OP_DEFINE_MASTER_DATA:
      case OpCode::OP_DEFINE_EXTERNAL_TOOL:
      {
        auto gov_opcode = static_cast<OpCode>(code[frame.ip - 1]);
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        Obj* result_obj = nullptr;
        ObjString* obj_name = nullptr;

        switch (gov_opcode) {
        case OpCode::OP_DEFINE_CATALOG_SOURCE: {
          auto* obj = new_gov_catalog_source();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("type")) obj->source_type = fields["type"];
          if (fields.count("connection")) obj->connection = fields["connection"];
          if (fields.count("credentials")) obj->credentials = fields["credentials"];
          if (fields.count("sync_mode")) obj->sync_mode = fields["sync_mode"];
          if (fields.count("conflict_resolution")) obj->conflict_resolution = fields["conflict_resolution"];
          if (fields.count("include_views")) obj->include_views = (fields["include_views"] == "true");
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_GOV_CATALOG: {
          auto* obj = new_gov_catalog();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("sources")) {
            std::istringstream iss(fields["sources"]); std::string s;
            while (std::getline(iss, s, ',')) obj->source_refs.push_back(s);
          }
          if (fields.count("auto_document")) obj->auto_document_json = fields["auto_document"];
          if (fields.count("staleness_threshold")) obj->staleness_threshold = fields["staleness_threshold"];
          if (fields.count("shadow_dataset_detection")) obj->shadow_dataset_detection = (fields["shadow_dataset_detection"] == "true");
          if (fields.count("ownership")) obj->ownership_json = fields["ownership"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_GLOSSARY: {
          auto* obj = new_glossary_obj();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("domains")) {
            std::istringstream iss(fields["domains"]); std::string s;
            while (std::getline(iss, s, ',')) obj->domains.push_back(s);
          }
          if (fields.count("auto_suggest")) obj->auto_suggest_json = fields["auto_suggest"];
          if (fields.count("synonym_detection")) obj->synonym_detection_json = fields["synonym_detection"];
          if (fields.count("terms")) obj->terms_json = fields["terms"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_CLASSIFICATION_POLICY: {
          auto* obj = new_classification_policy();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("levels")) obj->levels_json = fields["levels"];
          if (fields.count("auto_classify")) obj->auto_classify_json = fields["auto_classify"];
          if (fields.count("propagation")) obj->propagation_json = fields["propagation"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_ACCESS_POLICY: {
          auto* obj = new_access_policy();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("model")) obj->access_model = fields["model"];
          if (fields.count("roles")) obj->roles_json = fields["roles"];
          if (fields.count("attributes")) obj->attributes_json = fields["attributes"];
          if (fields.count("access_review")) obj->access_review_json = fields["access_review"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_QUALITY_POLICY: {
          auto* obj = new_quality_policy();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("profiling")) obj->profiling_json = fields["profiling"];
          if (fields.count("rules")) obj->rules_json = fields["rules"];
          if (fields.count("scoring")) obj->scoring_json = fields["scoring"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_LINEAGE_POLICY: {
          auto* obj = new_lineage_policy();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("auto_discover")) obj->auto_discover_json = fields["auto_discover"];
          if (fields.count("impact_analysis")) obj->impact_analysis_json = fields["impact_analysis"];
          if (fields.count("tag_propagation")) obj->tag_propagation_json = fields["tag_propagation"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_COMPLIANCE_POLICY: {
          auto* obj = new_compliance_policy();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("regulations")) {
            std::istringstream iss(fields["regulations"]); std::string s;
            while (std::getline(iss, s, ',')) obj->regulations.push_back(s);
          }
          if (fields.count("gdpr")) obj->gdpr_json = fields["gdpr"];
          if (fields.count("ccpa")) obj->ccpa_json = fields["ccpa"];
          if (fields.count("hipaa")) obj->hipaa_json = fields["hipaa"];
          if (fields.count("monitoring")) obj->monitoring_json = fields["monitoring"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_LIFECYCLE_POLICY: {
          auto* obj = new_lifecycle_policy();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("retention")) obj->retention_json = fields["retention"];
          if (fields.count("tiering")) obj->tiering_json = fields["tiering"];
          if (fields.count("legal_hold")) obj->legal_hold_json = fields["legal_hold"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_DATA_PRODUCT: {
          auto* obj = new_data_product();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("domain")) obj->domain = fields["domain"];
          if (fields.count("owner")) obj->owner = fields["owner"];
          if (fields.count("description")) obj->description = fields["description"];
          if (fields.count("contract")) obj->contract_json = fields["contract"];
          if (fields.count("quality")) obj->quality_json = fields["quality"];
          if (fields.count("access")) obj->access_json = fields["access"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_CONTRACT_POLICY: {
          auto* obj = new_contract_policy();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("schema_validation_on_deploy")) obj->schema_validation_on_deploy = (fields["schema_validation_on_deploy"] == "true");
          if (fields.count("breaking_change_detection")) obj->breaking_change_detection = (fields["breaking_change_detection"] == "true");
          if (fields.count("notify_consumers")) obj->notify_consumers = (fields["notify_consumers"] == "true");
          if (fields.count("versioning_strategy")) obj->versioning_strategy = fields["versioning_strategy"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_MASTER_DATA: {
          auto* obj = new_master_data_obj();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("entity")) obj->entity = fields["entity"];
          if (fields.count("golden_source")) obj->golden_source = fields["golden_source"];
          if (fields.count("contributing_sources")) {
            std::istringstream iss(fields["contributing_sources"]); std::string s;
            while (std::getline(iss, s, ',')) obj->contributing_sources.push_back(s);
          }
          if (fields.count("matching")) obj->matching_json = fields["matching"];
          if (fields.count("survivorship")) obj->survivorship_json = fields["survivorship"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        case OpCode::OP_DEFINE_EXTERNAL_TOOL: {
          auto* obj = new_gov_external_tool();
          if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
          if (fields.count("type")) obj->tool_type = fields["type"];
          if (fields.count("connection")) obj->connection = fields["connection"];
          if (fields.count("credentials")) obj->credentials = fields["credentials"];
          if (fields.count("capabilities")) obj->capabilities_json = fields["capabilities"];
          if (fields.count("sync_interval")) obj->sync_interval = fields["sync_interval"];
          if (fields.count("conflict_resolution")) obj->conflict_resolution = fields["conflict_resolution"];
          obj_name = obj->name;
          result_obj = reinterpret_cast<Obj*>(obj);
          break;
        }
        default: break;
        }

        if (result_obj && obj_name)
        {
          globals_.set(obj_name, Value::ObjVal(result_obj));
        }
        break;
      }

      case OpCode::OP_DEFINE_GOVERNANCE_AGENT:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* agent = new_governance_agent();
        if (fields.count("name")) agent->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("provider")) agent->provider = copy_string(fields["provider"].c_str(), fields["provider"].size());
        if (fields.count("model")) agent->model = copy_string(fields["model"].c_str(), fields["model"].size());
        if (fields.count("endpoint")) agent->endpoint = copy_string(fields["endpoint"].c_str(), fields["endpoint"].size());
        if (fields.count("api_key_env")) agent->api_key_env = copy_string(fields["api_key_env"].c_str(), fields["api_key_env"].size());
        if (fields.count("temperature")) agent->temperature = std::stod(fields["temperature"]);
        if (fields.count("system")) agent->system_prompt = fields["system"];
        if (fields.count("budget")) agent->budget_ref = fields["budget"];
        // Pillar refs
        if (fields.count("catalog")) agent->catalog_ref = fields["catalog"];
        if (fields.count("glossary")) agent->glossary_ref = fields["glossary"];
        if (fields.count("classification")) agent->classification_ref = fields["classification"];
        if (fields.count("access_control")) agent->access_control_ref = fields["access_control"];
        if (fields.count("quality")) agent->quality_ref = fields["quality"];
        if (fields.count("lineage")) agent->lineage_ref = fields["lineage"];
        if (fields.count("compliance")) agent->compliance_ref = fields["compliance"];
        if (fields.count("lifecycle")) agent->lifecycle_ref = fields["lifecycle"];
        // Ref lists
        auto split = [](const std::string& s) {
          std::vector<std::string> v;
          std::istringstream iss(s); std::string item;
          while (std::getline(iss, item, ',')) v.push_back(item);
          return v;
        };
        if (fields.count("external_tools")) agent->external_tool_refs = split(fields["external_tools"]);
        if (fields.count("coordinates_with")) agent->coordinates_with_refs = split(fields["coordinates_with"]);
        if (fields.count("skills")) agent->skill_refs = split(fields["skills"]);
        if (fields.count("guardchains")) agent->guardchain_refs = split(fields["guardchains"]);
        if (fields.count("reports")) agent->reports_json = fields["reports"];
        if (fields.count("policy")) agent->policy_ref = fields["policy"];
        if (fields.count("agent_md")) agent->agent_md_path = fields["agent_md"];

        // Register as global
        if (agent->name) {
          globals_.set(agent->name, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        break;
      }

      case OpCode::OP_GOVERNANCE_CLASSIFY:
      case OpCode::OP_GOVERNANCE_TRACE_LINEAGE:
      {
        // Runtime operations — stub for now, delegate to native functions
        stack_.push_back(Value::Nil());
        break;
      }

      // ═══════════════════════════════════════════════════════════
      // v0.9.5: Modeling Agent opcodes
      // ═══════════════════════════════════════════════════════════

      case OpCode::OP_DEFINE_SCHEMA_SOURCE:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* obj = new_schema_source();
        if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("type")) obj->source_type = copy_string(fields["type"].c_str(), fields["type"].size());
        if (fields.count("connection")) obj->connection = copy_string(fields["connection"].c_str(), fields["connection"].size());
        if (fields.count("credentials")) obj->credentials = copy_string(fields["credentials"].c_str(), fields["credentials"].size());
        if (fields.count("scan_interval")) obj->scan_interval = copy_string(fields["scan_interval"].c_str(), fields["scan_interval"].size());
        if (fields.count("path")) obj->path = copy_string(fields["path"].c_str(), fields["path"].size());
        if (fields.count("format")) obj->format = copy_string(fields["format"].c_str(), fields["format"].size());
        if (fields.count("model_type")) obj->model_type = copy_string(fields["model_type"].c_str(), fields["model_type"].size());
        if (fields.count("sync_direction")) obj->sync_direction = copy_string(fields["sync_direction"].c_str(), fields["sync_direction"].size());
        if (fields.count("api_connection")) obj->api_connection = copy_string(fields["api_connection"].c_str(), fields["api_connection"].size());
        if (fields.count("dialect")) obj->dialect = copy_string(fields["dialect"].c_str(), fields["dialect"].size());
        if (fields.count("project_path")) obj->project_path = copy_string(fields["project_path"].c_str(), fields["project_path"].size());
        if (fields.count("manifest_path")) obj->manifest_path = copy_string(fields["manifest_path"].c_str(), fields["manifest_path"].size());
        if (fields.count("include_views")) obj->include_views = (fields["include_views"] == "true");
        if (fields.count("include_procedures")) obj->include_procedures = (fields["include_procedures"] == "true");
        if (fields.count("read_constraints")) obj->read_constraints = (fields["read_constraints"] == "true");
        if (fields.count("read_indexes")) obj->read_indexes = (fields["read_indexes"] == "true");
        if (fields.count("read_statistics")) obj->read_statistics = (fields["read_statistics"] == "true");
        if (fields.count("sample_size")) obj->sample_size = std::stoi(fields["sample_size"]);
        if (fields.count("infer_relationships")) obj->infer_relationships_json = copy_string(fields["infer_relationships"].c_str(), fields["infer_relationships"].size());
        if (fields.count("schema_evolution")) obj->schema_evolution_json = copy_string(fields["schema_evolution"].c_str(), fields["schema_evolution"].size());

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }

      case OpCode::OP_DEFINE_ER_MODEL:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* obj = new_er_model();
        if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("version")) obj->version = copy_string(fields["version"].c_str(), fields["version"].size());
        if (fields.count("source")) obj->source = copy_string(fields["source"].c_str(), fields["source"].size());
        if (fields.count("notation")) obj->notation_json = copy_string(fields["notation"].c_str(), fields["notation"].size());
        if (fields.count("domains")) obj->domains_json = copy_string(fields["domains"].c_str(), fields["domains"].size());
        if (fields.count("relationship_inference")) obj->relationship_inference_json = copy_string(fields["relationship_inference"].c_str(), fields["relationship_inference"].size());
        if (fields.count("sync")) obj->sync_json = copy_string(fields["sync"].c_str(), fields["sync"].size());

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }

      case OpCode::OP_DEFINE_ENTITY_MODEL:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* obj = new_entity_obj();
        if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("domain")) obj->domain = copy_string(fields["domain"].c_str(), fields["domain"].size());
        if (fields.count("attributes")) obj->attributes_json = copy_string(fields["attributes"].c_str(), fields["attributes"].size());
        if (fields.count("relationships")) obj->relationships_json = copy_string(fields["relationships"].c_str(), fields["relationships"].size());
        if (fields.count("glossary_term")) obj->glossary_term = copy_string(fields["glossary_term"].c_str(), fields["glossary_term"].size());
        if (fields.count("owner")) obj->owner = copy_string(fields["owner"].c_str(), fields["owner"].size());
        if (fields.count("description")) obj->description = copy_string(fields["description"].c_str(), fields["description"].size());

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }

      case OpCode::OP_DEFINE_DIMENSIONAL_MODEL:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* obj = new_dimensional_model();
        if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("methodology")) obj->methodology = copy_string(fields["methodology"].c_str(), fields["methodology"].size());
        if (fields.count("source")) obj->source = copy_string(fields["source"].c_str(), fields["source"].size());
        if (fields.count("facts")) obj->facts_json = copy_string(fields["facts"].c_str(), fields["facts"].size());
        if (fields.count("dimensions")) obj->dimensions_json = copy_string(fields["dimensions"].c_str(), fields["dimensions"].size());
        if (fields.count("target_platform")) obj->target_platform = copy_string(fields["target_platform"].c_str(), fields["target_platform"].size());
        if (fields.count("target_schema")) obj->target_schema = copy_string(fields["target_schema"].c_str(), fields["target_schema"].size());
        if (fields.count("output")) obj->output_json = copy_string(fields["output"].c_str(), fields["output"].size());

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }

      case OpCode::OP_DEFINE_DATAMART_V095:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* obj = new_datamart_v095();
        if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("dimensional_model")) obj->dimensional_model = copy_string(fields["dimensional_model"].c_str(), fields["dimensional_model"].size());
        if (fields.count("purpose")) obj->purpose = copy_string(fields["purpose"].c_str(), fields["purpose"].size());
        if (fields.count("owner")) obj->owner = copy_string(fields["owner"].c_str(), fields["owner"].size());
        if (fields.count("additional_dimensions")) obj->additional_dimensions_json = copy_string(fields["additional_dimensions"].c_str(), fields["additional_dimensions"].size());
        if (fields.count("aggregate_tables")) obj->aggregate_tables_json = copy_string(fields["aggregate_tables"].c_str(), fields["aggregate_tables"].size());
        if (fields.count("materialization")) obj->materialization_json = copy_string(fields["materialization"].c_str(), fields["materialization"].size());
        if (fields.count("row_level_security")) obj->row_level_security_json = copy_string(fields["row_level_security"].c_str(), fields["row_level_security"].size());
        if (fields.count("column_masking")) obj->column_masking_json = copy_string(fields["column_masking"].c_str(), fields["column_masking"].size());
        if (fields.count("quality")) obj->quality_json = copy_string(fields["quality"].c_str(), fields["quality"].size());

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }

      case OpCode::OP_DEFINE_NORM_ANALYSIS:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* obj = new_norm_analysis();
        if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("scope")) obj->scope_json = copy_string(fields["scope"].c_str(), fields["scope"].size());
        if (fields.count("target_nf")) obj->target_nf = copy_string(fields["target_nf"].c_str(), fields["target_nf"].size());
        if (fields.count("fd_discovery")) obj->fd_discovery_json = copy_string(fields["fd_discovery"].c_str(), fields["fd_discovery"].size());
        if (fields.count("report")) obj->report_json = copy_string(fields["report"].c_str(), fields["report"].size());
        if (fields.count("governance")) obj->governance = copy_string(fields["governance"].c_str(), fields["governance"].size());
        if (fields.count("on_violation")) obj->on_violation = copy_string(fields["on_violation"].c_str(), fields["on_violation"].size());

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }

      case OpCode::OP_DEFINE_AMENDMENT_CONFIG:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* obj = new_amendment_config();
        if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("monitor")) obj->monitor_json = copy_string(fields["monitor"].c_str(), fields["monitor"].size());
        if (fields.count("change_types")) obj->change_types_json = copy_string(fields["change_types"].c_str(), fields["change_types"].size());
        if (fields.count("impact_scope")) obj->impact_scope_json = copy_string(fields["impact_scope"].c_str(), fields["impact_scope"].size());
        if (fields.count("approval")) obj->approval_json = copy_string(fields["approval"].c_str(), fields["approval"].size());
        if (fields.count("document")) obj->document_json = copy_string(fields["document"].c_str(), fields["document"].size());

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }

      case OpCode::OP_DEFINE_AMENDMENT:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* obj = new_amendment_obj();
        if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("model")) obj->model = copy_string(fields["model"].c_str(), fields["model"].size());
        if (fields.count("type")) obj->amendment_type = copy_string(fields["type"].c_str(), fields["type"].size());
        if (fields.count("description")) obj->description = copy_string(fields["description"].c_str(), fields["description"].size());
        if (fields.count("changes")) obj->changes_json = copy_string(fields["changes"].c_str(), fields["changes"].size());
        if (fields.count("auto_analyze")) obj->auto_analyze = (fields["auto_analyze"] == "true");
        if (fields.count("require_approval")) obj->require_approval = (fields["require_approval"] == "true");

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }

      case OpCode::OP_DEFINE_DATA_PROFILE:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* obj = new_data_profile();
        if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("sources")) obj->sources_json = copy_string(fields["sources"].c_str(), fields["sources"].size());
        if (fields.count("profiling")) obj->profiling_json = copy_string(fields["profiling"].c_str(), fields["profiling"].size());
        if (fields.count("output")) obj->output_json = copy_string(fields["output"].c_str(), fields["output"].size());

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }

      case OpCode::OP_DEFINE_MODELING_TOOL:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* obj = new_modeling_tool();
        if (fields.count("name")) obj->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("type")) obj->tool_type = copy_string(fields["type"].c_str(), fields["type"].size());
        if (fields.count("path")) obj->path = copy_string(fields["path"].c_str(), fields["path"].size());
        if (fields.count("api_url")) obj->api_url = copy_string(fields["api_url"].c_str(), fields["api_url"].size());
        if (fields.count("credentials")) obj->credentials = copy_string(fields["credentials"].c_str(), fields["credentials"].size());
        if (fields.count("repository")) obj->repository = copy_string(fields["repository"].c_str(), fields["repository"].size());
        if (fields.count("sync")) obj->sync_json = copy_string(fields["sync"].c_str(), fields["sync"].size());
        if (fields.count("mapping")) obj->mapping_json = copy_string(fields["mapping"].c_str(), fields["mapping"].size());
        if (fields.count("on_conflict")) obj->on_conflict_json = copy_string(fields["on_conflict"].c_str(), fields["on_conflict"].size());

        if (obj->name) {
          globals_.set(obj->name, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        }
        break;
      }

      case OpCode::OP_DEFINE_MODELING_AGENT:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* agent = new_modeling_agent();
        if (fields.count("name")) agent->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("provider")) agent->provider = copy_string(fields["provider"].c_str(), fields["provider"].size());
        if (fields.count("model")) agent->model = copy_string(fields["model"].c_str(), fields["model"].size());
        if (fields.count("endpoint")) agent->endpoint = copy_string(fields["endpoint"].c_str(), fields["endpoint"].size());
        if (fields.count("api_key_env")) agent->api_key_env = copy_string(fields["api_key_env"].c_str(), fields["api_key_env"].size());
        if (fields.count("budget")) agent->budget_ref = copy_string(fields["budget"].c_str(), fields["budget"].size());
        if (fields.count("catalog")) agent->catalog_ref = copy_string(fields["catalog"].c_str(), fields["catalog"].size());
        if (fields.count("governance")) agent->governance_ref = copy_string(fields["governance"].c_str(), fields["governance"].size());
        if (fields.count("enrich_from_governance")) agent->enrich_from_governance = (fields["enrich_from_governance"] == "true");

        // Comma-separated ref lists
        auto split = [](const std::string& s) {
          std::vector<std::string> v;
          std::istringstream iss(s); std::string item;
          while (std::getline(iss, item, ',')) v.push_back(item);
          return v;
        };
        auto split_to_objstr = [&](const std::string& s) {
          std::vector<ObjString*> v;
          for (auto& item : split(s)) {
            v.push_back(copy_string(item.c_str(), item.size()));
          }
          return v;
        };
        if (fields.count("sources")) agent->schema_source_refs = split_to_objstr(fields["sources"]);
        if (fields.count("modeling_tools")) agent->modeling_tool_refs = split_to_objstr(fields["modeling_tools"]);
        if (fields.count("coordinates_with")) agent->coordinates_with_refs = split_to_objstr(fields["coordinates_with"]);
        if (fields.count("capabilities")) {
          auto caps = fields["capabilities"];
          if (caps.find("reverse_engineer") != std::string::npos) agent->reverse_engineer_enabled = true;
          if (caps.find("normalization") != std::string::npos) agent->normalization_analysis_enabled = true;
          if (caps.find("dimensional") != std::string::npos) agent->dimensional_design_enabled = true;
          if (caps.find("amendment") != std::string::npos) agent->amendment_proposals_enabled = true;
          if (caps.find("profiling") != std::string::npos) agent->data_profiling_enabled = true;
        }

        // Register as global
        if (agent->name) {
          std::string agent_name(agent->name->chars, agent->name->length);
          globals_.set(agent->name, Value::ObjVal(reinterpret_cast<Obj*>(agent)));

          // Register AgentExtension for v0.8 compatibility
          AgentExtension extension;
          extension.agent_type = "modeling";
          agent_extensions_[agent_name] = std::move(extension);
        }
        break;
      }

      // ═══════════════════════════════════════════════════════════════
      // v0.9.6 Analyst Agent opcode handlers
      // ═══════════════════════════════════════════════════════════════

      case OpCode::OP_DEFINE_SQL_CONNECTION:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* conn = new_sql_connection();
        if (fields.count("name")) conn->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("platform")) conn->platform = copy_string(fields["platform"].c_str(), fields["platform"].size());
        if (fields.count("connection")) conn->connection = copy_string(fields["connection"].c_str(), fields["connection"].size());
        if (fields.count("credentials")) conn->credentials = copy_string(fields["credentials"].c_str(), fields["credentials"].size());
        if (fields.count("warehouse")) conn->warehouse = copy_string(fields["warehouse"].c_str(), fields["warehouse"].size());
        if (fields.count("database")) conn->database = copy_string(fields["database"].c_str(), fields["database"].size());
        if (fields.count("schema")) conn->schema = copy_string(fields["schema"].c_str(), fields["schema"].size());
        if (fields.count("project")) conn->project = copy_string(fields["project"].c_str(), fields["project"].size());
        if (fields.count("dataset")) conn->dataset = copy_string(fields["dataset"].c_str(), fields["dataset"].size());
        if (fields.count("catalog")) conn->catalog = copy_string(fields["catalog"].c_str(), fields["catalog"].size());
        if (fields.count("cluster")) conn->cluster = copy_string(fields["cluster"].c_str(), fields["cluster"].size());
        if (fields.count("timeout")) conn->timeout = std::stoi(fields["timeout"]);
        if (fields.count("max_rows")) conn->max_rows = std::stoi(fields["max_rows"]);
        if (fields.count("cost_limit")) conn->cost_limit = std::stod(fields["cost_limit"]);
        if (fields.count("queue")) conn->queue = copy_string(fields["queue"].c_str(), fields["queue"].size());
        if (fields.count("prefer_materialized_views")) conn->prefer_materialized_views = (fields["prefer_materialized_views"] != "false");
        if (fields.count("use_result_cache")) conn->use_result_cache = (fields["use_result_cache"] != "false");
        if (fields.count("partition_pruning")) conn->partition_pruning = (fields["partition_pruning"] != "false");
        if (fields.count("schema_source")) conn->schema_source = copy_string(fields["schema_source"].c_str(), fields["schema_source"].size());
        if (fields.count("semantic_layer")) conn->semantic_layer = copy_string(fields["semantic_layer"].c_str(), fields["semantic_layer"].size());

        if (conn->name) {
          globals_.set(conn->name, Value::ObjVal(reinterpret_cast<Obj*>(conn)));
        }
        break;
      }

      case OpCode::OP_DEFINE_DOMAIN_CONTEXT:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* ctx = new_domain_context();
        if (fields.count("name")) ctx->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("glossary")) ctx->glossary = copy_string(fields["glossary"].c_str(), fields["glossary"].size());
        if (fields.count("classification")) ctx->classification = copy_string(fields["classification"].c_str(), fields["classification"].size());
        if (fields.count("access_policy")) ctx->access_policy = copy_string(fields["access_policy"].c_str(), fields["access_policy"].size());
        if (fields.count("query_history")) ctx->query_history = (fields["query_history"] == "true");
        if (fields.count("feedback_loop")) ctx->feedback_loop = (fields["feedback_loop"] == "true");

        auto split_to_objstr = [&](const std::string& s) {
          std::vector<ObjString*> v;
          std::istringstream iss(s); std::string item;
          while (std::getline(iss, item, ',')) v.push_back(copy_string(item.c_str(), item.size()));
          return v;
        };
        if (fields.count("models")) ctx->model_refs = split_to_objstr(fields["models"]);
        if (fields.count("dimensional_models")) ctx->dimensional_model_refs = split_to_objstr(fields["dimensional_models"]);
        if (fields.count("marts")) ctx->mart_refs = split_to_objstr(fields["marts"]);
        if (fields.count("schema_sources")) ctx->schema_source_refs = split_to_objstr(fields["schema_sources"]);
        if (fields.count("data_products")) ctx->data_product_refs = split_to_objstr(fields["data_products"]);
        if (fields.count("semantic_layers")) ctx->semantic_layer_refs = split_to_objstr(fields["semantic_layers"]);

        if (ctx->name) {
          globals_.set(ctx->name, Value::ObjVal(reinterpret_cast<Obj*>(ctx)));
        }
        break;
      }

      case OpCode::OP_DEFINE_QUERY_TEMPLATE:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* tmpl = new_query_template();
        if (fields.count("name")) tmpl->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("description")) tmpl->description = copy_string(fields["description"].c_str(), fields["description"].size());
        if (fields.count("category")) tmpl->category = copy_string(fields["category"].c_str(), fields["category"].size());
        if (fields.count("params")) tmpl->params_json = copy_string(fields["params"].c_str(), fields["params"].size());
        if (fields.count("sql")) tmpl->sql = copy_string(fields["sql"].c_str(), fields["sql"].size());
        if (fields.count("default_format")) tmpl->default_format = copy_string(fields["default_format"].c_str(), fields["default_format"].size());
        if (fields.count("chart")) tmpl->chart_json = copy_string(fields["chart"].c_str(), fields["chart"].size());
        if (fields.count("classification")) tmpl->classification = copy_string(fields["classification"].c_str(), fields["classification"].size());
        if (fields.count("audit")) tmpl->audit = (fields["audit"] != "false");

        if (tmpl->name) {
          globals_.set(tmpl->name, Value::ObjVal(reinterpret_cast<Obj*>(tmpl)));
        }
        break;
      }

      case OpCode::OP_DEFINE_QUERY_OPTIMIZER:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* opt = new_query_optimizer();
        if (fields.count("name")) opt->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("cost_model")) opt->cost_model = copy_string(fields["cost_model"].c_str(), fields["cost_model"].size());
        if (fields.count("max_cost_per_query")) opt->max_cost_per_query = std::stod(fields["max_cost_per_query"]);
        if (fields.count("max_scan_gb")) opt->max_scan_gb = std::stod(fields["max_scan_gb"]);
        if (fields.count("max_execution_time")) opt->max_execution_time = std::stoi(fields["max_execution_time"]);
        if (fields.count("rules")) opt->rules_json = copy_string(fields["rules"].c_str(), fields["rules"].size());
        if (fields.count("explain_optimizations")) opt->explain_optimizations = (fields["explain_optimizations"] != "false");
        if (fields.count("show_cost_comparison")) opt->show_cost_comparison = (fields["show_cost_comparison"] != "false");

        if (opt->name) {
          globals_.set(opt->name, Value::ObjVal(reinterpret_cast<Obj*>(opt)));
        }
        break;
      }

      case OpCode::OP_DEFINE_EXECUTION_POLICY:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* pol = new_execution_policy();
        if (fields.count("name")) pol->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("max_rows")) pol->max_rows = std::stoi(fields["max_rows"]);
        if (fields.count("max_cost")) pol->max_cost = std::stod(fields["max_cost"]);
        if (fields.count("timeout")) pol->timeout = std::stoi(fields["timeout"]);
        if (fields.count("read_only")) pol->read_only = (fields["read_only"] != "false");
        if (fields.count("apply_masking")) pol->apply_masking = (fields["apply_masking"] != "false");
        if (fields.count("apply_row_level_security")) pol->apply_row_level_security = (fields["apply_row_level_security"] != "false");
        if (fields.count("audit_all_queries")) pol->audit_all_queries = (fields["audit_all_queries"] != "false");
        if (fields.count("retry_on_timeout")) pol->retry_on_timeout = (fields["retry_on_timeout"] == "true");
        if (fields.count("retry_with_smaller_warehouse")) pol->retry_with_smaller_warehouse = (fields["retry_with_smaller_warehouse"] == "true");
        if (fields.count("cache_results")) pol->cache_results = (fields["cache_results"] != "false");
        if (fields.count("cache_ttl")) pol->cache_ttl = copy_string(fields["cache_ttl"].c_str(), fields["cache_ttl"].size());
        if (fields.count("cache_key")) pol->cache_key = copy_string(fields["cache_key"].c_str(), fields["cache_key"].size());

        if (pol->name) {
          globals_.set(pol->name, Value::ObjVal(reinterpret_cast<Obj*>(pol)));
        }
        break;
      }

      case OpCode::OP_DEFINE_OUTPUT_FORMAT:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* fmt = new_output_format();
        if (fields.count("name")) fmt->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("type")) fmt->format_type = copy_string(fields["type"].c_str(), fields["type"].size());
        if (fields.count("excel")) fmt->excel_json = copy_string(fields["excel"].c_str(), fields["excel"].size());
        if (fields.count("pdf")) fmt->pdf_json = copy_string(fields["pdf"].c_str(), fields["pdf"].size());
        if (fields.count("html")) fmt->html_json = copy_string(fields["html"].c_str(), fields["html"].size());
        if (fields.count("csv")) fmt->csv_json = copy_string(fields["csv"].c_str(), fields["csv"].size());
        if (fields.count("json_config")) fmt->json_config_json = copy_string(fields["json_config"].c_str(), fields["json_config"].size());
        if (fields.count("slack")) fmt->slack_json = copy_string(fields["slack"].c_str(), fields["slack"].size());

        if (fmt->name) {
          globals_.set(fmt->name, Value::ObjVal(reinterpret_cast<Obj*>(fmt)));
        }
        break;
      }

      case OpCode::OP_DEFINE_QUERY_LIBRARY:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* lib = new_query_library();
        if (fields.count("name")) lib->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("storage")) lib->storage = copy_string(fields["storage"].c_str(), fields["storage"].size());
        if (fields.count("path")) lib->path = copy_string(fields["path"].c_str(), fields["path"].size());
        if (fields.count("tags")) lib->tags = (fields["tags"] == "true");
        if (fields.count("visibility")) lib->library_visibility = copy_string(fields["visibility"].c_str(), fields["visibility"].size());
        if (fields.count("approval_required")) lib->approval_required = (fields["approval_required"] == "true");
        if (fields.count("track_usage")) lib->track_usage = (fields["track_usage"] == "true");
        if (fields.count("track_performance")) lib->track_performance = (fields["track_performance"] == "true");
        if (fields.count("suggest_similar")) lib->suggest_similar = (fields["suggest_similar"] == "true");
        if (fields.count("auto_optimize")) lib->auto_optimize = (fields["auto_optimize"] == "true");

        auto split_to_objstr = [&](const std::string& s) {
          std::vector<ObjString*> v;
          std::istringstream iss(s); std::string item;
          while (std::getline(iss, item, ',')) v.push_back(copy_string(item.c_str(), item.size()));
          return v;
        };
        if (fields.count("categories")) lib->category_refs = split_to_objstr(fields["categories"]);

        if (lib->name) {
          globals_.set(lib->name, Value::ObjVal(reinterpret_cast<Obj*>(lib)));
        }
        break;
      }

      case OpCode::OP_DEFINE_ANALYSIS_SCHEDULE:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* sched = new_analysis_schedule();
        if (fields.count("name")) sched->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("query")) sched->query = copy_string(fields["query"].c_str(), fields["query"].size());
        if (fields.count("cron")) sched->cron = copy_string(fields["cron"].c_str(), fields["cron"].size());
        if (fields.count("connection")) sched->connection = copy_string(fields["connection"].c_str(), fields["connection"].size());
        if (fields.count("format")) sched->format = copy_string(fields["format"].c_str(), fields["format"].size());
        if (fields.count("output_path")) sched->output_path = copy_string(fields["output_path"].c_str(), fields["output_path"].size());
        if (fields.count("delivery")) sched->delivery_json = copy_string(fields["delivery"].c_str(), fields["delivery"].size());
        if (fields.count("audit")) sched->audit = (fields["audit"] != "false");
        if (fields.count("budget")) sched->budget = copy_string(fields["budget"].c_str(), fields["budget"].size());

        if (sched->name) {
          globals_.set(sched->name, Value::ObjVal(reinterpret_cast<Obj*>(sched)));
        }
        break;
      }

      case OpCode::OP_DEFINE_ANALYST_AGENT:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (std::size_t i = 0; i < field_count; ++i)
        {
          fields[to_std_string(stack_[base + i * 2])] = to_std_string(stack_[base + i * 2 + 1]);
        }
        stack_.resize(base);

        auto* agent = new_analyst_agent();
        if (fields.count("name")) agent->name = copy_string(fields["name"].c_str(), fields["name"].size());
        if (fields.count("provider")) agent->provider = copy_string(fields["provider"].c_str(), fields["provider"].size());
        if (fields.count("model")) agent->model = copy_string(fields["model"].c_str(), fields["model"].size());
        if (fields.count("endpoint")) agent->endpoint = copy_string(fields["endpoint"].c_str(), fields["endpoint"].size());
        if (fields.count("api_key_env")) agent->api_key_env = copy_string(fields["api_key_env"].c_str(), fields["api_key_env"].size());
        if (fields.count("budget")) agent->budget_ref = copy_string(fields["budget"].c_str(), fields["budget"].size());
        if (fields.count("domain_context")) agent->domain_context_ref = copy_string(fields["domain_context"].c_str(), fields["domain_context"].size());
        if (fields.count("glossary")) agent->glossary_ref = copy_string(fields["glossary"].c_str(), fields["glossary"].size());
        if (fields.count("governance")) agent->governance_ref = copy_string(fields["governance"].c_str(), fields["governance"].size());
        if (fields.count("classification")) agent->classification_ref = copy_string(fields["classification"].c_str(), fields["classification"].size());
        if (fields.count("access_policy")) agent->access_policy_ref = copy_string(fields["access_policy"].c_str(), fields["access_policy"].size());
        if (fields.count("optimizer")) agent->optimizer_ref = copy_string(fields["optimizer"].c_str(), fields["optimizer"].size());
        if (fields.count("execution_policy")) agent->execution_policy_ref = copy_string(fields["execution_policy"].c_str(), fields["execution_policy"].size());
        if (fields.count("query_library")) agent->query_library_ref = copy_string(fields["query_library"].c_str(), fields["query_library"].size());
        if (fields.count("default_output")) agent->default_output = copy_string(fields["default_output"].c_str(), fields["default_output"].size());
        if (fields.count("system")) agent->system_prompt = fields["system"];
        if (fields.count("temperature")) agent->temperature = std::stod(fields["temperature"]);

        auto split = [](const std::string& s) {
          std::vector<std::string> v;
          std::istringstream iss(s); std::string item;
          while (std::getline(iss, item, ',')) v.push_back(item);
          return v;
        };
        auto split_to_objstr = [&](const std::string& s) {
          std::vector<ObjString*> v;
          for (auto& item : split(s)) {
            v.push_back(copy_string(item.c_str(), item.size()));
          }
          return v;
        };
        if (fields.count("connections")) agent->connection_refs = split_to_objstr(fields["connections"]);
        if (fields.count("models")) agent->model_refs = split_to_objstr(fields["models"]);
        if (fields.count("dimensional_models")) agent->dimensional_model_refs = split_to_objstr(fields["dimensional_models"]);
        if (fields.count("marts")) agent->mart_refs = split_to_objstr(fields["marts"]);
        if (fields.count("semantic_layers")) agent->semantic_layer_refs = split_to_objstr(fields["semantic_layers"]);
        if (fields.count("output_formats")) agent->output_format_refs = split_to_objstr(fields["output_formats"]);
        if (fields.count("skills")) agent->skill_refs = split_to_objstr(fields["skills"]);
        if (fields.count("extern_skills")) agent->extern_skill_refs = split_to_objstr(fields["extern_skills"]);
        if (fields.count("coordinates_with")) agent->coordinates_with_refs = split_to_objstr(fields["coordinates_with"]);
        if (fields.count("handoffs")) agent->handoff_refs = split_to_objstr(fields["handoffs"]);

        // Register as global
        if (agent->name) {
          std::string agent_name(agent->name->chars, agent->name->length);
          globals_.set(agent->name, Value::ObjVal(reinterpret_cast<Obj*>(agent)));

          // Register AgentExtension for v0.8 compatibility
          AgentExtension extension;
          extension.agent_type = "analyst";
          agent_extensions_[agent_name] = std::move(extension);
        }
        break;
      }

      case OpCode::OP_ANALYST_QUERY:
      case OpCode::OP_ANALYST_EXECUTE:
      case OpCode::OP_ANALYST_FORMAT:
      case OpCode::OP_ANALYST_OPTIMIZE:
      {
        // Action opcodes — stub implementations for now
        // These will be used by native functions at runtime
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        stack_.resize(base);
        stack_.push_back(Value::Nil());
        break;
      }

      // v0.9.7: Data Pipeline Deployment opcodes
      case OpCode::OP_DEFINE_DEPLOY_TARGET:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* target = new_deploy_target();
        target->name = fields["name"];
        if (fields.count("environment")) target->environment = fields["environment"];
        if (fields.count("connection")) target->connection = fields["connection"];
        if (fields.count("namespace")) target->namespace_name = fields["namespace"];
        if (fields.count("region")) target->region = fields["region"];
        if (fields.count("tags"))
        {
          try { target->tags = nlohmann::json::parse(fields["tags"]); } catch (...) {}
        }
        if (fields.count("variables"))
        {
          try { target->variables = nlohmann::json::parse(fields["variables"]); } catch (...) {}
        }
        if (fields.count("frozen")) target->frozen = fields["frozen"] == "true";
        if (fields.count("freeze_reason")) target->freeze_reason = fields["freeze_reason"];

        auto* name_str = copy_string(target->name.c_str(), target->name.size());
        globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(target)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(target)));
        break;
      }

      case OpCode::OP_DEFINE_PROMOTION_RULE:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* rule = new_promotion_rule();
        rule->name = fields["name"];
        if (fields.count("from_env")) rule->from_env = fields["from_env"];
        if (fields.count("to_env")) rule->to_env = fields["to_env"];
        if (fields.count("require_tests")) rule->require_tests = fields["require_tests"] == "true";
        if (fields.count("require_approval")) rule->require_approval = fields["require_approval"] == "true";
        if (fields.count("approvers"))
        {
          std::string csv = fields["approvers"];
          std::stringstream ss(csv);
          std::string item;
          while (std::getline(ss, item, ','))
          {
            if (!item.empty()) rule->approvers.push_back(item);
          }
        }
        if (fields.count("auto_promote")) rule->auto_promote = fields["auto_promote"] == "true";
        if (fields.count("cooldown")) rule->cooldown = fields["cooldown"];
        if (fields.count("gate_checks"))
        {
          try { rule->gate_checks = nlohmann::json::parse(fields["gate_checks"]); } catch (...) {}
        }

        auto* rule_name_str = copy_string(rule->name.c_str(), rule->name.size());
        globals_.set(rule_name_str, Value::ObjVal(reinterpret_cast<Obj*>(rule)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(rule)));
        break;
      }

      case OpCode::OP_DEFINE_ROLLBACK_POLICY:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* policy = new_rollback_policy();
        policy->name = fields["name"];
        if (fields.count("strategy")) policy->strategy = fields["strategy"];
        if (fields.count("keep_data")) policy->keep_data = fields["keep_data"] == "true";
        if (fields.count("notify_dataops")) policy->notify_dataops = fields["notify_dataops"] == "true";
        if (fields.count("max_rollback_window")) policy->max_rollback_window = fields["max_rollback_window"];
        if (fields.count("reconciliation")) policy->reconciliation = fields["reconciliation"] == "true";
        if (fields.count("pre_rollback_checks"))
        {
          try { policy->pre_rollback_checks = nlohmann::json::parse(fields["pre_rollback_checks"]); } catch (...) {}
        }
        if (fields.count("notifications"))
        {
          try { policy->notifications = nlohmann::json::parse(fields["notifications"]); } catch (...) {}
        }

        auto* policy_name_str = copy_string(policy->name.c_str(), policy->name.size());
        globals_.set(policy_name_str, Value::ObjVal(reinterpret_cast<Obj*>(policy)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(policy)));
        break;
      }

      case OpCode::OP_DEFINE_ARTIFACT_REGISTRY:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* registry = new_artifact_registry();
        registry->name = fields["name"];
        if (fields.count("storage")) registry->storage = fields["storage"];
        if (fields.count("path")) registry->path = fields["path"];
        if (fields.count("versioning")) registry->versioning = fields["versioning"];
        if (fields.count("retention")) registry->retention = fields["retention"];
        if (fields.count("sign_artifacts")) registry->sign_artifacts = fields["sign_artifacts"] == "true";
        if (fields.count("checksum")) registry->checksum = fields["checksum"];
        if (fields.count("immutable")) registry->immutable = fields["immutable"] == "true";

        auto* reg_name_str = copy_string(registry->name.c_str(), registry->name.size());
        globals_.set(reg_name_str, Value::ObjVal(reinterpret_cast<Obj*>(registry)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(registry)));
        break;
      }

      case OpCode::OP_DEFINE_DEPLOY_CONFIG:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* config = new_deploy_config();
        config->name = fields["name"];
        if (fields.count("target")) config->target = fields["target"];
        if (fields.count("strategy")) config->strategy = fields["strategy"];
        if (fields.count("approval_gate")) config->approval_gate = fields["approval_gate"] == "true";
        if (fields.count("pipeline_ref")) config->pipeline_ref = fields["pipeline_ref"];
        if (fields.count("pre_deploy_checks"))
        {
          try { config->pre_deploy_checks = nlohmann::json::parse(fields["pre_deploy_checks"]); } catch (...) {}
        }
        if (fields.count("post_deploy_checks"))
        {
          try { config->post_deploy_checks = nlohmann::json::parse(fields["post_deploy_checks"]); } catch (...) {}
        }
        if (fields.count("notifications"))
        {
          try { config->notifications = nlohmann::json::parse(fields["notifications"]); } catch (...) {}
        }
        if (fields.count("schedule")) config->schedule = fields["schedule"];
        if (fields.count("auto_rollback")) config->auto_rollback = fields["auto_rollback"] == "true";
        if (fields.count("rollback_policy")) config->rollback_policy = fields["rollback_policy"];
        if (fields.count("artifact_registry")) config->artifact_registry = fields["artifact_registry"];

        auto* config_name_str = copy_string(config->name.c_str(), config->name.size());
        globals_.set(config_name_str, Value::ObjVal(reinterpret_cast<Obj*>(config)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(config)));
        break;
      }

      case OpCode::OP_DEPLOY_EXECUTE:
      case OpCode::OP_DEPLOY_ROLLBACK:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        stack_.resize(base);
        stack_.push_back(Value::Nil());
        break;
      }

      // ═══════════════════════════════════════════════════════════════
      // v0.9.8: Data Scientist Agent opcodes
      // ═══════════════════════════════════════════════════════════════

      // --- 6 key DEFINE opcodes with full field mapping ---

      case OpCode::OP_DEFINE_PROBLEM_STATEMENT:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* obj = new_problem_statement();
        obj->name = fields["name"];
        if (fields.count("statement")) obj->statement = fields["statement"];
        if (fields.count("business_context"))
        {
          try { obj->business_context_json = fields["business_context"]; } catch (...) {}
        }
        if (fields.count("constraints"))
        {
          try { obj->constraints_json = fields["constraints"]; } catch (...) {}
        }
        if (fields.count("deliverables"))
        {
          try { obj->deliverables_json = fields["deliverables"]; } catch (...) {}
        }

        auto* name_str = copy_string(obj->name.c_str(), obj->name.size());
        globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        break;
      }

      case OpCode::OP_DEFINE_ML_EXPERIMENT:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* obj = new_ml_experiment();
        obj->name = fields["name"];
        if (fields.count("problem_type")) obj->problem_type = fields["problem_type"];
        if (fields.count("target")) obj->target = fields["target"];
        if (fields.count("positive_class")) obj->positive_class = fields["positive_class"];
        if (fields.count("dataset")) obj->dataset = fields["dataset"];
        if (fields.count("train_test_split")) obj->train_test_split = std::stod(fields["train_test_split"]);
        if (fields.count("stratify")) obj->stratify = fields["stratify"] == "true";
        if (fields.count("cross_validation"))
        {
          try { obj->cross_validation_json = fields["cross_validation"]; } catch (...) {}
        }
        if (fields.count("algorithms")) obj->algorithms = fields["algorithms"];
        if (fields.count("metrics"))
        {
          try { obj->metrics_json = fields["metrics"]; } catch (...) {}
        }
        if (fields.count("interpretability")) obj->interpretability = fields["interpretability"];
        if (fields.count("budget")) obj->budget_ref = fields["budget"];

        auto* name_str = copy_string(obj->name.c_str(), obj->name.size());
        globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        break;
      }

      case OpCode::OP_DEFINE_EDA_CONFIG:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* obj = new_eda_config();
        obj->name = fields["name"];
        if (fields.count("structural"))
        {
          try { obj->structural_json = fields["structural"]; } catch (...) {}
        }
        if (fields.count("univariate"))
        {
          try { obj->univariate_json = fields["univariate"]; } catch (...) {}
        }
        if (fields.count("bivariate"))
        {
          try { obj->bivariate_json = fields["bivariate"]; } catch (...) {}
        }
        if (fields.count("multivariate"))
        {
          try { obj->multivariate_json = fields["multivariate"]; } catch (...) {}
        }
        if (fields.count("temporal"))
        {
          try { obj->temporal_json = fields["temporal"]; } catch (...) {}
        }
        if (fields.count("performance"))
        {
          try { obj->performance_json = fields["performance"]; } catch (...) {}
        }
        if (fields.count("output"))
        {
          try { obj->output_json = fields["output"]; } catch (...) {}
        }

        auto* name_str = copy_string(obj->name.c_str(), obj->name.size());
        globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        break;
      }

      case OpCode::OP_DEFINE_VOLUME_ROUTER:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* obj = new_volume_router();
        obj->name = fields["name"];
        if (fields.count("volume_probe"))
        {
          try { obj->volume_probe_json = fields["volume_probe"]; } catch (...) {}
        }
        if (fields.count("routing_rules"))
        {
          try { obj->routing_rules_json = fields["routing_rules"]; } catch (...) {}
        }
        if (fields.count("auto_escalate"))
        {
          try { obj->auto_escalate_json = fields["auto_escalate"]; } catch (...) {}
        }
        if (fields.count("sampling_strategy"))
        {
          try { obj->sampling_strategy_json = fields["sampling_strategy"]; } catch (...) {}
        }

        auto* name_str = copy_string(obj->name.c_str(), obj->name.size());
        globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        break;
      }

      case OpCode::OP_DEFINE_CODE_INTERPRETER:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* obj = new_code_interpreter();
        obj->name = fields["name"];
        if (fields.count("runtime")) obj->runtime = fields["runtime"];
        if (fields.count("version")) obj->version = fields["version"];
        if (fields.count("venv_manager")) obj->venv_manager_ref = fields["venv_manager"];
        if (fields.count("profiles"))
        {
          try { obj->profiles_json = fields["profiles"]; } catch (...) {}
        }
        if (fields.count("profile_selection")) obj->profile_selection = fields["profile_selection"];
        if (fields.count("sandbox"))
        {
          try { obj->sandbox_json = fields["sandbox"]; } catch (...) {}
        }
        if (fields.count("auto_test"))
        {
          try { obj->auto_test_json = fields["auto_test"]; } catch (...) {}
        }
        if (fields.count("data_bridge"))
        {
          try { obj->data_bridge_json = fields["data_bridge"]; } catch (...) {}
        }

        auto* name_str = copy_string(obj->name.c_str(), obj->name.size());
        globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        break;
      }

      case OpCode::OP_DEFINE_DATASCIENTIST_AGENT:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* obj = new_datascientist_agent();
        obj->name = fields["name"];
        // LLM base
        if (fields.count("provider")) obj->provider = fields["provider"];
        if (fields.count("model")) obj->llm_model = fields["model"];
        if (fields.count("system")) obj->system_prompt = fields["system"];
        if (fields.count("temperature")) obj->temperature = std::stod(fields["temperature"]);
        if (fields.count("endpoint")) obj->endpoint = fields["endpoint"];
        if (fields.count("api_key_env")) obj->api_key_env = fields["api_key_env"];
        // Agent.MD
        if (fields.count("agent_md")) obj->agent_md_path = fields["agent_md"];
        // Problem
        if (fields.count("problem")) obj->problem_ref = fields["problem"];
        if (fields.count("problem_types")) obj->problem_types = fields["problem_types"];
        // Sub-agents & forge
        if (fields.count("sub_agents"))
        {
          try { obj->sub_agents_json = fields["sub_agents"]; } catch (...) {}
        }
        if (fields.count("forge")) obj->forge_ref = fields["forge"];
        // Data
        if (fields.count("data_sources"))
        {
          try { obj->data_sources_json = fields["data_sources"]; } catch (...) {}
        }
        // Component refs
        if (fields.count("eda_config")) obj->eda_config_ref = fields["eda_config"];
        if (fields.count("feature_config")) obj->feature_config_ref = fields["feature_config"];
        if (fields.count("experiment")) obj->experiment_ref = fields["experiment"];
        if (fields.count("automl")) obj->automl_ref = fields["automl"];
        if (fields.count("ensemble")) obj->ensemble_ref = fields["ensemble"];
        if (fields.count("hypotheses")) obj->hypotheses_refs = fields["hypotheses"];
        if (fields.count("evaluation")) obj->evaluation_ref = fields["evaluation"];
        if (fields.count("explainability")) obj->explainability_ref = fields["explainability"];
        if (fields.count("code_interpreter")) obj->code_interpreter_ref = fields["code_interpreter"];
        if (fields.count("model_registry")) obj->model_registry_ref = fields["model_registry"];
        if (fields.count("churn")) obj->churn_ref = fields["churn"];
        if (fields.count("clv")) obj->clv_ref = fields["clv"];
        if (fields.count("propensity")) obj->propensity_ref = fields["propensity"];
        if (fields.count("recommendation")) obj->recommendation_ref = fields["recommendation"];
        if (fields.count("experiment_engine")) obj->experiment_engine_ref = fields["experiment_engine"];
        if (fields.count("decision_framework")) obj->decision_framework_ref = fields["decision_framework"];
        if (fields.count("volume_router")) obj->volume_router_ref = fields["volume_router"];
        if (fields.count("distributed_compute")) obj->distributed_compute_ref = fields["distributed_compute"];
        if (fields.count("performance")) obj->performance_ref = fields["performance"];
        if (fields.count("data_quality")) obj->data_quality_ref = fields["data_quality"];
        if (fields.count("self_correction")) obj->self_correction_ref = fields["self_correction"];
        if (fields.count("self_assessment")) obj->self_assessment_ref = fields["self_assessment"];
        if (fields.count("adaptive_knowledge")) obj->adaptive_knowledge_ref = fields["adaptive_knowledge"];
        if (fields.count("deployment")) obj->deployment_ref = fields["deployment"];
        // Coordination
        if (fields.count("coordinates_with")) obj->coordinates_with = fields["coordinates_with"];
        if (fields.count("handoffs")) obj->handoffs = fields["handoffs"];
        // Identity
        if (fields.count("role")) obj->role = fields["role"];
        if (fields.count("purpose")) obj->purpose = fields["purpose"];
        if (fields.count("autonomy")) obj->autonomy = fields["autonomy"];
        // Budget
        if (fields.count("budget")) obj->budget_ref = fields["budget"];

        obj->status = "initialized";

        auto* name_str = copy_string(obj->name.c_str(), obj->name.size());
        globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        break;
      }

      // --- 29 remaining DEFINE opcodes: generic field-reading handlers ---

      case OpCode::OP_DEFINE_HYPOTHESIS_TEST:
      case OpCode::OP_DEFINE_FEATURE_ENGINEERING:
      case OpCode::OP_DEFINE_AUTOML_CONFIG:
      case OpCode::OP_DEFINE_HYPERPARAMETER_CONFIG:
      case OpCode::OP_DEFINE_STACKED_MODEL:
      case OpCode::OP_DEFINE_EVALUATION_CONFIG:
      case OpCode::OP_DEFINE_DS_MODEL_REGISTRY:
      case OpCode::OP_DEFINE_EXPLAINABILITY_CONFIG:
      case OpCode::OP_DEFINE_VENV_MANAGER:
      case OpCode::OP_DEFINE_NLP_PIPELINE:
      case OpCode::OP_DEFINE_CHURN_ANALYSIS:
      case OpCode::OP_DEFINE_CLV_MODEL:
      case OpCode::OP_DEFINE_PROPENSITY_MODEL:
      case OpCode::OP_DEFINE_RECOMMENDATION_ENGINE:
      case OpCode::OP_DEFINE_EXPERIMENT_DESIGN:
      case OpCode::OP_DEFINE_SCENARIO_ANALYSIS:
      case OpCode::OP_DEFINE_DECISION_SUPPORT:
      case OpCode::OP_DEFINE_EDA_TECHNIQUE_SELECTOR:
      case OpCode::OP_DEFINE_SMART_CONNECTOR:
      case OpCode::OP_DEFINE_COMPUTE_CONNECTOR:
      case OpCode::OP_DEFINE_FILE_CONNECTOR:
      case OpCode::OP_DEFINE_DISTRIBUTED_COMPUTE_CONFIG:
      case OpCode::OP_DEFINE_PERFORMANCE_CONFIG:
      case OpCode::OP_DEFINE_DATA_QUALITY_PIPELINE:
      case OpCode::OP_DEFINE_SELF_CORRECTION_CONFIG:
      case OpCode::OP_DEFINE_SELF_ASSESSMENT:
      case OpCode::OP_DEFINE_ADAPTIVE_KNOWLEDGE_CONFIG:
      case OpCode::OP_DEFINE_ANALYSIS_HISTORY:
      case OpCode::OP_DEFINE_OBSERVABILITY_CONFIG:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        // Create the appropriate runtime object based on opcode
        Obj* runtime_obj = nullptr;
        std::string obj_name;

        switch (op)
        {
          case OpCode::OP_DEFINE_HYPOTHESIS_TEST:
          {
            auto* o = new_hypothesis_test();
            o->name = fields["name"];
            if (fields.count("null_hypothesis")) o->null_hypothesis = fields["null_hypothesis"];
            if (fields.count("alternative")) o->alternative = fields["alternative"];
            if (fields.count("test_type")) o->test_type = fields["test_type"];
            if (fields.count("significance_level")) o->significance_level = std::stod(fields["significance_level"]);
            if (fields.count("power")) o->power = std::stod(fields["power"]);
            if (fields.count("effect_size")) o->effect_size = fields["effect_size"];
            if (fields.count("data_source")) o->data_source = fields["data_source"];
            if (fields.count("group_a")) o->group_a = fields["group_a"];
            if (fields.count("group_b")) o->group_b = fields["group_b"];
            if (fields.count("assumptions")) o->assumptions_json = fields["assumptions"];
            if (fields.count("if_significant")) o->if_significant_json = fields["if_significant"];
            if (fields.count("if_not_significant")) o->if_not_significant_json = fields["if_not_significant"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_FEATURE_ENGINEERING:
          {
            auto* o = new_feature_engineering();
            o->name = fields["name"];
            if (fields.count("source_tables")) o->source_tables_json = fields["source_tables"];
            if (fields.count("strategies")) o->strategies_json = fields["strategies"];
            if (fields.count("selection")) o->selection_json = fields["selection"];
            if (fields.count("output")) o->output_json = fields["output"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_AUTOML_CONFIG:
          {
            auto* o = new_automl_config();
            o->name = fields["name"];
            if (fields.count("algorithms")) o->algorithms = fields["algorithms"];
            if (fields.count("preprocessing_search")) o->preprocessing_search_json = fields["preprocessing_search"];
            if (fields.count("optimization")) o->optimization_json = fields["optimization"];
            if (fields.count("cv_folds")) o->cv_folds = std::stoi(fields["cv_folds"]);
            if (fields.count("primary_metric")) o->primary_metric = fields["primary_metric"];
            if (fields.count("holdout_validation")) o->holdout_validation = fields["holdout_validation"] == "true";
            if (fields.count("selection_criteria")) o->selection_criteria_json = fields["selection_criteria"];
            if (fields.count("leaderboard")) o->leaderboard = fields["leaderboard"] == "true";
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_HYPERPARAMETER_CONFIG:
          {
            auto* o = new_hyperparameter_config();
            o->name = fields["name"];
            if (fields.count("algorithm")) o->algorithm = fields["algorithm"];
            if (fields.count("search_space")) o->search_space_json = fields["search_space"];
            if (fields.count("optimizer")) o->optimizer_json = fields["optimizer"];
            if (fields.count("early_stopping")) o->early_stopping_json = fields["early_stopping"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_STACKED_MODEL:
          {
            auto* o = new_stacked_model();
            o->name = fields["name"];
            if (fields.count("base_learners")) o->base_learners_json = fields["base_learners"];
            if (fields.count("meta_learner")) o->meta_learner_json = fields["meta_learner"];
            if (fields.count("strategy")) o->strategy_json = fields["strategy"];
            if (fields.count("compare_against")) o->compare_against = fields["compare_against"];
            if (fields.count("improvement_threshold")) o->improvement_threshold = std::stod(fields["improvement_threshold"]);
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_EVALUATION_CONFIG:
          {
            auto* o = new_evaluation_config();
            o->name = fields["name"];
            if (fields.count("classification")) o->classification_json = fields["classification"];
            if (fields.count("regression")) o->regression_json = fields["regression"];
            if (fields.count("clustering")) o->clustering_json = fields["clustering"];
            if (fields.count("business")) o->business_json = fields["business"];
            if (fields.count("cv_strategy")) o->cv_strategy = fields["cv_strategy"];
            if (fields.count("outer_folds")) o->outer_folds = std::stoi(fields["outer_folds"]);
            if (fields.count("inner_folds")) o->inner_folds = std::stoi(fields["inner_folds"]);
            if (fields.count("model_comparison")) o->model_comparison_json = fields["model_comparison"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_DS_MODEL_REGISTRY:
          {
            auto* o = new_ds_model_registry();
            o->name = fields["name"];
            if (fields.count("storage")) o->storage_json = fields["storage"];
            if (fields.count("tracking")) o->tracking_json = fields["tracking"];
            if (fields.count("model_card")) o->model_card_json = fields["model_card"];
            if (fields.count("lifecycle")) o->lifecycle_json = fields["lifecycle"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_EXPLAINABILITY_CONFIG:
          {
            auto* o = new_explainability_config();
            o->name = fields["name"];
            if (fields.count("global")) o->global_json = fields["global"];
            if (fields.count("local")) o->local_json = fields["local"];
            if (fields.count("fairness")) o->fairness_json = fields["fairness"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_VENV_MANAGER:
          {
            auto* o = new_venv_manager();
            o->name = fields["name"];
            if (fields.count("lifecycle")) o->lifecycle_json = fields["lifecycle"];
            if (fields.count("pool")) o->pool_json = fields["pool"];
            if (fields.count("dependency_resolver")) o->dependency_resolver_json = fields["dependency_resolver"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_NLP_PIPELINE:
          {
            auto* o = new_nlp_pipeline();
            o->name = fields["name"];
            if (fields.count("preprocessing")) o->preprocessing_json = fields["preprocessing"];
            if (fields.count("tasks")) o->tasks_json = fields["tasks"];
            if (fields.count("embedding_model")) o->embedding_model = fields["embedding_model"];
            if (fields.count("vector_store")) o->vector_store = fields["vector_store"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_CHURN_ANALYSIS:
          {
            auto* o = new_churn_analysis();
            o->name = fields["name"];
            if (fields.count("churn_definition")) o->churn_definition_json = fields["churn_definition"];
            if (fields.count("features")) o->features_json = fields["features"];
            if (fields.count("primary_model")) o->primary_model = fields["primary_model"];
            if (fields.count("calibration")) o->calibration = fields["calibration"];
            if (fields.count("threshold_optimization")) o->threshold_optimization = fields["threshold_optimization"];
            if (fields.count("outputs")) o->outputs_json = fields["outputs"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_CLV_MODEL:
          {
            auto* o = new_clv_model();
            o->name = fields["name"];
            if (fields.count("model_type")) o->model_type = fields["model_type"];
            if (fields.count("frequency")) o->frequency = fields["frequency"];
            if (fields.count("recency")) o->recency = fields["recency"];
            if (fields.count("monetary")) o->monetary = fields["monetary"];
            if (fields.count("T")) o->T = fields["T"];
            if (fields.count("prediction_periods")) o->prediction_periods_json = fields["prediction_periods"];
            if (fields.count("discount_rate")) o->discount_rate = std::stod(fields["discount_rate"]);
            if (fields.count("segments")) o->segments_json = fields["segments"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_PROPENSITY_MODEL:
          {
            auto* o = new_propensity_model();
            o->name = fields["name"];
            if (fields.count("target_action")) o->target_action = fields["target_action"];
            if (fields.count("training_window")) o->training_window = fields["training_window"];
            if (fields.count("features")) o->features_json = fields["features"];
            if (fields.count("algorithm")) o->algorithm = fields["algorithm"];
            if (fields.count("calibration_method")) o->calibration_method = fields["calibration_method"];
            if (fields.count("score_output")) o->score_output_json = fields["score_output"];
            if (fields.count("actions")) o->actions_json = fields["actions"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_RECOMMENDATION_ENGINE:
          {
            auto* o = new_recommendation_engine();
            o->name = fields["name"];
            if (fields.count("strategy")) o->strategy = fields["strategy"];
            if (fields.count("collaborative")) o->collaborative_json = fields["collaborative"];
            if (fields.count("content_based")) o->content_based_json = fields["content_based"];
            if (fields.count("blending")) o->blending_json = fields["blending"];
            if (fields.count("rules")) o->rules_json = fields["rules"];
            if (fields.count("metrics")) o->metrics = fields["metrics"];
            if (fields.count("serving")) o->serving_json = fields["serving"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_EXPERIMENT_DESIGN:
          {
            auto* o = new_experiment_design();
            o->name = fields["name"];
            if (fields.count("experiment_type")) o->experiment_type = fields["experiment_type"];
            if (fields.count("control")) o->control_json = fields["control"];
            if (fields.count("treatments")) o->treatments_json = fields["treatments"];
            if (fields.count("unit")) o->unit = fields["unit"];
            if (fields.count("stratify_by")) o->stratify_by = fields["stratify_by"];
            if (fields.count("power_analysis")) o->power_analysis_json = fields["power_analysis"];
            if (fields.count("primary_metric")) o->primary_metric_json = fields["primary_metric"];
            if (fields.count("guardrails")) o->guardrails_json = fields["guardrails"];
            if (fields.count("analysis")) o->analysis_json = fields["analysis"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_SCENARIO_ANALYSIS:
          {
            auto* o = new_scenario_analysis();
            o->name = fields["name"];
            if (fields.count("base_model")) o->base_model = fields["base_model"];
            if (fields.count("scenarios")) o->scenarios_json = fields["scenarios"];
            if (fields.count("simulation")) o->simulation_json = fields["simulation"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_DECISION_SUPPORT:
          {
            auto* o = new_decision_support();
            o->name = fields["name"];
            if (fields.count("deliverables")) o->deliverables_json = fields["deliverables"];
            if (fields.count("confidence")) o->confidence_json = fields["confidence"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_EDA_TECHNIQUE_SELECTOR:
          {
            auto* o = new_eda_technique_selector();
            o->name = fields["name"];
            if (fields.count("rules")) o->rules_json = fields["rules"];
            if (fields.count("output")) o->output_json = fields["output"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_SMART_CONNECTOR:
          {
            auto* o = new_smart_connector();
            o->name = fields["name"];
            if (fields.count("discovery")) o->discovery_json = fields["discovery"];
            if (fields.count("metadata_cache")) o->metadata_cache_json = fields["metadata_cache"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_COMPUTE_CONNECTOR:
          {
            auto* o = new_compute_connector();
            o->name = fields["name"];
            if (fields.count("engine")) o->engine = fields["engine"];
            if (fields.count("connection")) o->connection = fields["connection"];
            if (fields.count("token")) o->token = fields["token"];
            if (fields.count("cluster_config")) o->cluster_config_json = fields["cluster_config"];
            if (fields.count("idle_timeout")) o->idle_timeout = fields["idle_timeout"];
            if (fields.count("cost_tracking")) o->cost_tracking = fields["cost_tracking"] == "true";
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_FILE_CONNECTOR:
          {
            auto* o = new_file_connector();
            o->name = fields["name"];
            if (fields.count("base_path")) o->base_path = fields["base_path"];
            if (fields.count("auto_detect_schema")) o->auto_detect_schema = fields["auto_detect_schema"] == "true";
            if (fields.count("auto_detect_delimiter")) o->auto_detect_delimiter = fields["auto_detect_delimiter"] == "true";
            if (fields.count("auto_detect_encoding")) o->auto_detect_encoding = fields["auto_detect_encoding"] == "true";
            if (fields.count("supported_formats")) o->supported_formats_json = fields["supported_formats"];
            if (fields.count("large_file_strategy")) o->large_file_strategy_json = fields["large_file_strategy"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_DISTRIBUTED_COMPUTE_CONFIG:
          {
            auto* o = new_distributed_compute_config();
            o->name = fields["name"];
            if (fields.count("spark")) o->spark_json = fields["spark"];
            if (fields.count("databricks")) o->databricks_json = fields["databricks"];
            if (fields.count("snowflake")) o->snowflake_json = fields["snowflake"];
            if (fields.count("hadoop")) o->hadoop_json = fields["hadoop"];
            if (fields.count("gpu")) o->gpu_json = fields["gpu"];
            if (fields.count("selection_logic")) o->selection_logic_json = fields["selection_logic"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_PERFORMANCE_CONFIG:
          {
            auto* o = new_performance_config();
            o->name = fields["name"];
            if (fields.count("phase_slas")) o->phase_slas_json = fields["phase_slas"];
            if (fields.count("cache")) o->cache_json = fields["cache"];
            if (fields.count("parallelism")) o->parallelism_json = fields["parallelism"];
            if (fields.count("lazy_eval")) o->lazy_eval_json = fields["lazy_eval"];
            if (fields.count("memory")) o->memory_json = fields["memory"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_DATA_QUALITY_PIPELINE:
          {
            auto* o = new_data_quality_pipeline();
            o->name = fields["name"];
            if (fields.count("profiling")) o->profiling_json = fields["profiling"];
            if (fields.count("scoring")) o->scoring_json = fields["scoring"];
            if (fields.count("remediation")) o->remediation_json = fields["remediation"];
            if (fields.count("governance")) o->governance_ref = fields["governance"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_SELF_CORRECTION_CONFIG:
          {
            auto* o = new_self_correction_config();
            o->name = fields["name"];
            if (fields.count("code_errors")) o->code_errors_json = fields["code_errors"];
            if (fields.count("statistical_errors")) o->statistical_errors_json = fields["statistical_errors"];
            if (fields.count("model_errors")) o->model_errors_json = fields["model_errors"];
            if (fields.count("reasoning_errors")) o->reasoning_errors_json = fields["reasoning_errors"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_SELF_ASSESSMENT:
          {
            auto* o = new_self_assessment();
            o->name = fields["name"];
            if (fields.count("planning")) o->planning_json = fields["planning"];
            if (fields.count("execution")) o->execution_json = fields["execution"];
            if (fields.count("interpretation")) o->interpretation_json = fields["interpretation"];
            if (fields.count("communication")) o->communication_json = fields["communication"];
            if (fields.count("gate")) o->gate_json = fields["gate"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_ADAPTIVE_KNOWLEDGE_CONFIG:
          {
            auto* o = new_adaptive_knowledge_config();
            o->name = fields["name"];
            if (fields.count("knowledge_sources")) o->knowledge_sources_json = fields["knowledge_sources"];
            if (fields.count("adaptation")) o->adaptation_json = fields["adaptation"];
            if (fields.count("learning")) o->learning_json = fields["learning"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_ANALYSIS_HISTORY:
          {
            auto* o = new_analysis_history();
            o->name = fields["name"];
            if (fields.count("knowledge_base")) o->knowledge_base = fields["knowledge_base"];
            if (fields.count("vector_store")) o->vector_store = fields["vector_store"];
            if (fields.count("embedding_model")) o->embedding_model = fields["embedding_model"];
            if (fields.count("retrieval_strategy")) o->retrieval_strategy = fields["retrieval_strategy"];
            if (fields.count("record_fields")) o->record_fields_json = fields["record_fields"];
            if (fields.count("retention")) o->retention = fields["retention"];
            if (fields.count("max_records")) o->max_records = std::stoi(fields["max_records"]);
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_OBSERVABILITY_CONFIG:
          {
            auto* o = new_observability_config();
            o->name = fields["name"];
            if (fields.count("feature_monitoring")) o->feature_monitoring_json = fields["feature_monitoring"];
            if (fields.count("prediction_monitoring")) o->prediction_monitoring_json = fields["prediction_monitoring"];
            if (fields.count("alerts")) o->alerts_json = fields["alerts"];
            if (fields.count("auto_remediation")) o->auto_remediation_json = fields["auto_remediation"];
            if (fields.count("dataops")) o->dataops_ref = fields["dataops"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          default:
            break;
        }

        if (runtime_obj)
        {
          auto* name_str = copy_string(obj_name.c_str(), obj_name.size());
          globals_.set(name_str, Value::ObjVal(runtime_obj));
          stack_.push_back(Value::ObjVal(runtime_obj));
        }
        else
        {
          stack_.push_back(Value::Nil());
        }
        break;
      }

      // --- 17 ACTION opcodes: runtime stubs ---

      case OpCode::OP_DS_DISCOVER_DATA:
      case OpCode::OP_DS_SENSE_VOLUME:
      case OpCode::OP_DS_RUN_EDA:
      case OpCode::OP_DS_FRAME_PROBLEM:
      case OpCode::OP_DS_TEST_HYPOTHESIS:
      case OpCode::OP_DS_ENGINEER_FEATURES:
      case OpCode::OP_DS_TRAIN_MODEL:
      case OpCode::OP_DS_EVALUATE_MODEL:
      case OpCode::OP_DS_EXPLAIN_MODEL:
      case OpCode::OP_DS_PREDICT:
      case OpCode::OP_DS_RECOMMEND:
      case OpCode::OP_DS_RUN_EXPERIMENT:
      case OpCode::OP_DS_SCENARIO:
      case OpCode::OP_DS_BUILD_VENV:
      case OpCode::OP_DS_EXEC_PYTHON:
      case OpCode::OP_DS_SUBMIT_SPARK:
      case OpCode::OP_DS_PUSHDOWN_SQL:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        stack_.resize(base);
        stack_.push_back(Value::Nil());
        break;
      }

      // ═══════════════════════════════════════════════════════════════
      // v0.9.8.1 Causal Agent opcodes
      // ═══════════════════════════════════════════════════════════════

      case OpCode::OP_DEFINE_CAUSAL_AGENT:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* obj = new_causal_agent();
        obj->name = fields["name"];
        // LLM base
        if (fields.count("provider")) obj->provider = fields["provider"];
        if (fields.count("model")) obj->llm_model = fields["model"];
        if (fields.count("system")) obj->system_prompt = fields["system"];
        if (fields.count("temperature")) obj->temperature = std::stod(fields["temperature"]);
        // Agent.MD
        if (fields.count("agent_md")) obj->agent_md_path = fields["agent_md"];
        // Sub-agents & forge
        if (fields.count("sub_agents"))
        {
          try { obj->sub_agents_json = fields["sub_agents"]; } catch (...) {}
        }
        if (fields.count("forge")) obj->forge_ref = fields["forge"];
        // Peer agent
        if (fields.count("peer_agent")) obj->peer_agent_ref = fields["peer_agent"];
        // Component refs
        if (fields.count("discovery")) obj->discovery_ref = fields["discovery"];
        if (fields.count("scm")) obj->scm_ref = fields["scm"];
        if (fields.count("intervention")) obj->intervention_ref = fields["intervention"];
        if (fields.count("counterfactual")) obj->counterfactual_ref = fields["counterfactual"];
        if (fields.count("bayesian_model")) obj->bayesian_model_ref = fields["bayesian_model"];
        if (fields.count("estimator")) obj->estimator_ref = fields["estimator"];
        if (fields.count("sensitivity")) obj->sensitivity_ref = fields["sensitivity"];
        if (fields.count("data_requirements")) obj->data_requirements_ref = fields["data_requirements"];
        if (fields.count("code_interpreter")) obj->code_interpreter_ref = fields["code_interpreter"];
        // Coordination
        if (fields.count("coordinates_with")) obj->coordinates_with = fields["coordinates_with"];
        if (fields.count("handoffs")) obj->handoffs = fields["handoffs"];
        // Identity
        if (fields.count("role")) obj->role = fields["role"];
        if (fields.count("purpose")) obj->purpose = fields["purpose"];
        if (fields.count("autonomy")) obj->autonomy = fields["autonomy"];
        // Budget
        if (fields.count("budget")) obj->budget_ref = fields["budget"];

        obj->status = "initialized";

        auto* name_str = copy_string(obj->name.c_str(), obj->name.size());
        globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(obj)));
        break;
      }

      // --- 9 remaining Causal DEFINE opcodes: generic field-reading handlers ---

      case OpCode::OP_DEFINE_CAUSAL_DISCOVERY:
      case OpCode::OP_DEFINE_SCM:
      case OpCode::OP_DEFINE_INTERVENTION:
      case OpCode::OP_DEFINE_COUNTERFACTUAL:
      case OpCode::OP_DEFINE_BAYESIAN_MODEL:
      case OpCode::OP_DEFINE_CAUSAL_ESTIMATOR:
      case OpCode::OP_DEFINE_QUASI_EXPERIMENT:
      case OpCode::OP_DEFINE_CAUSAL_SENSITIVITY:
      case OpCode::OP_DEFINE_CAUSAL_DATA_REQUIREMENTS:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        // Create the appropriate runtime object based on opcode
        Obj* runtime_obj = nullptr;
        std::string obj_name;

        switch (op)
        {
          case OpCode::OP_DEFINE_CAUSAL_DISCOVERY:
          {
            auto* o = new_causal_discovery();
            o->name = fields["name"];
            if (fields.count("llm_discovery")) o->llm_discovery_json = fields["llm_discovery"];
            if (fields.count("algorithmic_discovery")) o->algorithmic_discovery_json = fields["algorithmic_discovery"];
            if (fields.count("merge_strategy")) o->merge_strategy_json = fields["merge_strategy"];
            if (fields.count("validation")) o->validation_json = fields["validation"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_SCM:
          {
            auto* o = new_scm();
            o->name = fields["name"];
            if (fields.count("variables")) o->variables_json = fields["variables"];
            if (fields.count("exogenous")) o->exogenous_json = fields["exogenous"];
            if (fields.count("latent_confounders")) o->latent_confounders_json = fields["latent_confounders"];
            if (fields.count("dag")) o->dag_ref = fields["dag"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_INTERVENTION:
          {
            auto* o = new_intervention();
            o->name = fields["name"];
            if (fields.count("scm")) o->scm_ref = fields["scm"];
            if (fields.count("do")) o->do_json = fields["do"];
            if (fields.count("outcome")) o->outcome = fields["outcome"];
            if (fields.count("identification")) o->identification_json = fields["identification"];
            if (fields.count("estimation")) o->estimation_json = fields["estimation"];
            if (fields.count("compare_with_naive")) o->compare_with_naive = fields["compare_with_naive"] == "true";
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_COUNTERFACTUAL:
          {
            auto* o = new_counterfactual();
            o->name = fields["name"];
            if (fields.count("scm")) o->scm_ref = fields["scm"];
            if (fields.count("evidence")) o->evidence_json = fields["evidence"];
            if (fields.count("question")) o->question = fields["question"];
            if (fields.count("abduction")) o->abduction_json = fields["abduction"];
            if (fields.count("action")) o->action_json = fields["action"];
            if (fields.count("prediction")) o->prediction_json = fields["prediction"];
            if (fields.count("attribution")) o->attribution_json = fields["attribution"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_BAYESIAN_MODEL:
          {
            auto* o = new_bayesian_model();
            o->name = fields["name"];
            if (fields.count("framework")) o->framework = fields["framework"];
            if (fields.count("version")) o->version = fields["version"];
            if (fields.count("priors")) o->priors_json = fields["priors"];
            if (fields.count("likelihood")) o->likelihood_json = fields["likelihood"];
            if (fields.count("sampling")) o->sampling_json = fields["sampling"];
            if (fields.count("posterior")) o->posterior_json = fields["posterior"];
            if (fields.count("comparison")) o->comparison_json = fields["comparison"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_CAUSAL_ESTIMATOR:
          {
            auto* o = new_causal_estimator();
            o->name = fields["name"];
            if (fields.count("scm")) o->scm_ref = fields["scm"];
            if (fields.count("treatment")) o->treatment = fields["treatment"];
            if (fields.count("outcome")) o->outcome = fields["outcome"];
            if (fields.count("primary")) o->primary_json = fields["primary"];
            if (fields.count("secondary")) o->secondary_json = fields["secondary"];
            if (fields.count("heterogeneous")) o->heterogeneous_json = fields["heterogeneous"];
            if (fields.count("compare_estimators")) o->compare_estimators = fields["compare_estimators"] == "true";
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_QUASI_EXPERIMENT:
          {
            auto* o = new_quasi_experiment();
            o->name = fields["name"];
            if (fields.count("method")) o->method = fields["method"];
            if (fields.count("treatment_time")) o->treatment_time = fields["treatment_time"];
            if (fields.count("treatment_group")) o->treatment_group = fields["treatment_group"];
            if (fields.count("control_group")) o->control_group = fields["control_group"];
            if (fields.count("outcome")) o->outcome = fields["outcome"];
            if (fields.count("covariates")) o->covariates = fields["covariates"];
            if (fields.count("parallel_trends_test")) o->parallel_trends_test = fields["parallel_trends_test"] == "true";
            if (fields.count("bayesian")) o->bayesian = fields["bayesian"] == "true";
            if (fields.count("mcmc")) o->mcmc_json = fields["mcmc"];
            if (fields.count("running_variable")) o->running_variable = fields["running_variable"];
            if (fields.count("cutoff")) o->cutoff = std::stod(fields["cutoff"]);
            if (fields.count("bandwidth")) o->bandwidth = fields["bandwidth"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_CAUSAL_SENSITIVITY:
          {
            auto* o = new_causal_sensitivity();
            o->name = fields["name"];
            if (fields.count("estimator")) o->estimator_ref = fields["estimator"];
            if (fields.count("rosenbaum")) o->rosenbaum_json = fields["rosenbaum"];
            if (fields.count("e_value")) o->e_value = fields["e_value"] == "true";
            if (fields.count("refutations")) o->refutations_json = fields["refutations"];
            if (fields.count("assumptions")) o->assumptions_json = fields["assumptions"];
            if (fields.count("output")) o->output_json = fields["output"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          case OpCode::OP_DEFINE_CAUSAL_DATA_REQUIREMENTS:
          {
            auto* o = new_causal_data_requirements();
            o->name = fields["name"];
            if (fields.count("temporal")) o->temporal_json = fields["temporal"];
            if (fields.count("required_confounders")) o->required_confounders = fields["required_confounders"];
            if (fields.count("instruments")) o->instruments_json = fields["instruments"];
            if (fields.count("natural_experiments")) o->natural_experiments = fields["natural_experiments"];
            if (fields.count("quality")) o->quality_json = fields["quality"];
            obj_name = o->name;
            runtime_obj = reinterpret_cast<Obj*>(o);
            break;
          }
          default:
            break;
        }

        if (runtime_obj)
        {
          auto* name_str = copy_string(obj_name.c_str(), obj_name.size());
          globals_.set(name_str, Value::ObjVal(runtime_obj));
          stack_.push_back(Value::ObjVal(runtime_obj));
        }
        else
        {
          stack_.push_back(Value::Nil());
        }
        break;
      }

      // --- 10 Causal ACTION opcodes: runtime stubs ---

      case OpCode::OP_CAUSAL_DISCOVER:
      case OpCode::OP_CAUSAL_BUILD_SCM:
      case OpCode::OP_CAUSAL_DO:
      case OpCode::OP_CAUSAL_IDENTIFY:
      case OpCode::OP_CAUSAL_ESTIMATE:
      case OpCode::OP_CAUSAL_COUNTERFACTUAL:
      case OpCode::OP_CAUSAL_BAYESIAN_FIT:
      case OpCode::OP_CAUSAL_SENSITIVITY:
      case OpCode::OP_CAUSAL_EXPLAIN:
      case OpCode::OP_CAUSAL_VISUALIZE_DAG:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        stack_.resize(base);
        stack_.push_back(Value::Nil());
        break;
      }

      // ═══════════════════════════════════════════════════════════════
      // v0.9.8.2 MLOps Agent opcodes
      // ═══════════════════════════════════════════════════════════════

      case OpCode::OP_DEFINE_MLOPS_AGENT:
      {
        const auto field_count = code[frame.ip++];
        std::size_t base = stack_.size() - field_count * 2;
        std::unordered_map<std::string, std::string> fields;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        auto* agent = new_mlops_agent();
        agent->name = fields["name"];
        // LLM base
        if (fields.count("provider")) agent->provider = fields["provider"];
        if (fields.count("model")) agent->llm_model = fields["model"];
        if (fields.count("system")) agent->system_prompt = fields["system"];
        if (fields.count("temperature")) agent->temperature = std::stod(fields["temperature"]);
        // Agent.MD
        if (fields.count("agent_md")) agent->agent_md_path = fields["agent_md"];
        // Sub-agents & forge
        if (fields.count("sub_agents"))
        {
          try { agent->sub_agents_json = fields["sub_agents"]; } catch (...) {}
        }
        // Component refs
        if (fields.count("drift_monitor")) agent->drift_monitor_ref = fields["drift_monitor"];
        if (fields.count("retraining_pipeline")) agent->retraining_pipeline_ref = fields["retraining_pipeline"];
        if (fields.count("deployment_strategy")) agent->deployment_strategy_ref = fields["deployment_strategy"];
        if (fields.count("champion_challenger")) agent->champion_challenger_ref = fields["champion_challenger"];
        if (fields.count("serving_infra")) agent->serving_infra_ref = fields["serving_infra"];
        if (fields.count("training_infra")) agent->training_infra_ref = fields["training_infra"];
        if (fields.count("rollback_policy")) agent->rollback_policy_ref = fields["rollback_policy"];
        if (fields.count("monitoring_stack")) agent->monitoring_stack_ref = fields["monitoring_stack"];
        if (fields.count("mlflow")) agent->mlflow_ref = fields["mlflow"];
        if (fields.count("business_kpi_tracker")) agent->business_kpi_tracker_ref = fields["business_kpi_tracker"];
        if (fields.count("feedback_loop")) agent->feedback_loop_ref = fields["feedback_loop"];
        if (fields.count("decision_engine")) agent->decision_engine_ref = fields["decision_engine"];
        if (fields.count("event_bus")) agent->event_bus_ref = fields["event_bus"];
        // Coordination
        if (fields.count("coordinates_with")) agent->coordinates_with = fields["coordinates_with"];
        if (fields.count("handoffs")) agent->handoffs = fields["handoffs"];
        // Identity
        if (fields.count("role")) agent->role = fields["role"];
        if (fields.count("purpose")) agent->purpose = fields["purpose"];
        if (fields.count("autonomy")) agent->autonomy = fields["autonomy"];
        // Budget
        if (fields.count("budget")) agent->budget_ref = fields["budget"];

        agent->status = "initialized";

        auto* name_str = copy_string(agent->name.c_str(), agent->name.size());
        globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        break;
      }

      // --- 15 remaining MLOps DEFINE opcodes: generic field-reading handlers ---

      case OpCode::OP_DEFINE_DRIFT_MONITOR:
      case OpCode::OP_DEFINE_RETRAINING_PIPELINE:
      case OpCode::OP_DEFINE_ML_DEPLOY_STRATEGY:
      case OpCode::OP_DEFINE_CHAMPION_CHALLENGER:
      case OpCode::OP_DEFINE_SERVING_INFRA:
      case OpCode::OP_DEFINE_TRAINING_INFRA_MLOPS:
      case OpCode::OP_DEFINE_MLOPS_ROLLBACK:
      case OpCode::OP_DEFINE_MONITORING_STACK:
      case OpCode::OP_DEFINE_MLFLOW_CONFIG:
      case OpCode::OP_DEFINE_BUSINESS_KPI_TRACKER:
      case OpCode::OP_DEFINE_DATASET_VERSION:
      case OpCode::OP_DEFINE_FEEDBACK_LOOP:
      case OpCode::OP_DEFINE_DECISION_ENGINE:
      case OpCode::OP_DEFINE_EVENT_BUS:
      case OpCode::OP_DEFINE_DRIFT_RCA:
      {
        const auto field_count = code[frame.ip++];
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);
        stack_.push_back(Value::Nil());
        break;
      }

      // v0.9.8.3: Data-BA Agent consolidated opcode
      case OpCode::OP_DEFINE_BA_DECLARATION:
      {
        const auto sub_type = code[frame.ip++];
        const auto field_count = code[frame.ip++];

        // Read all key-value pairs into a map
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val = to_std_string(stack_[base + i * 2 + 1]);
          fields[key] = val;
        }
        stack_.resize(base);

        // For sub_type 16 (DataBAAgent), create full agent object
        if (sub_type == 16)
        {
          auto* agent = new_databa_agent();
          if (fields.count("name")) agent->name = fields["name"];
          if (fields.count("provider")) agent->provider = fields["provider"];
          if (fields.count("model")) agent->llm_model = fields["model"];
          if (fields.count("system")) agent->system_prompt = fields["system"];
          if (fields.count("temperature")) agent->temperature = std::stod(fields["temperature"]);
          if (fields.count("agent_md")) agent->agent_md_path = fields["agent_md"];
          if (fields.count("downstream_agents")) agent->downstream_agents_json = fields["downstream_agents"];
          if (fields.count("elicitation")) agent->elicitation_ref = fields["elicitation"];
          if (fields.count("brd")) agent->brd_ref = fields["brd"];
          if (fields.count("functional_spec")) agent->functional_spec_ref = fields["functional_spec"];
          if (fields.count("nfr_spec")) agent->nfr_spec_ref = fields["nfr_spec"];
          if (fields.count("data_requirements")) agent->data_requirements_ref = fields["data_requirements"];
          if (fields.count("impact_analysis")) agent->impact_analysis_ref = fields["impact_analysis"];
          if (fields.count("traceability")) agent->traceability_ref = fields["traceability"];
          if (fields.count("etl_spec")) agent->etl_spec_ref = fields["etl_spec"];
          if (fields.count("ml_spec")) agent->ml_spec_ref = fields["ml_spec"];
          if (fields.count("governance_spec")) agent->governance_spec_ref = fields["governance_spec"];
          if (fields.count("analytics_spec")) agent->analytics_spec_ref = fields["analytics_spec"];
          if (fields.count("stakeholders")) agent->stakeholders_ref = fields["stakeholders"];
          if (fields.count("user_stories")) agent->user_stories_ref = fields["user_stories"];
          if (fields.count("scope")) agent->scope_ref = fields["scope"];
          if (fields.count("coordinates_with")) agent->coordinates_with = fields["coordinates_with"];
          if (fields.count("handoffs")) agent->handoffs = fields["handoffs"];
          if (fields.count("role")) agent->role = fields["role"];
          if (fields.count("purpose")) agent->purpose = fields["purpose"];
          if (fields.count("autonomy")) agent->autonomy = fields["autonomy"];
          if (fields.count("budget")) agent->budget_ref = fields["budget"];

          auto* name_str = copy_string(agent->name.c_str(), agent->name.size());
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else
        {
          // For sub_types 0-15, just pop fields and push Nil (generic handler)
          stack_.push_back(Value::Nil());
        }
        break;
      }

      // v0.9.8.4: Data Testing Agent consolidated opcode
      case OpCode::OP_DEFINE_TEST_DECLARATION:
      {
        const auto sub_type = code[frame.ip++];
        const auto field_count = code[frame.ip++];

        // Read all key-value pairs into a map
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val_v = stack_[base + i * 2 + 1];
          if (val_v.is_string())
          {
            fields[key] = to_std_string(val_v);
          }
          else if (val_v.is_number())
          {
            fields[key] = std::to_string(val_v.as_number());
          }
          else if (val_v.is_bool())
          {
            fields[key] = val_v.as_bool() ? "true" : "false";
          }
        }
        stack_.resize(base);

        // For sub_type 15 (DataTestAgent), create full agent object
        if (sub_type == 15)
        {
          auto* agent = new_datatest_agent();
          if (fields.count("name")) agent->name = fields["name"];
          if (fields.count("provider")) agent->provider = fields["provider"];
          if (fields.count("model")) agent->llm_model = fields["model"];
          if (fields.count("system")) agent->name = fields["name"];  // system_prompt not in struct; name already set
          if (fields.count("temperature")) agent->temperature = std::stod(fields["temperature"]);
          if (fields.count("agent_md")) agent->agent_md_path = fields["agent_md"];
          if (fields.count("sub_agents")) agent->sub_agents_json = fields["sub_agents"];
          if (fields.count("forge")) agent->forge_ref = fields["forge"];
          if (fields.count("test_strategy")) agent->test_strategy_ref = fields["test_strategy"];
          if (fields.count("test_generator")) agent->test_generator_ref = fields["test_generator"];
          if (fields.count("etl_tests")) agent->etl_tests_ref = fields["etl_tests"];
          if (fields.count("dw_tests")) agent->dw_tests_ref = fields["dw_tests"];
          if (fields.count("ml_tests")) agent->ml_tests_ref = fields["ml_tests"];
          if (fields.count("api_tests")) agent->api_tests_ref = fields["api_tests"];
          if (fields.count("performance_tests")) agent->performance_tests_ref = fields["performance_tests"];
          if (fields.count("edge_tests")) agent->edge_tests_ref = fields["edge_tests"];
          if (fields.count("sit_suite")) agent->sit_suite_ref = fields["sit_suite"];
          if (fields.count("uat_suite")) agent->uat_suite_ref = fields["uat_suite"];
          if (fields.count("regression_suite")) agent->regression_suite_ref = fields["regression_suite"];
          if (fields.count("quality_gate")) agent->quality_gate_ref = fields["quality_gate"];
          if (fields.count("report_config")) agent->report_config_ref = fields["report_config"];
          if (fields.count("defect_mgmt")) agent->defect_mgmt_ref = fields["defect_mgmt"];
          if (fields.count("coordinates_with")) agent->coordinates_with = fields["coordinates_with"];
          if (fields.count("handoffs")) agent->handoffs = fields["handoffs"];
          if (fields.count("role")) agent->role = fields["role"];
          if (fields.count("purpose")) agent->purpose = fields["purpose"];
          if (fields.count("autonomy")) agent->autonomy = fields["autonomy"];
          if (fields.count("budget")) agent->budget_ref = fields["budget"];

          auto* name_str = copy_string(agent->name.c_str(), agent->name.size());
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else
        {
          // For sub_types 0-14, just pop fields and push Nil (generic handler)
          stack_.push_back(Value::Nil());
        }
        break;
      }

      // v0.9.9: Data Intelligent Orchestrator consolidated opcode
      case OpCode::OP_DEFINE_DIO_DECLARATION:
      {
        const auto sub_type = code[frame.ip++];
        const auto field_count = code[frame.ip++];

        // Read all key-value pairs into a map
        std::unordered_map<std::string, std::string> fields;
        std::size_t base = stack_.size() - field_count * 2;
        for (int i = 0; i < field_count; i++)
        {
          auto key = to_std_string(stack_[base + i * 2]);
          auto val_v = stack_[base + i * 2 + 1];
          if (val_v.is_string())
          {
            fields[key] = to_std_string(val_v);
          }
          else if (val_v.is_number())
          {
            fields[key] = std::to_string(val_v.as_number());
          }
          else if (val_v.is_bool())
          {
            fields[key] = val_v.as_bool() ? "true" : "false";
          }
        }
        stack_.resize(base);

        // For sub_type 15 (DIOAgent), create full agent object
        if (sub_type == 15)
        {
          auto* agent = new_dio_agent();
          if (fields.count("name")) agent->name = fields["name"];
          if (fields.count("provider")) agent->provider = fields["provider"];
          if (fields.count("model")) agent->llm_model = fields["model"];
          if (fields.count("temperature")) agent->temperature = std::stod(fields["temperature"]);
          if (fields.count("mode")) agent->mode = fields["mode"];
          if (fields.count("task")) agent->task = fields["task"];
          if (fields.count("agent_md")) agent->agent_md_path = fields["agent_md"];
          if (fields.count("infrastructure")) agent->infrastructure_ref = fields["infrastructure"];
          if (fields.count("agent_registry")) agent->agent_registry_ref = fields["agent_registry"];
          if (fields.count("raci_matrix")) agent->raci_matrix_ref = fields["raci_matrix"];
          if (fields.count("pattern_selector")) agent->pattern_selector_ref = fields["pattern_selector"];
          if (fields.count("crew_formation")) agent->crew_formation_ref = fields["crew_formation"];
          if (fields.count("execution_manager")) agent->execution_manager_ref = fields["execution_manager"];
          if (fields.count("state_machine")) agent->state_machine_ref = fields["state_machine"];
          if (fields.count("error_handling")) agent->error_handling_ref = fields["error_handling"];
          if (fields.count("result_synthesizer")) agent->result_synthesizer_ref = fields["result_synthesizer"];
          if (fields.count("managed_agents")) agent->managed_agents_json = fields["managed_agents"];
          if (fields.count("guardrails")) agent->guardrails_json = fields["guardrails"];
          if (fields.count("coordinates_with")) agent->coordinates_with = fields["coordinates_with"];
          if (fields.count("role")) agent->role = fields["role"];
          if (fields.count("purpose")) agent->purpose = fields["purpose"];
          if (fields.count("autonomy")) agent->autonomy = fields["autonomy"];
          if (fields.count("budget")) agent->budget_ref = fields["budget"];

          auto* name_str = copy_string(agent->name.c_str(), agent->name.size());
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else if (sub_type == 16)
        {
          // v1.1: NeamOS Foundation simple keywords (knowledge_card, context_assembly, etc.)
          // Register name in globals as a generic declaration
          std::string decl_name = fields.count("name") ? fields["name"] : "unnamed";
          auto* name_str = copy_string(decl_name.c_str(), decl_name.size());
          // Create a minimal object - fields stored in globals for native function access
          auto* agent = new_dio_agent();
          agent->name = decl_name;
          if (fields.count("provider")) agent->provider = fields["provider"];
          if (fields.count("model")) agent->llm_model = fields["model"];
          for (const auto& [k, v] : fields) {
            if (k != "name" && k != "provider" && k != "model") {
              // Store extra fields in managed_agents_json for later retrieval
              if (!agent->managed_agents_json.empty()) agent->managed_agents_json += ",";
              agent->managed_agents_json += "\"" + k + "\":\"" + v + "\"";
            }
          }
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else if (sub_type == 17)
        {
          // v1.1: KnowledgeWeaver agent
          auto* agent = new_dio_agent();
          if (fields.count("name")) agent->name = fields["name"];
          if (fields.count("provider")) agent->provider = fields["provider"];
          if (fields.count("model")) agent->llm_model = fields["model"];
          if (fields.count("temperature")) agent->temperature = std::stod(fields["temperature"]);
          if (fields.count("budget")) agent->budget_ref = fields["budget"];
          if (fields.count("fabric")) agent->managed_agents_json = fields["fabric"];
          if (fields.count("monitors")) agent->guardrails_json = fields["monitors"];
          agent->mode = "knowledgeweaver";
          auto* name_str = copy_string(agent->name.c_str(), agent->name.size());
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else if (sub_type == 18)
        {
          // v1.1: AdaptAgent
          auto* agent = new_dio_agent();
          if (fields.count("name")) agent->name = fields["name"];
          if (fields.count("provider")) agent->provider = fields["provider"];
          if (fields.count("model")) agent->llm_model = fields["model"];
          if (fields.count("temperature")) agent->temperature = std::stod(fields["temperature"]);
          if (fields.count("budget")) agent->budget_ref = fields["budget"];
          if (fields.count("monitors")) agent->managed_agents_json = fields["monitors"];
          if (fields.count("proposals")) agent->guardrails_json = fields["proposals"];
          agent->mode = "adaptagent";
          auto* name_str = copy_string(agent->name.c_str(), agent->name.size());
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else if (sub_type == 19)
        {
          // v1.1: Storyteller agent
          auto* agent = new_dio_agent();
          if (fields.count("name")) agent->name = fields["name"];
          if (fields.count("provider")) agent->provider = fields["provider"];
          if (fields.count("model")) agent->llm_model = fields["model"];
          if (fields.count("temperature")) agent->temperature = std::stod(fields["temperature"]);
          if (fields.count("budget")) agent->budget_ref = fields["budget"];
          if (fields.count("sub_agents")) agent->managed_agents_json = fields["sub_agents"];
          if (fields.count("safety")) agent->guardrails_json = fields["safety"];
          agent->mode = "storyteller";
          auto* name_str = copy_string(agent->name.c_str(), agent->name.size());
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else if (sub_type == 20)
        {
          // v1.2: NeamProd declarations (plugin, session_service, eval_test, etc.)
          std::string decl_name = fields.count("name") ? fields["name"] : "unnamed";
          auto* name_str = copy_string(decl_name.c_str(), decl_name.size());
          auto* agent = new_dio_agent();
          agent->name = decl_name;
          agent->mode = "v1.2_prod";
          for (const auto& [k, v] : fields) {
            if (k != "name") {
              if (!agent->managed_agents_json.empty()) agent->managed_agents_json += ",";
              agent->managed_agents_json += "\"" + k + "\":\"" + v + "\"";
            }
          }
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else if (sub_type == 21)
        {
          // v1.3: NeamLab simple declarations (program, experiment_loop, metric_extractor)
          std::string decl_name = fields.count("name") ? fields["name"] : "unnamed";
          auto* name_str = copy_string(decl_name.c_str(), decl_name.size());
          auto* agent = new_dio_agent();
          agent->name = decl_name;
          agent->mode = "v1.3_lab";
          for (const auto& [k, v] : fields) {
            if (k != "name") {
              if (!agent->managed_agents_json.empty()) agent->managed_agents_json += ",";
              agent->managed_agents_json += "\"" + k + "\":\"" + v + "\"";
            }
          }
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else if (sub_type == 22)
        {
          // v1.3: Research Agent
          auto* agent = new_dio_agent();
          if (fields.count("name")) agent->name = fields["name"];
          if (fields.count("provider")) agent->provider = fields["provider"];
          if (fields.count("model")) agent->llm_model = fields["model"];
          if (fields.count("temperature")) agent->temperature = std::stod(fields["temperature"]);
          if (fields.count("budget")) agent->budget_ref = fields["budget"];
          if (fields.count("program")) agent->managed_agents_json = "program:" + fields["program"];
          if (fields.count("metric")) agent->guardrails_json = "metric:" + fields["metric"];
          agent->mode = "research";
          auto* name_str = copy_string(agent->name.c_str(), agent->name.size());
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else if (sub_type == 23)
        {
          // v1.4: NeamWiki — Wiki declaration
          std::string decl_name = fields.count("name") ? fields["name"] : "unnamed";
          auto* name_str = copy_string(decl_name.c_str(), decl_name.size());
          auto* agent = new_dio_agent();
          agent->name = decl_name;
          agent->mode = "v1.4_wiki";
          for (const auto& [k, v] : fields) {
            if (k != "name") {
              if (!agent->managed_agents_json.empty()) agent->managed_agents_json += ",";
              agent->managed_agents_json += "\"" + k + "\":\"" + v + "\"";
            }
          }
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else if (sub_type == 24)
        {
          // v1.4: NeamWiki — Wiki Agent
          auto* agent = new_dio_agent();
          if (fields.count("name")) agent->name = fields["name"];
          if (fields.count("provider")) agent->provider = fields["provider"];
          if (fields.count("model")) agent->llm_model = fields["model"];
          if (fields.count("temperature")) agent->temperature = std::stod(fields["temperature"]);
          if (fields.count("budget")) agent->budget_ref = fields["budget"];
          if (fields.count("wikis")) agent->managed_agents_json = "wikis:" + fields["wikis"];
          if (fields.count("operations")) agent->guardrails_json = "operations:" + fields["operations"];
          agent->mode = "wiki_agent";
          auto* name_str = copy_string(agent->name.c_str(), agent->name.size());
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));
        }
        else if (sub_type == 25 || sub_type == 26 || sub_type == 27 ||
                 sub_type == 28 || sub_type == 29)
        {
          // v1.4.5: NeamHarness family —
          //   25: harness  26: handoff  27: tool_registry
          //   28: assertion_registry  29: harness_benchmark
          static const char* kModes[] = {
            "v1.4.5_harness",            // 25
            "v1.4.5_handoff",            // 26
            "v1.4.5_tool_registry",      // 27
            "v1.4.5_assertion_registry", // 28
            "v1.4.5_harness_benchmark"   // 29
          };
          std::string decl_name = fields.count("name") ? fields["name"] : "unnamed";
          auto* agent = new_dio_agent();
          agent->name = decl_name;
          agent->mode = kModes[sub_type - 25];
          for (const auto& [k, v] : fields) {
            if (k != "name") {
              if (!agent->managed_agents_json.empty()) agent->managed_agents_json += ",";
              agent->managed_agents_json += "\"" + k + "\":\"" + v + "\"";
            }
          }
          auto* name_str = copy_string(agent->name.c_str(), agent->name.size());
          globals_.set(name_str, Value::ObjVal(reinterpret_cast<Obj*>(agent)));
          stack_.push_back(Value::ObjVal(reinterpret_cast<Obj*>(agent)));

          // v1.4.5 Phase 3-minimal: populate HarnessRegistry side table.
          // FR-H-5 hash is over (name + fields_json).
          const std::string fields_blob = fields.count("fields") ? fields["fields"] : "";
          auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
          if (sub_type == 25) {
            ::neamc::vm::harness::HarnessRecord rec;
            rec.name = decl_name;
            rec.provider = fields.count("provider") ? fields["provider"] : "";
            rec.model = fields.count("model") ? fields["model"] : "";
            rec.fields_json = fields_blob;
            rec.bytecode_hash =
                ::neamc::vm::harness::compute_harness_hash(decl_name, fields_blob);
            rec.status = "registered";
            reg.register_harness(std::move(rec));
          } else if (sub_type == 26) {
            ::neamc::vm::harness::HandoffRecord rec;
            rec.name = decl_name;
            rec.fields_json = fields_blob;
            rec.schema_version =
                ::neamc::vm::harness::extract_json_string(fields_blob, "schema_version");
            reg.register_handoff(std::move(rec));
          } else if (sub_type == 27) {
            ::neamc::vm::harness::ToolRegistryRecord rec;
            rec.name = decl_name;
            rec.fields_json = fields_blob;
            reg.register_tool_registry(std::move(rec));
          } else if (sub_type == 28) {
            ::neamc::vm::harness::AssertionRegistryRecord rec;
            rec.name = decl_name;
            rec.fields_json = fields_blob;
            reg.register_assertion_registry(std::move(rec));
          } else if (sub_type == 29) {
            ::neamc::vm::harness::HarnessBenchmarkRecord rec;
            rec.name = decl_name;
            rec.fields_json = fields_blob;
            reg.register_harness_benchmark(std::move(rec));
          }
        }
        else
        {
          // For sub_types 0-14, just pop fields and push Nil (generic handler)
          stack_.push_back(Value::Nil());
        }
        break;
      }

      // v0.9.8.2: MLOps ACTION opcodes handled via native functions (not opcodes)
      // to stay within uint8_t opcode limit (256 max)

      // v0.8 Phase 6: Channel registration
      case OpCode::OP_DEFINE_CHANNEL:
      {
        Value config_value = pop();  // config map
        Value name_value = pop();    // channel name
        std::string ch_name = to_std_string(name_value);

        // Create channel object
        auto* channel = new_channel();
        channel->name = as_string(name_value);
        channel->config = config_value.is_map() ? as_map(config_value) : nullptr;

        // Register in channel_registry_ and globals_
        channel_registry_[ch_name] = channel;
        globals_.set(channel->name, Value::ObjVal(reinterpret_cast<Obj*>(channel)));

        // Create adapter based on "type" config field
        std::string ch_type;
        if (config_value.is_map())
        {
          ch_type = map_string_value(as_map(config_value), "type");
        }
        if (ch_type == "cli")
        {
          channel_adapters_[ch_name] = std::make_unique<CLIChannelAdapter>();
        }
        else if (ch_type == "http")
        {
          channel_adapters_[ch_name] = std::make_unique<HTTPChannelAdapter>();
        }
        break;
      }
      case OpCode::OP_WORKSPACE_READ:
      {
        Value path_val = pop();
        std::string rel_path = to_std_string(path_val);
        std::string full_path = resolve_workspace_path(claw_agents_, rel_path);
        if (full_path.empty())
        {
          stack_.push_back(Value::Nil());
          break;
        }
        std::ifstream in(full_path);
        if (!in.is_open())
        {
          stack_.push_back(Value::Nil());
          break;
        }
        std::string content((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
        stack_.push_back(Value::String(content.c_str(), content.size()));
        break;
      }
      case OpCode::OP_WORKSPACE_WRITE:
      {
        Value content_val = pop();
        Value path_val = pop();
        std::string rel_path = to_std_string(path_val);
        std::string content = to_std_string(content_val);
        std::string full_path = resolve_workspace_path(claw_agents_, rel_path);
        if (full_path.empty())
        {
          stack_.push_back(Value::Bool(false));
          break;
        }
        ensure_parent_dirs(full_path);
        std::ofstream out(full_path, std::ios::trunc);
        if (!out.is_open())
        {
          stack_.push_back(Value::Bool(false));
          break;
        }
        out << content;
        stack_.push_back(Value::Bool(true));
        break;
      }
      case OpCode::OP_MEMORY_SEARCH:
      {
        Value top_k_val = pop();
        Value query_val = pop();
        std::string query = to_std_string(query_val);
        std::size_t top_k = 5;
        if (top_k_val.is_number())
        {
          top_k = static_cast<std::size_t>(top_k_val.as_number());
        }

        auto results = search_memory(query, top_k);
        std::vector<Value> result_list;
        for (const auto& r : results)
        {
          std::unordered_map<std::string, Value> m;
          m["file_path"] = Value::String(r.file_path.c_str(), r.file_path.size());
          m["chunk"] = Value::String(r.chunk.c_str(), r.chunk.size());
          m["score"] = Value::Number(static_cast<double>(r.score));
          result_list.push_back(Value::Map(new_map(std::move(m))));
        }
        stack_.push_back(Value::List(new_list(std::move(result_list))));
        break;
      }
      case OpCode::OP_SPAWN_AGENT:
      {
        Value config_val = pop();
        Value agent_name_val = pop();
        std::string agent_name = to_std_string(agent_name_val);

        // Extract task string from config
        std::string task;
        if (config_val.is_string())
        {
          task = to_std_string(config_val);
        }
        else if (config_val.is_map())
        {
          auto* cmap = as_map(config_val);
          auto task_it = cmap->entries.find("task");
          if (task_it != cmap->entries.end())
            task = to_std_string(task_it->second);
        }

        // Try claw agents first
        auto claw_it = claw_agents_.find(agent_name);
        if (claw_it != claw_agents_.end())
        {
          auto* claw = claw_it->second;

          // Orchestrable on_spawn callback
          if (has_trait_impl(impl_tables_, trait_defs_, agent_name, "Orchestrable"))
          {
            try
            {
              auto child_val = Value::String(agent_name.c_str(), agent_name.size());
              invoke_trait_method(agent_name, "on_spawn", Value::Nil(), {child_val});
            }
            catch (...) {}
          }

          // Simplified single-turn LLM call (no tool loop)
          try
          {
            llm::ProviderConfig prov_config;
            prov_config.model = to_std_string(claw->model);
            prov_config.endpoint = to_std_string(claw->endpoint);
            prov_config.api_key = to_std_string(claw->api_key_env);
            prov_config.temperature = claw->temperature;
            prov_config.default_host = resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST", "http://localhost:11434");
            std::string pname = to_std_string(claw->provider);
            if (!prov_config.api_key.empty())
            {
              if (const char* ev = std::getenv(prov_config.api_key.c_str()))
                prov_config.api_key = ev;
              else
                prov_config.api_key.clear();
            }
            auto provider = llm::create_provider(pname, prov_config);
            std::string sys = to_std_string(claw->system);
            std::string prompt = sys.empty() ? task : (sys + "\n\n" + task);
            std::string response = provider->complete(prompt);

            // Orchestrable on_delegate callback
            if (has_trait_impl(impl_tables_, trait_defs_, agent_name, "Orchestrable"))
            {
              try
              {
                auto res_val = Value::String(response.c_str(), response.size());
                invoke_trait_method(agent_name, "on_delegate", Value::Nil(), {res_val});
              }
              catch (...) {}
            }

            stack_.push_back(Value::String(response.c_str(), response.size()));
          }
          catch (const std::exception& e)
          {
            std::string err = std::string("Spawn error (claw ") + agent_name + "): " + e.what();
            stack_.push_back(Value::String(err.c_str(), err.size()));
          }
          break;
        }

        // Try forge agents
        auto forge_it = forge_agents_.find(agent_name);
        if (forge_it != forge_agents_.end())
        {
          auto* forge = forge_it->second;

          // Orchestrable on_spawn callback
          if (has_trait_impl(impl_tables_, trait_defs_, agent_name, "Orchestrable"))
          {
            try
            {
              auto child_val = Value::String(agent_name.c_str(), agent_name.size());
              invoke_trait_method(agent_name, "on_spawn", Value::Nil(), {child_val});
            }
            catch (...) {}
          }

          // Build outcome map (full loop only via .run())
          std::unordered_map<std::string, Value> outcome;
          outcome["outcome"] = Value::String("completed", 9);
          outcome["iterations"] = Value::Number(0);
          outcome["total_cost"] = Value::Number(0.0);

          // Try single-turn for task description
          try
          {
            llm::ProviderConfig prov_config;
            prov_config.model = to_std_string(forge->model);
            prov_config.endpoint = to_std_string(forge->endpoint);
            prov_config.api_key = to_std_string(forge->api_key_env);
            prov_config.temperature = forge->temperature;
            prov_config.default_host = resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST", "http://localhost:11434");
            std::string pname = to_std_string(forge->provider);
            if (!prov_config.api_key.empty())
            {
              if (const char* ev = std::getenv(prov_config.api_key.c_str()))
                prov_config.api_key = ev;
              else
                prov_config.api_key.clear();
            }
            auto provider = llm::create_provider(pname, prov_config);
            std::string sys = to_std_string(forge->system);
            std::string prompt = sys.empty() ? task : (sys + "\n\n" + task);
            std::string response = provider->complete(prompt);
            outcome["message"] = Value::String(response.c_str(), response.size());
          }
          catch (const std::exception& e)
          {
            std::string msg = std::string("Provider error: ") + e.what();
            outcome["outcome"] = Value::String("aborted", 7);
            outcome["message"] = Value::String(msg.c_str(), msg.size());
          }

          auto* result_map = new_map(std::move(outcome));
          auto result_val = Value::Map(result_map);

          // Orchestrable on_delegate callback
          if (has_trait_impl(impl_tables_, trait_defs_, agent_name, "Orchestrable"))
          {
            try
            {
              invoke_trait_method(agent_name, "on_delegate", Value::Nil(), {result_val});
            }
            catch (...) {}
          }

          stack_.push_back(result_val);
          break;
        }

        // Try legacy agents in globals
        {
          auto* name_str = copy_string(agent_name.c_str(), agent_name.size());
          Value agent_val;
          if (globals_.get(name_str, &agent_val) && agent_val.is_agent())
          {
            auto* agent = static_cast<ObjAgent*>(agent_val.as_obj());
            try
            {
              llm::ProviderConfig prov_config;
              prov_config.model = to_std_string(agent->model);
              prov_config.endpoint = to_std_string(agent->endpoint);
              prov_config.api_key = to_std_string(agent->api_key_env);
              prov_config.temperature = agent->temperature;
              prov_config.default_host = resolve_env_config(*this, "ollama_host", "NEAM_OLLAMA_HOST", "http://localhost:11434");
              std::string pname = to_std_string(agent->provider);
              if (!prov_config.api_key.empty())
              {
                if (const char* ev = std::getenv(prov_config.api_key.c_str()))
                  prov_config.api_key = ev;
                else
                  prov_config.api_key.clear();
              }
              auto provider = llm::create_provider(pname, prov_config);
              std::string sys = to_std_string(agent->system);
              std::string prompt = sys.empty() ? task : (sys + "\n\n" + task);
              std::string response = provider->complete(prompt);
              stack_.push_back(Value::String(response.c_str(), response.size()));
            }
            catch (const std::exception& e)
            {
              std::string err = std::string("Spawn error (agent ") + agent_name + "): " + e.what();
              stack_.push_back(Value::String(err.c_str(), err.size()));
            }
            break;
          }
        }

        // Agent not found
        std::string err = "Agent not found: " + agent_name;
        stack_.push_back(Value::String(err.c_str(), err.size()));
        break;
      }
      case OpCode::OP_FORGE_ITERATE:
      {
        // v0.8 Phase 4: Reserved no-op — iteration managed by inline forge loop
        break;
      }
      case OpCode::OP_VERIFY:
      {
        // v0.8 Phase 4: Call verify_fn on first forge agent if available
        if (!forge_agents_.empty())
        {
          auto* forge = forge_agents_.begin()->second;
          if (forge->verify_fn)
          {
            auto* loop_ctx = new_loop_context();
            loop_ctx->forge_agent = forge;
            loop_ctx->iteration = 0;
            std::vector<Value> verify_args;
            verify_args.push_back(Value::LoopContext(loop_ctx));
            try
            {
              Value result = call_function(forge->verify_fn, verify_args, false, "verify");
              stack_.push_back(result);
            }
            catch (const std::exception&)
            {
              stack_.push_back(Value::Nil());
            }
          }
          else
          {
            stack_.push_back(Value::Nil());
          }
        }
        else
        {
          stack_.push_back(Value::Nil());
        }
        break;
      }
      case OpCode::OP_COMPACT:
      {
        // v0.8 Phase 3: Trigger claw session compaction
        if (!claw_agents_.empty())
        {
          auto* claw = claw_agents_.begin()->second;
          SessionManager sm;
          auto& session = sm.get_or_create(claw, "default");
          CompactionEngine ce;
          if (ce.needs_compaction(claw, session))
          {
            std::string provider_name = to_std_string(claw->provider);
            llm::ProviderConfig config;
            config.model = to_std_string(claw->model);
            config.endpoint = to_std_string(claw->endpoint);
            config.api_key = to_std_string(claw->api_key_env);
            if (!config.api_key.empty())
            {
              if (const char* env_val = std::getenv(config.api_key.c_str()))
                config.api_key = env_val;
              else
                config.api_key.clear();
            }
            auto provider = llm::create_provider(provider_name, config);
            auto summary = ce.compact(claw, session,
                [&](const std::string& prompt) {
                  return provider->complete(prompt);
                });
            if (!summary.empty())
            {
              sm.compact(claw, "default", summary);
            }
          }
        }
        break;
      }
      case OpCode::OP_FLUSH:
      {
        // v0.8 Phase 3: Flush claw session to workspace
        if (!claw_agents_.empty())
        {
          auto* claw = claw_agents_.begin()->second;
          if (!claw->workspace.empty())
          {
            SessionManager sm;
            auto& session = sm.get_or_create(claw, "default");
            std::string path = claw->workspace + "/session_dump.jsonl";
            std::ofstream out(path, std::ios::app);
            for (const auto& [role, content] : session.history)
            {
              nlohmann::json j;
              j["role"] = role;
              j["content"] = content;
              out << j.dump() << "\n";
            }

            // v0.8 Phase 7: Reindex memory on flush if flush_on_compact
            if (claw->flush_on_compact)
            {
              std::string aname(claw->name->chars, claw->name->length);
              auto mi_it = memory_indices_.find(aname);
              if (mi_it != memory_indices_.end())
              {
                mi_it->second->reindex_changed();
                mi_it->second->save();
              }
            }
          }
        }
        break;
      }
      case OpCode::OP_SESSION_HISTORY:
      {
        // v0.8 Phase 3: Query session history
        Value limit = pop();
        Value key = pop();
        std::string session_key = key.is_string() ? to_std_string(key) : "default";
        int max_entries = limit.is_number() ? static_cast<int>(limit.as_number()) : -1;
        if (!claw_agents_.empty())
        {
          auto* claw = claw_agents_.begin()->second;
          SessionManager sm;
          auto history = sm.load_history(claw, session_key, max_entries);
          auto* list = new_list(std::vector<Value>{});
          for (const auto& [role, content] : history)
          {
            auto* map = new_map({});
            map->entries["role"] = Value::String(role.c_str(), role.size());
            map->entries["content"] = Value::String(content.c_str(), content.size());
            list->items.push_back(Value::Map(map));
          }
          stack_.push_back(Value::List(list));
        }
        else
        {
          stack_.push_back(Value::List(new_list(std::vector<Value>{})));
        }
        break;
      }
      case OpCode::OP_FORGE_RUN:
      {
        Value config_val = pop();
        Value agent_name_val = pop();
        // v0.8 Phase 4: Look up forge agent and invoke .run()
        std::string aname = to_std_string(agent_name_val);
        auto it = forge_agents_.find(aname);
        if (it == forge_agents_.end())
        {
          throw std::runtime_error("Forge agent not found: " + aname);
        }
        auto* forge = it->second;

        // Apply config overrides if map provided
        if (config_val.is_map())
        {
          auto* cmap = as_map(config_val);
          std::string ws = map_string_value(cmap, "workspace");
          if (!ws.empty()) forge->workspace = ws;
          double mi = map_number_value(cmap, "max_iterations");
          if (mi > 0) forge->loop_config.max_iterations = static_cast<int>(mi);
          double mc = map_number_value(cmap, "max_cost");
          if (mc > 0) forge->loop_config.max_cost = mc;
        }

        // Invoke .run() by pushing the receiver and calling OP_INVOKE_METHOD pattern
        // Reuse the method dispatch logic by calling run() directly
        stack_.push_back(Value::ForgeAgent(forge));
        // Trigger method call via run_frames after setting up the forge agent on stack
        // Simpler: just construct and push a result map from the forge loop directly
        // The forge .run() method dispatch is already handled above — push the receiver
        // and re-enter the method dispatch. For simplicity, push nil and let the
        // .run() method be called from user code instead.

        // For opcode usage, build a minimal outcome map
        auto* result_map = new_map({});
        const char* msg = "Use forge_agent.run() for full forge loop";
        result_map->entries["outcome"] = Value::String("completed", 9);
        result_map->entries["iterations"] = Value::Number(0);
        result_map->entries["total_cost"] = Value::Number(0.0);
        result_map->entries["message"] = Value::String(msg, std::strlen(msg));
        // Remove the forge agent we pushed
        stack_.pop_back();
        stack_.push_back(Value::Map(result_map));
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

// v0.8 Phase 6: Public trait query + invocation methods
bool VirtualMachine::has_agent_trait_impl(const std::string& type, const std::string& trait) const
{
  return has_trait_impl(impl_tables_, trait_defs_, type, trait);
}

Value VirtualMachine::invoke_trait_method(const std::string& type, const std::string& method,
                                          Value self, const std::vector<Value>& args)
{
  std::lock_guard<std::mutex> lock(execution_mutex_);

  // Look up method in impl_tables_
  auto impl_it = impl_tables_.find(type);
  if (impl_it != impl_tables_.end())
  {
    auto method_it = impl_it->second->methods.find(method);
    if (method_it != impl_it->second->methods.end() && method_it->second.function)
    {
      std::vector<Value> call_args;
      call_args.push_back(self);
      for (const auto& a : args) call_args.push_back(a);
      return call_function(method_it->second.function, call_args, false, "");
    }
  }

  // Fall back to trait default methods
  for (const auto& [tname, tdef] : trait_defs_)
  {
    for (const auto& m : tdef->methods)
    {
      if (m.name == method && m.default_impl)
      {
        std::vector<Value> call_args;
        call_args.push_back(self);
        for (const auto& a : args) call_args.push_back(a);
        return call_function(m.default_impl, call_args, false, "");
      }
    }
  }

  throw std::runtime_error("Method '" + method + "' not found for type '" + type + "'");
}

ObjChannel* VirtualMachine::get_channel(const std::string& name) const
{
  auto it = channel_registry_.find(name);
  if (it != channel_registry_.end()) return it->second;
  return nullptr;
}

std::vector<MemorySearchResult> VirtualMachine::search_memory(const std::string& query,
                                                              std::size_t top_k)
{
  if (memory_indices_.empty()) return {};
  // Use first claw agent's memory index (matches existing Phase 3 pattern)
  auto it = memory_indices_.begin();
  // Reindex changed files before searching for fresh results
  it->second->reindex_changed();
  return it->second->search(query, top_k);
}
}  // namespace neamc::vm
