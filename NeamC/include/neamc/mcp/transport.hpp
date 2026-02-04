// SPDX-License-Identifier: Apache-2.0
//
// Neam MCP - Transport abstraction
//

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace neamc::mcp
{
class Transport
{
public:
  virtual ~Transport() = default;
  virtual void send(const std::string& data) = 0;
  virtual std::string receive() = 0;
  virtual void close() = 0;
  virtual bool is_open() const = 0;
};

std::unique_ptr<Transport> create_stdio_transport(
    const std::string& command,
    const std::vector<std::string>& args = {},
    const std::unordered_map<std::string, std::string>& env = {});

std::unique_ptr<Transport> create_sse_transport(
    const std::string& url,
    const std::vector<std::string>& headers = {});
}  // namespace neamc::mcp
