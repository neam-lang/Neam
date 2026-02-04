// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - WebSocket client for real-time streaming
//

#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace neamc::llm
{
class WebSocketClient
{
public:
  using MessageCallback = std::function<void(const std::string& message)>;
  using BinaryCallback = std::function<void(const std::vector<uint8_t>& data)>;
  using ErrorCallback = std::function<void(const std::string& error)>;
  using CloseCallback = std::function<void(int code, const std::string& reason)>;

  WebSocketClient();
  ~WebSocketClient();

  WebSocketClient(const WebSocketClient&) = delete;
  WebSocketClient& operator=(const WebSocketClient&) = delete;

  void connect(const std::string& url, const std::vector<std::string>& headers = {});
  void send_text(const std::string& message);
  void send_binary(const std::vector<uint8_t>& data);
  void close(int code = 1000, const std::string& reason = "");
  bool is_connected() const;

  void set_on_message(MessageCallback cb);
  void set_on_binary(BinaryCallback cb);
  void set_on_error(ErrorCallback cb);
  void set_on_close(CloseCallback cb);

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace neamc::llm
