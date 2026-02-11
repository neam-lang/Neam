//
// Neam LLM - OpenAI adapter
//
// v0.6.6: Added chat_with_tools() for OpenAI function calling protocol.
//

#include "neamc/llm/openai_adapter.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"
#include "neamc/llm/llm_logger.hpp"
#include "neamc/vm/async/executor.hpp"

namespace neamc::llm
{
namespace
{
std::string extract_response_text(const nlohmann::json& json)
{
  if (json.contains("choices") && json.at("choices").is_array() &&
      !json.at("choices").empty())
  {
    const auto& choice = json.at("choices").front();
    if (choice.contains("message"))
    {
      return choice.at("message").value("content", "");
    }
    if (choice.contains("text"))
    {
      return choice.at("text").get<std::string>();
    }
  }
  throw std::runtime_error("OpenAI response missing content");
}

// Build messages array for OpenAI Chat Completions API.
// Handles plain messages, assistant messages with tool_calls, and tool result messages.
nlohmann::json build_messages_json(const std::vector<Message>& messages)
{
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& msg : messages)
  {
    nlohmann::json entry;
    entry["role"] = msg.role;

    if (!msg.content_blocks.is_null() && msg.content_blocks.is_array() &&
        !msg.content_blocks.empty())
    {
      // Check if this is an assistant message with tool_calls (OpenAI format)
      if (msg.role == "assistant" && msg.content_blocks.front().contains("type") &&
          msg.content_blocks.front().at("type") == "function")
      {
        // Reconstruct assistant message with tool_calls array
        entry["content"] = msg.content.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.content);
        nlohmann::json tool_calls = nlohmann::json::array();
        for (const auto& block : msg.content_blocks)
        {
          nlohmann::json tc;
          tc["id"] = block.value("id", "");
          tc["type"] = "function";
          tc["function"] = {
              {"name", block.value("name", "")},
              {"arguments", block.value("arguments", "")}};
          tool_calls.push_back(std::move(tc));
        }
        entry["tool_calls"] = std::move(tool_calls);
      }
      // Check if this is a tool result message
      else if (msg.role == "tool")
      {
        entry["content"] = msg.content;
        if (msg.content_blocks.front().contains("tool_call_id"))
        {
          entry["tool_call_id"] = msg.content_blocks.front().at("tool_call_id");
        }
      }
      else
      {
        entry["content"] = msg.content;
      }
    }
    else
    {
      entry["content"] = msg.content;
    }
    arr.push_back(std::move(entry));
  }
  return arr;
}

// Parse OpenAI response into ChatResponse, detecting function calling tool_calls.
ChatResponse parse_openai_response(const nlohmann::json& parsed)
{
  ChatResponse resp;

  if (!parsed.contains("choices") || !parsed.at("choices").is_array() ||
      parsed.at("choices").empty())
  {
    throw std::runtime_error("OpenAI response missing choices");
  }

  const auto& choice = parsed.at("choices").front();
  resp.stop_reason = choice.value("finish_reason", "stop");

  if (choice.contains("message"))
  {
    const auto& message = choice.at("message");

    // Extract text content
    if (message.contains("content") && !message.at("content").is_null())
    {
      resp.text = message.at("content").get<std::string>();
    }

    // Extract tool calls (function calling)
    if (message.contains("tool_calls") && message.at("tool_calls").is_array())
    {
      for (const auto& tc : message.at("tool_calls"))
      {
        if (tc.value("type", "") != "function")
        {
          continue;
        }
        ToolCall tool_call;
        tool_call.id = tc.value("id", "");
        if (tc.contains("function"))
        {
          const auto& fn = tc.at("function");
          tool_call.name = fn.value("name", "");
          const std::string args_str = fn.value("arguments", "{}");
          try
          {
            tool_call.input = nlohmann::json::parse(args_str);
          }
          catch (...)
          {
            tool_call.input = nlohmann::json::object();
          }
        }
        resp.tool_calls.push_back(std::move(tool_call));
      }
    }
  }

  // Normalize stop_reason: OpenAI uses "tool_calls", Claude uses "tool_use"
  if (resp.stop_reason == "tool_calls")
  {
    resp.stop_reason = "tool_use";
  }

  return resp;
}
}  // namespace

class OpenAIAdapter final : public LLMProvider
{
public:
  explicit OpenAIAdapter(ProviderConfig config)
      : config_(std::move(config))
  {
    if (config_.model.empty())
    {
      throw std::runtime_error("OpenAI model is required");
    }
    if (config_.endpoint.empty())
    {
      throw std::runtime_error("OpenAI endpoint is required");
    }
    if (config_.api_key.empty())
    {
      throw std::runtime_error("OpenAI API key is required");
    }
  }

  std::string complete(const std::string& prompt) override
  {
    std::vector<Message> messages;
    messages.push_back({"user", prompt});
    return chat(messages);
  }

  vm::async::Future<std::string> complete_async(const std::string& prompt) override
  {
    return vm::async::Executor::global().submit([this, prompt]() { return complete(prompt); });
  }

