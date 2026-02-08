//
// Neam Virtual Machine - External skill dispatch (v0.6.7)
//
// Dispatches external skill calls to MCP servers, HTTP APIs,
// or Claude built-in tool handlers.
//

#include "neamc/vm/external_skill.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "neamc/llm/http_client.hpp"
#include "neamc/vm/object.hpp"

namespace neamc::vm
{
namespace
{
// Replace {param} placeholders in a template string with values from JSON input.
std::string expand_template(const std::string& tmpl, const nlohmann::json& input)
{
  std::string result = tmpl;
  // Match {param_name} patterns
  static const std::regex placeholder_re(R"(\{([a-zA-Z_][a-zA-Z0-9_]*)\})");
  std::smatch match;
  std::string working = tmpl;
  result.clear();
  while (std::regex_search(working, match, placeholder_re))
  {
    result += match.prefix().str();
    const std::string key = match[1].str();
    if (input.contains(key))
    {
      const auto& val = input.at(key);
      if (val.is_string())
      {
        result += val.get<std::string>();
      }
      else
      {
        result += val.dump();
      }
    }
    else
    {
      result += match[0].str();  // Keep unresolved placeholder
    }
    working = match.suffix().str();
  }
  result += working;
  return result;
}

// Expand ${ENV_VAR} references in a string via std::getenv.
std::string expand_env_vars(const std::string& input)
{
  std::string result = input;
  static const std::regex env_re(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\})");
  std::smatch match;
  std::string working = input;
  result.clear();
  while (std::regex_search(working, match, env_re))
  {
    result += match.prefix().str();
    const char* env_val = std::getenv(match[1].str().c_str());
    if (env_val)
    {
      result += env_val;
    }
    working = match.suffix().str();
  }
  result += working;
  return result;
}

// Extract a value from JSON using a JSON pointer path (e.g., "/forecast/summary").
std::string extract_json_path(const nlohmann::json& data, const std::string& path)
{
  if (path.empty())
  {
    return data.dump();
  }
  try
  {
    const auto& value = data.at(nlohmann::json::json_pointer(path));
    if (value.is_string())
    {
      return value.get<std::string>();
    }
    return value.dump();
  }
  catch (const std::exception&)
  {
    return data.dump();
  }
}

std::string dispatch_http_skill(ObjSkill* skill, const nlohmann::json& input)
{
  const auto& cfg = *skill->external;

  // Expand URL template with parameter values
  std::string url = expand_template(cfg.http_url_template, input);

  // Build body if present
  std::string body;
  if (!cfg.http_body_template.empty())
  {
    body = expand_template(cfg.http_body_template, input);
  }
  else if (cfg.http_method == "POST" || cfg.http_method == "PUT" ||
           cfg.http_method == "PATCH")
  {
    body = input.dump();
  }

  // Build headers with env var expansion
  std::vector<std::string> headers;
  for (const auto& [key, value] : cfg.http_headers)
  {
    headers.push_back(key + ": " + expand_env_vars(value));
  }
  // Default Content-Type if not set
  bool has_content_type = false;
  for (const auto& h : headers)
  {
    if (h.substr(0, 12) == "Content-Type" || h.substr(0, 12) == "content-type")
    {
      has_content_type = true;
      break;
    }
  }
  if (!has_content_type && !body.empty())
  {
    headers.push_back("Content-Type: application/json");
  }

  // Execute HTTP request
  auto result = llm::http_request(cfg.http_method, url, body, headers, cfg.http_timeout_ms);

  if (result.status >= 400)
  {
    throw std::runtime_error("HTTP " + std::to_string(result.status) + ": " + result.body);
  }

  // Extract response via JSON pointer if configured
  if (!cfg.http_response_path.empty())
  {
    try
    {
      auto parsed = nlohmann::json::parse(result.body);
      return extract_json_path(parsed, cfg.http_response_path);
    }
    catch (const nlohmann::json::parse_error&)
    {
      return result.body;
    }
  }

  return result.body;
}

std::string dispatch_mcp_skill(ObjSkill* skill, const nlohmann::json& input)
{
  // MCP dispatch is a placeholder — full JSON-RPC MCP client integration
  // would require a separate MCP client library. For now, return an error
  // indicating the MCP server connection is not yet established.
  const auto& cfg = *skill->external;
  throw std::runtime_error(
      "MCP tool dispatch not yet connected: server='" + cfg.mcp_server_name +
      "', tool='" + cfg.mcp_tool_name + "'. MCP client integration pending.");
}

// Execute a Claude built-in tool locally.
// When the provider is Anthropic/Bedrock, these tools are dispatched server-side.
// For other providers (OpenAI, Ollama), we execute them locally so that
// extern skill declarations are portable across all LLM backends.
std::string dispatch_claude_builtin(ObjSkill* skill, const nlohmann::json& input)
{
  const auto& tool_type = skill->external->claude_tool_type;

  if (tool_type == "bash_20241022")
  {
    // Execute bash command and capture output
    std::string command;
    if (input.contains("command") && input["command"].is_string())
    {
      command = input["command"].get<std::string>();
    }
    else
    {
      throw std::runtime_error("bash_20241022: 'command' parameter required");
    }

    // Redirect stderr to stdout so we capture everything
    command += " 2>&1";

    FILE* pipe = popen(command.c_str(), "r");
    if (!pipe)
    {
      throw std::runtime_error("bash_20241022: failed to execute command");
    }

    std::string output;
    char buffer[4096];
    while (fgets(buffer, sizeof(buffer), pipe))
    {
      output += buffer;
    }

    int status = pclose(pipe);
    if (status != 0 && output.empty())
    {
      output = "Command exited with status " + std::to_string(status);
    }

    return output;
  }

  if (tool_type == "text_editor_20241022")
  {
    // Basic text editor: supports "view" and "create" commands
    std::string cmd;
    std::string path;
    if (input.contains("command") && input["command"].is_string())
      cmd = input["command"].get<std::string>();
    if (input.contains("path") && input["path"].is_string())
      path = input["path"].get<std::string>();

    if (cmd == "view" && !path.empty())
    {
      std::ifstream file(path);
      if (!file.is_open())
        throw std::runtime_error("text_editor: cannot open " + path);
      std::stringstream buf;
      buf << file.rdbuf();
      return buf.str();
    }
    else if (cmd == "create" && !path.empty())
    {
      std::string content;
      if (input.contains("file_text") && input["file_text"].is_string())
        content = input["file_text"].get<std::string>();
      std::ofstream file(path);
      if (!file.is_open())
        throw std::runtime_error("text_editor: cannot write " + path);
      file << content;
      return "File created: " + path;
    }

    throw std::runtime_error("text_editor_20241022: unsupported command '" + cmd + "'");
  }

  throw std::runtime_error("Unsupported Claude built-in tool type: " + tool_type);
}
}  // namespace

std::string dispatch_external_skill(ObjSkill* skill, const nlohmann::json& input)
{
  if (!skill || !skill->external)
  {
    throw std::runtime_error("dispatch_external_skill called on non-external skill");
  }

  switch (skill->external->binding)
  {
    case SkillBinding::McpTool:
      return dispatch_mcp_skill(skill, input);
    case SkillBinding::HttpApi:
      return dispatch_http_skill(skill, input);
    case SkillBinding::ClaudeBuiltin:
      return dispatch_claude_builtin(skill, input);
    default:
      throw std::runtime_error("Unknown external skill binding type");
  }
}
}  // namespace neamc::vm
