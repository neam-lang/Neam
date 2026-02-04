// SPDX-License-Identifier: Apache-2.0
//
// Neam MCP - SSE transport (HTTP-based)
//

#include "neamc/mcp/transport.hpp"

#include <mutex>
#include <queue>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"

namespace neamc::mcp
{
class SseTransport final : public Transport
{
public:
  SseTransport(const std::string& url, const std::vector<std::string>& headers)
      : url_(url)
      , headers_(headers)
      , open_(true)
  {
    // Add Content-Type for JSON-RPC
    bool has_content_type = false;
    for (const auto& h : headers_)
    {
      if (h.find("Content-Type") != std::string::npos)
      {
        has_content_type = true;
        break;
      }
    }
    if (!has_content_type)
    {
      headers_.push_back("Content-Type: application/json");
    }
  }

  void send(const std::string& data) override
  {
    if (!open_)
    {
      throw std::runtime_error("Transport is closed");
    }
    // POST JSON-RPC request and collect response
    std::string response = llm::http_post_json(url_, data, headers_);
    std::lock_guard<std::mutex> lock(mutex_);
    pending_responses_.push(std::move(response));
  }

  std::string receive() override
  {
    if (!open_)
    {
      throw std::runtime_error("Transport is closed");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (pending_responses_.empty())
    {
      return "{}";
    }
    std::string front = std::move(pending_responses_.front());
    pending_responses_.pop();
    return front;
  }

  void close() override
  {
    open_ = false;
  }

  bool is_open() const override { return open_; }

private:
  std::string url_;
  std::vector<std::string> headers_;
  bool open_{false};
  std::mutex mutex_;
  std::queue<std::string> pending_responses_;
};

std::unique_ptr<Transport> create_sse_transport(
    const std::string& url,
    const std::vector<std::string>& headers)
{
  return std::make_unique<SseTransport>(url, headers);
}
}  // namespace neamc::mcp
