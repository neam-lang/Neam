// SPDX-License-Identifier: Apache-2.0
//
// Neam MCP - JSON-RPC 2.0 implementation
//

#include "neamc/mcp/jsonrpc.hpp"

namespace neamc::mcp
{
std::string encode_request(const JsonRpcRequest& request)
{
  nlohmann::json j;
  j["jsonrpc"] = "2.0";
  j["id"] = request.id;
  j["method"] = request.method;
  if (!request.params.is_null())
  {
    j["params"] = request.params;
  }
  return j.dump();
}

std::string encode_notification(const std::string& method, const nlohmann::json& params)
{
  nlohmann::json j;
  j["jsonrpc"] = "2.0";
  j["method"] = method;
  if (!params.is_null() && !params.empty())
  {
    j["params"] = params;
  }
  return j.dump();
}

JsonRpcResponse decode_response(const std::string& data)
{
  JsonRpcResponse response;
  auto j = nlohmann::json::parse(data);
  response.id = j.value("id", static_cast<int64_t>(0));

  if (j.contains("error"))
  {
    const auto& err = j.at("error");
    response.error_code = err.value("code", 0);
    response.error_message = err.value("message", "Unknown error");
  }
  else if (j.contains("result"))
  {
    response.result = j.at("result");
  }
  return response;
}
}  // namespace neamc::mcp
