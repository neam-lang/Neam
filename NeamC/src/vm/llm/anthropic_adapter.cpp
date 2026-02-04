// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Anthropic Claude adapter
//

#include "neamc/llm/anthropic_adapter.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"
#include "neamc/vm/async/executor.hpp"

namespace neamc::llm
{
namespace
{
nlohmann::json messages_to_anthropic_json(const std::vector<Message>& messages,
                                          std::string& system_prompt_out)
{
  nlohmann::json json = nlohmann::json::array();
  for (const auto& message : messages)
  {
    if (message.role == "system")
    {
      system_prompt_out = message.content;
      continue;
    }
    nlohmann::json msg;
    msg["role"] = message.role;

    if (!message.tool_call_id.empty())
    {
      // Tool result message
      msg["role"] = "user";
      msg["content"] = nlohmann::json::array(
          {{{"type", "tool_result"},
            {"tool_use_id", message.tool_call_id},
            {"content", message.content}}});
    }
    else if (!message.content_parts.empty() && message.has_images())
    {
      // Multi-part content with images (vision)
      nlohmann::json parts = nlohmann::json::array();
      for (const auto& part : message.content_parts)
      {
        if (part.type == MessageContent::Type::kText)
        {
          parts.push_back({{"type", "text"}, {"text", part.text}});
        }
        else if (part.type == MessageContent::Type::kImageBase64)
        {
          parts.push_back({{"type", "image"},
                           {"source", {{"type", "base64"},
                                       {"media_type", part.media_type},
                                       {"data", part.base64_data}}}});
        }
        else if (part.type == MessageContent::Type::kImageUrl)
        {
          // Anthropic prefers base64; for URLs, pass as text reference
          parts.push_back({{"type", "text"},
                           {"text", "[Image URL: " + part.image_url + "]"}});
        }
      }
      msg["content"] = parts;
    }
    else
    {
      msg["content"] = message.content;
    }
    json.push_back(std::move(msg));
  }
  return json;
}

nlohmann::json tools_to_anthropic_json(const std::vector<ToolDefinition>& tools)
{
  nlohmann::json json = nlohmann::json::array();
  for (const auto& tool : tools)
  {
    nlohmann::json t;
    t["name"] = tool.name;
    t["description"] = tool.description;
    t["input_schema"] = tool.parameters;
    json.push_back(std::move(t));
  }
  return json;
}

std::string extract_anthropic_text(const nlohmann::json& json)
{
  if (!json.contains("content") || !json.at("content").is_array())
  {
    throw std::runtime_error("Anthropic response missing content array");
  }
  for (const auto& block : json.at("content"))
  {
    if (block.value("type", "") == "text")
    {
      return block.value("text", "");
    }
  }
  return "";
}

ChatResult extract_anthropic_result(const nlohmann::json& json)
{
  ChatResult result;
  if (!json.contains("content") || !json.at("content").is_array())
  {
    throw std::runtime_error("Anthropic response missing content array");
  }
  for (const auto& block : json.at("content"))
  {
    const std::string type = block.value("type", "");
    if (type == "text")
    {
      result.content += block.value("text", "");
    }
    else if (type == "tool_use")
    {
      ToolCall call;
      call.id = block.value("id", "");
      call.name = block.value("name", "");
      if (block.contains("input"))
      {
        call.arguments = block.at("input").dump();
      }
      else
      {
        call.arguments = "{}";
      }
      result.tool_calls.push_back(std::move(call));
    }
  }
  // Extract usage info
  if (json.contains("usage"))
  {
    result.usage.prompt_tokens = json["usage"].value("input_tokens", std::size_t{0});
    result.usage.completion_tokens = json["usage"].value("output_tokens", std::size_t{0});
    result.usage.total_tokens = result.usage.prompt_tokens + result.usage.completion_tokens;
  }
  return result;
}
}  // namespace

class AnthropicAdapter final : public LLMProvider
{
public:
  explicit AnthropicAdapter(ProviderConfig config)
      : config_(std::move(config))
  {
    if (config_.model.empty())
    {
      throw std::runtime_error("Anthropic model is required");
    }
    if (config_.endpoint.empty())
    {
      config_.endpoint = "https://api.anthropic.com/v1/messages";
    }
    if (config_.api_key.empty())
    {
      throw std::runtime_error("Anthropic API key is required");
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
    std::string system_prompt;
    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["max_tokens"] = 4096;
    payload["messages"] = messages_to_anthropic_json(messages, system_prompt);
    if (!system_prompt.empty())
    {
      payload["system"] = system_prompt;
    }
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    const std::string response = http_post_json(
        config_.endpoint, payload.dump(),
        {"Content-Type: application/json",
         "x-api-key: " + config_.api_key,
         "anthropic-version: 2023-06-01"});
    return extract_anthropic_text(nlohmann::json::parse(response));
  }

  ChatResult chat_with_tools(const std::vector<Message>& messages,
                             const std::vector<ToolDefinition>& tools,
                             const std::string& response_format = "") override
  {
    std::string system_prompt;
    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["max_tokens"] = 4096;
    payload["messages"] = messages_to_anthropic_json(messages, system_prompt);
    if (!system_prompt.empty())
    {
      payload["system"] = system_prompt;
    }
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }
    if (!tools.empty())
    {
      payload["tools"] = tools_to_anthropic_json(tools);
    }
    if (response_format == "json_object")
    {
      // Anthropic doesn't have a native json_object mode;
      // append instruction to system prompt
      if (payload.contains("system"))
      {
        payload["system"] =
            payload["system"].get<std::string>() + "\n\nRespond with valid JSON only.";
      }
      else
      {
        payload["system"] = "Respond with valid JSON only.";
      }
    }

    const std::string response = http_post_json(
        config_.endpoint, payload.dump(),
        {"Content-Type: application/json",
         "x-api-key: " + config_.api_key,
         "anthropic-version: 2023-06-01"});
    return extract_anthropic_result(nlohmann::json::parse(response));
  }

