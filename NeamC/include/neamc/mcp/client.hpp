// SPDX-License-Identifier: Apache-2.0
//
// Neam MCP - Client for connecting to MCP servers
//

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/mcp/transport.hpp"

namespace neamc::mcp
{
enum class TransportType
{
  Stdio,
  SSE
};

struct McpServerConfig
{
  std::string name;
  std::string command;                                    // For stdio transport
  std::vector<std::string> args;                          // For stdio transport
  std::string url;                                        // For SSE transport
  std::unordered_map<std::string, std::string> env;       // Environment variables
  TransportType transport_type{TransportType::Stdio};
};

struct McpToolDef
{
  std::string name;
  std::string description;
  nlohmann::json input_schema;
};

class McpClient
{
public:
  explicit McpClient(McpServerConfig config);
  ~McpClient();

  McpClient(const McpClient&) = delete;
  McpClient& operator=(const McpClient&) = delete;

  void connect();
  void disconnect();
  bool is_connected() const;

  std::vector<McpToolDef> list_tools();
  nlohmann::json call_tool(const std::string& name, const nlohmann::json& arguments);

  const std::string& server_name() const { return config_.name; }

private:
  nlohmann::json send_request(const std::string& method,
                              const nlohmann::json& params = nlohmann::json::object());

  McpServerConfig config_;
  std::unique_ptr<Transport> transport_;
  int64_t next_id_{1};
  bool connected_{false};
};
}  // namespace neamc::mcp
