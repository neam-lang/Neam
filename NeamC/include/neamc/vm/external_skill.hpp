//
// Neam Virtual Machine - External skill bindings (v0.6.7)
//
// Allows skills to be bound to external sources: MCP servers,
// HTTP APIs, and Claude built-in tools.
//

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json_fwd.hpp>

namespace neamc::vm
{
struct ObjSkill;
enum class SkillBinding : uint8_t
{
  Local = 0,       // impl -> ObjFunction* (existing, unchanged)
  McpTool,         // Dispatch via MCP client (JSON-RPC tools/call)
  HttpApi,         // Dispatch via HTTP request template
  ClaudeBuiltin,   // Pass as Anthropic built-in tool to API
};

struct ExternalSkillConfig
{
  SkillBinding binding{SkillBinding::Local};

  // MCP
  std::string mcp_server_name;
  std::string mcp_tool_name;

  // HTTP
  std::string http_method;         // GET, POST, PUT, DELETE
  std::string http_url_template;   // URL with {param} placeholders
  std::string http_body_template;
  std::vector<std::pair<std::string, std::string>> http_headers;
  std::string http_response_path;  // JSON pointer for result extraction
  long http_timeout_ms{30000};

  // Claude built-in
  std::string claude_tool_type;    // "computer_20241022", "bash_20241022", etc.
};

class McpClient;  // Forward declaration

// Dispatch an external skill call and return the result as a string.
std::string dispatch_external_skill(ObjSkill* skill, const nlohmann::json& input,
                                    McpClient* mcp_client = nullptr);
}  // namespace neamc::vm