  std::string chat_stream(const std::vector<Message>& messages,
                          const TokenCallback& token_callback) override
  {
    std::string system_prompt;
    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["max_tokens"] = 4096;
    payload["messages"] = messages_to_anthropic_json(messages, system_prompt);
    payload["stream"] = true;
    if (!system_prompt.empty())
    {
      payload["system"] = system_prompt;
    }
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    std::string accumulated;
    std::string sse_buffer;

    http_post_stream(
        config_.endpoint, payload.dump(),
        {"Content-Type: application/json",
         "x-api-key: " + config_.api_key,
         "anthropic-version: 2023-06-01"},
        [&](const std::string& chunk) -> bool {
          sse_buffer += chunk;
          std::size_t pos = 0;
          while (true)
          {
            auto nl = sse_buffer.find('\n', pos);
            if (nl == std::string::npos)
            {
              break;
            }
            std::string line = sse_buffer.substr(pos, nl - pos);
            pos = nl + 1;
            if (!line.empty() && line.back() == '\r')
            {
              line.pop_back();
            }

            // Check for event type
            if (line.rfind("event: ", 0) == 0)
            {
              std::string event_type = line.substr(7);
              if (event_type == "message_stop")
              {
                sse_buffer.erase(0, pos);
                return true;
              }
              continue;
            }

            if (line.rfind("data: ", 0) == 0)
            {
              std::string data = line.substr(6);
              try
              {
                auto j = nlohmann::json::parse(data);
                const std::string type = j.value("type", "");
                if (type == "content_block_delta")
                {
                  const auto& delta = j.value("delta", nlohmann::json::object());
                  if (delta.value("type", "") == "text_delta")
                  {
                    std::string text = delta.value("text", "");
                    if (!text.empty())
                    {
                      accumulated += text;
                      if (token_callback)
                      {
                        token_callback(text);
                      }
                    }
                  }
                }
              }
              catch (...)
              {
                // Skip malformed SSE chunks
              }
            }
          }
          sse_buffer.erase(0, pos);
          return true;
        });

    return accumulated;
  }

private:
  ProviderConfig config_;
};

std::unique_ptr<LLMProvider> create_anthropic_provider(const ProviderConfig& config)
{
  return std::make_unique<AnthropicAdapter>(config);
}
}  // namespace neamc::llm
