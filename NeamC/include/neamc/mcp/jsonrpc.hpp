// SPDX-License-Identifier: Apache-2.0
//
// Neam MCP - JSON-RPC 2.0 protocol
//

#pragma once

#include <cstdint>
#include <string>

#include <nlohmann/json.hpp>

namespace neamc::mcp
{
struct JsonRpcRequest
{
  int64_t id{0};
  std::string method;
  nlohmann::json params;
};

struct JsonRpcResponse
{
  int64_t id{0};
  nlohmann::json result;
  std::string error_message;
  int error_code{0};

  bool is_error() const { return !error_message.empty() || error_code != 0; }
};

std::string encode_request(const JsonRpcRequest& request);
std::string encode_notification(const std::string& method,
                                const nlohmann::json& params = nlohmann::json::object());
JsonRpcResponse decode_response(const std::string& data);
}  // namespace neamc::mcp
