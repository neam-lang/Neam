// SPDX-License-Identifier: Apache-2.0
//
// Neam MCP - Client implementation
//

#include "neamc/mcp/client.hpp"

#include <stdexcept>

#include "neamc/mcp/jsonrpc.hpp"

namespace neamc::mcp
{
McpClient::McpClient(McpServerConfig config)
    : config_(std::move(config))
{
}

McpClient::~McpClient()
{
  disconnect();
}

void McpClient::connect()
{
  if (connected_)
  {
    return;
  }

  // Create transport
  if (config_.transport_type == TransportType::Stdio)
  {
    transport_ = create_stdio_transport(config_.command, config_.args, config_.env);
  }
  else
  {
    std::vector<std::string> headers;
    transport_ = create_sse_transport(config_.url, headers);
  }

  // Send initialize request
  nlohmann::json init_params;
  init_params["protocolVersion"] = "2024-11-05";
  init_params["capabilities"] = nlohmann::json::object();
  init_params["clientInfo"] = {{"name", "neam"}, {"version", "0.6.0"}};

  auto result = send_request("initialize", init_params);
  // We don't strictly validate the server response; just proceed

  // Send initialized notification
  std::string notification = encode_notification("notifications/initialized");
  transport_->send(notification);

  connected_ = true;
}

void McpClient::disconnect()
{
  if (!connected_ || !transport_)
  {
    return;
  }
  connected_ = false;
  transport_->close();
  transport_.reset();
}

bool McpClient::is_connected() const
{
  return connected_ && transport_ && transport_->is_open();
}

std::vector<McpToolDef> McpClient::list_tools()
{
  if (!is_connected())
  {
    throw std::runtime_error("MCP client not connected: " + config_.name);
  }

  auto result = send_request("tools/list");
  std::vector<McpToolDef> tools;

  if (result.contains("tools") && result.at("tools").is_array())
  {
    for (const auto& t : result.at("tools"))
    {
      McpToolDef tool;
      tool.name = t.value("name", "");
      tool.description = t.value("description", "");
      if (t.contains("inputSchema"))
      {
        tool.input_schema = t.at("inputSchema");
      }
      tools.push_back(std::move(tool));
    }
  }
  return tools;
}

nlohmann::json McpClient::call_tool(const std::string& name, const nlohmann::json& arguments)
{
  if (!is_connected())
  {
    throw std::runtime_error("MCP client not connected: " + config_.name);
  }

  nlohmann::json params;
  params["name"] = name;
  params["arguments"] = arguments;

  auto result = send_request("tools/call", params);

  // Extract content from result
  if (result.contains("content") && result.at("content").is_array())
  {
    for (const auto& block : result.at("content"))
    {
      if (block.value("type", "") == "text")
      {
        return block.value("text", "");
      }
    }
  }
  return result;
}

nlohmann::json McpClient::send_request(const std::string& method,
                                       const nlohmann::json& params)
{
  JsonRpcRequest request;
  request.id = next_id_++;
  request.method = method;
  request.params = params;

  std::string encoded = encode_request(request);
  transport_->send(encoded);

  std::string response_str = transport_->receive();
  auto response = decode_response(response_str);

  if (response.is_error())
  {
    throw std::runtime_error("MCP error (" + config_.name + "): " + response.error_message);
  }

  return response.result;
}
}  // namespace neamc::mcp
