// SPDX-License-Identifier: Apache-2.0
//
// Neam MCP - Registry implementation
//

#include "neamc/mcp/registry.hpp"

#include <stdexcept>

namespace neamc::mcp
{
void McpRegistry::add_server(const McpServerConfig& config)
{
  clients_[config.name] = std::make_unique<McpClient>(config);
}

void McpRegistry::connect_all()
{
  for (auto& [name, client] : clients_)
  {
    if (!client->is_connected())
    {
      client->connect();
    }
  }
}

void McpRegistry::disconnect_all()
{
  for (auto& [name, client] : clients_)
  {
    client->disconnect();
  }
  tool_cache_.clear();
}

std::vector<McpToolDef> McpRegistry::all_tools() const
{
  std::vector<McpToolDef> result;
  for (const auto& [name, client] : clients_)
  {
    if (!client->is_connected())
    {
      continue;
    }
    auto tools = server_tools(name);
    for (auto& t : tools)
    {
      // Prefix with server name
      t.name = name + "." + t.name;
      result.push_back(std::move(t));
    }
  }
  return result;
}

std::vector<McpToolDef> McpRegistry::server_tools(const std::string& server_name) const
{
  auto cache_it = tool_cache_.find(server_name);
  if (cache_it != tool_cache_.end())
  {
    return cache_it->second;
  }

  auto it = clients_.find(server_name);
  if (it == clients_.end() || !it->second->is_connected())
  {
    return {};
  }

  auto tools = it->second->list_tools();
  tool_cache_[server_name] = tools;
  return tools;
}

nlohmann::json McpRegistry::call_tool(const std::string& server_name,
                                      const std::string& tool_name,
                                      const nlohmann::json& arguments)
{
  auto it = clients_.find(server_name);
  if (it == clients_.end())
  {
    throw std::runtime_error("Unknown MCP server: " + server_name);
  }
  if (!it->second->is_connected())
  {
    throw std::runtime_error("MCP server not connected: " + server_name);
  }
  return it->second->call_tool(tool_name, arguments);
}

bool McpRegistry::has_server(const std::string& name) const
{
  return clients_.count(name) > 0;
}
}  // namespace neamc::mcp