  std::string chat(const std::vector<Message>& messages) override
  {
    LLMLogger::info("openai", "Chat request: model=" + config_.model +
                                  ", messages=" + std::to_string(messages.size()));

    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["messages"] = build_messages_json(messages);
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    try
    {
      const std::string response = http_post_json(
          config_.endpoint, payload.dump(),
          {"Content-Type: application/json",
           "Authorization: Bearer " + config_.api_key});
      auto parsed = nlohmann::json::parse(response);
      auto text = extract_response_text(parsed);
      LLMLogger::debug("openai", "Response: " + std::to_string(text.size()) + " chars");
      return text;
    }
    catch (const std::exception& e)
    {
      LLMLogger::error("openai", "Chat failed: " + std::string(e.what()));
      throw;
    }
  }

  // v0.6.6: OpenAI function calling via Chat Completions API
  ChatResponse chat_with_tools(const std::vector<Message>& messages,
                               const std::vector<ToolDefinition>& tools,
                               const std::string& tool_choice) override
  {
    if (tools.empty())
    {
      ChatResponse resp;
      resp.text = chat(messages);
      resp.stop_reason = "end_turn";
      return resp;
    }

    LLMLogger::info("openai", "Tool-aware chat: model=" + config_.model +
                                    ", tools=" + std::to_string(tools.size()) +
                                    ", messages=" + std::to_string(messages.size()));

    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["messages"] = build_messages_json(messages);
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    // Build tools array in OpenAI format
    nlohmann::json tools_json = nlohmann::json::array();
    for (const auto& tool : tools)
    {
      tools_json.push_back(
          {{"type", "function"},
           {"function",
            {{"name", tool.name},
             {"description", tool.description},
             {"parameters", tool.input_schema}}}});
    }
    payload["tools"] = std::move(tools_json);

    // Tool choice
    if (tool_choice == "any")
    {
      payload["tool_choice"] = "required";
    }
    else if (tool_choice == "none")
    {
      payload["tool_choice"] = "none";
    }
    else if (tool_choice != "auto" && !tool_choice.empty())
    {
      payload["tool_choice"] = {{"type", "function"}, {"function", {{"name", tool_choice}}}};
    }
    else
    {
      payload["tool_choice"] = "auto";
    }

    try
    {
      const std::string response = http_post_json(
          config_.endpoint, payload.dump(),
          {"Content-Type: application/json",
           "Authorization: Bearer " + config_.api_key});
      auto parsed = nlohmann::json::parse(response);
      auto chat_resp = parse_openai_response(parsed);
      LLMLogger::debug("openai", "Tool response: stop_reason=" + chat_resp.stop_reason +
                                       ", tool_calls=" + std::to_string(chat_resp.tool_calls.size()));
      return chat_resp;
    }
    catch (const std::exception& e)
    {
      LLMLogger::error("openai", "Tool chat failed: " + std::string(e.what()));
      throw;
    }
  }

  // v0.6.8: SSE streaming for OpenAI
  void chat_stream(const std::vector<Message>& messages, StreamCallback callback) override
  {
    LLMLogger::info("openai", "Stream request: model=" + config_.model);

    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["messages"] = build_messages_json(messages);
    payload["stream"] = true;
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    std::string buffer;
    http_post_streaming(
        config_.endpoint, payload.dump(),
        {"Content-Type: application/json",
         "Authorization: Bearer " + config_.api_key},
        [&](const char* data, size_t size) {
          buffer.append(data, size);

          // Parse SSE lines: "data: {...}\n"
          size_t pos = 0;
          while (pos < buffer.size())
          {
            size_t nl = buffer.find('\n', pos);
            if (nl == std::string::npos)
            {
              break;
            }
            std::string line = buffer.substr(pos, nl - pos);
            pos = nl + 1;

            if (line.empty() || line == "\r")
            {
              continue;
            }
            // Remove trailing \r
            if (!line.empty() && line.back() == '\r')
            {
              line.pop_back();
            }

            if (line.rfind("data: ", 0) != 0)
            {
              continue;
            }
            std::string json_str = line.substr(6);
            if (json_str == "[DONE]")
            {
              callback("", true);
              break;
            }

            try
            {
              auto parsed = nlohmann::json::parse(json_str);
              if (parsed.contains("choices") && !parsed["choices"].empty())
              {
                const auto& delta = parsed["choices"][0].value("delta", nlohmann::json::object());
                if (delta.contains("content") && !delta["content"].is_null())
                {
                  std::string chunk = delta["content"].get<std::string>();
                  if (!chunk.empty())
                  {
                    callback(chunk, false);
                  }
                }
              }
            }
            catch (const nlohmann::json::parse_error&)
            {
              // Skip malformed SSE lines
            }
          }
          buffer = buffer.substr(pos);
        },
        120000);
  }

private:
  ProviderConfig config_;
};

std::unique_ptr<LLMProvider> create_openai_provider(const ProviderConfig& config)
{
  return std::make_unique<OpenAIAdapter>(config);
}
}  // namespace neamc::llm
