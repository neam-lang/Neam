// SPDX-License-Identifier: Apache-2.0
//
// Neam MCP - Registry managing multiple MCP server connections
//

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/mcp/client.hpp"

namespace neamc::mcp
{
class McpRegistry
{
public:
  void add_server(const McpServerConfig& config);
  void connect_all();
  void disconnect_all();

  /// Returns all tools across all connected servers, prefixed as "server.tool"
  std::vector<McpToolDef> all_tools() const;

  /// Returns tools for a specific server
  std::vector<McpToolDef> server_tools(const std::string& server_name) const;

  /// Call a tool on a specific server
  nlohmann::json call_tool(const std::string& server_name, const std::string& tool_name,
                           const nlohmann::json& arguments);

  bool has_server(const std::string& name) const;

private:
  std::unordered_map<std::string, std::unique_ptr<McpClient>> clients_;
  // Cache tools per server after connect
  mutable std::unordered_map<std::string, std::vector<McpToolDef>> tool_cache_;
};
}  // namespace neamc::mcp
