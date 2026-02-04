// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Provider interface
//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/vm/async/future.hpp"

namespace neamc::llm
{

// Multi-part content for vision support
struct MessageContent
{
  enum class Type
  {
    kText,
    kImageUrl,
    kImageBase64
  };
  Type type{Type::kText};
  std::string text;
  std::string image_url;
  std::string base64_data;
  std::string media_type;  // "image/png", "image/jpeg", etc.
};

struct Message
{
  std::string role;
  std::string content;
  std::string tool_call_id;  // For tool result messages
  std::string name;          // For tool result messages
  std::vector<MessageContent> content_parts;  // Multi-modal content (images + text)

  bool has_images() const
  {
    for (const auto& part : content_parts)
    {
      if (part.type == MessageContent::Type::kImageUrl ||
          part.type == MessageContent::Type::kImageBase64)
      {
        return true;
      }
    }
    return false;
  }
};

struct ToolDefinition
{
  std::string name;
  std::string description;
  nlohmann::json parameters;  // JSON Schema for parameters
};

struct ToolCall
{
  std::string id;
  std::string name;
  std::string arguments;  // JSON string of arguments
};

struct UsageInfo
{
  std::size_t prompt_tokens{0};
  std::size_t completion_tokens{0};
  std::size_t total_tokens{0};
};

struct ChatResult
{
  std::string content;
  std::vector<ToolCall> tool_calls;
  UsageInfo usage;
  bool has_tool_calls() const { return !tool_calls.empty(); }
};

struct ProviderConfig
{
  std::string model;
  std::string endpoint;
  std::string api_key;
  double temperature{0.0};
  std::string default_host;
};

class LLMProvider
{
public:
  virtual ~LLMProvider() = default;
  virtual std::string complete(const std::string& prompt) = 0;
  virtual vm::async::Future<std::string> complete_async(const std::string& prompt) = 0;
  virtual std::string chat(const std::vector<Message>& messages) = 0;

  /// Chat with tool definitions. Returns structured result with possible tool calls.
  /// Default implementation falls back to plain chat().
  /// response_format: empty = text, "json_object" = JSON mode
  virtual ChatResult chat_with_tools(const std::vector<Message>& messages,
                                     const std::vector<ToolDefinition>& tools,
                                     const std::string& response_format = "")
  {
    (void)tools;
    (void)response_format;
    ChatResult result;
    result.content = chat(messages);
    return result;
  }
  /// Stream callback: receives each text token as it arrives.
  using TokenCallback = std::function<void(const std::string& token)>;

  /// Chat with streaming. Calls token_callback for each token.
  /// Returns the full accumulated response.
  /// Default implementation falls back to non-streaming chat().
  virtual std::string chat_stream(const std::vector<Message>& messages,
                                  const TokenCallback& token_callback)
  {
    auto result = chat(messages);
    if (token_callback)
    {
      token_callback(result);
    }
    return result;
  }
};
}  // namespace neamc::llm
