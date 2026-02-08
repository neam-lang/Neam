//
// Neam LLM - Provider interface
//
// v0.6.6: Added tool-aware types (ToolDefinition, ToolCall, ChatResponse,
// ToolResult) and chat_with_tools() for native Claude/OpenAI function calling.
//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/vm/async/future.hpp"

namespace neamc::llm
{
struct Message
{
  std::string role;
  std::string content;
  nlohmann::json content_blocks{};  // For multi-block messages (tool_use / tool_result)
};

struct ProviderConfig
{
  std::string model;
  std::string endpoint;
  std::string api_key;
  double temperature{0.0};
  std::string default_host;
};

// --- Tool-aware types (v0.6.6) -----------------------------------------------

struct ToolDefinition
{
  std::string name;
  std::string description;
  nlohmann::json input_schema;  // JSON Schema from build_skill_schema()
  std::string type;             // v0.6.7: Non-empty for Claude built-in tools (e.g., "bash_20241022")
};

struct ToolCall
{
  std::string id;        // Provider-assigned ID (e.g., "toolu_01..." or "call_...")
  std::string name;      // Tool/skill name
  nlohmann::json input;  // Parsed arguments as JSON object
};

struct ChatResponse
{
  std::string text;         // Convenience: concatenated text blocks
  std::string stop_reason;  // "end_turn", "tool_use", "max_tokens"
  std::vector<ToolCall> tool_calls;

  bool has_tool_calls() const { return !tool_calls.empty(); }
};

struct ToolResult
{
  std::string tool_use_id;  // Matches ToolCall.id
  std::string content;      // Serialized result
  bool is_error{false};
};

// --- Provider interface ------------------------------------------------------

class LLMProvider
{
public:
  virtual ~LLMProvider() = default;
  virtual std::string complete(const std::string& prompt) = 0;
  virtual vm::async::Future<std::string> complete_async(const std::string& prompt) = 0;
  virtual std::string chat(const std::vector<Message>& messages) = 0;

  // Tool-aware chat (v0.6.6). Default falls back to plain chat for providers
  // that don't support native tool calling (e.g., older Ollama models).
  virtual ChatResponse chat_with_tools(const std::vector<Message>& messages,
                                       const std::vector<ToolDefinition>& tools,
                                       const std::string& tool_choice = "auto")
  {
    (void)tools;
    (void)tool_choice;
    ChatResponse resp;
    resp.text = chat(messages);
    resp.stop_reason = "end_turn";
    return resp;
  }
};
}  // namespace neamc::llm
