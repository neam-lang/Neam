// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - OpenAI adapter
//

#include "neamc/llm/openai_adapter.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"
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

ChatResult extract_chat_result(const nlohmann::json& json)
{
  ChatResult result;
  if (!json.contains("choices") || !json.at("choices").is_array() ||
      json.at("choices").empty())
  {
    throw std::runtime_error("OpenAI response missing choices");
  }
  const auto& choice = json.at("choices").front();
  if (!choice.contains("message"))
  {
    throw std::runtime_error("OpenAI response missing message");
  }
  const auto& message = choice.at("message");
  result.content = message.value("content", "");
  if (message.contains("tool_calls") && message.at("tool_calls").is_array())
  {
    for (const auto& tc : message.at("tool_calls"))
    {
      ToolCall call;
      call.id = tc.value("id", "");
      if (tc.contains("function"))
      {
        call.name = tc.at("function").value("name", "");
        call.arguments = tc.at("function").value("arguments", "{}");
      }
      result.tool_calls.push_back(std::move(call));
    }
  }
  // Extract usage info
  if (json.contains("usage"))
  {
    result.usage.prompt_tokens = json["usage"].value("prompt_tokens", std::size_t{0});
    result.usage.completion_tokens = json["usage"].value("completion_tokens", std::size_t{0});
    result.usage.total_tokens = json["usage"].value("total_tokens", std::size_t{0});
  }
  return result;
}

nlohmann::json message_list_to_json(const std::vector<Message>& messages)
{
  nlohmann::json json = nlohmann::json::array();
  for (const auto& message : messages)
  {
    nlohmann::json msg;
    msg["role"] = message.role;
    if (!message.content_parts.empty() && message.has_images())
    {
      // Multi-part content with images (vision)
      nlohmann::json parts = nlohmann::json::array();
      for (const auto& part : message.content_parts)
      {
        if (part.type == MessageContent::Type::kText)
        {
          parts.push_back({{"type", "text"}, {"text", part.text}});
        }
        else if (part.type == MessageContent::Type::kImageUrl)
        {
          parts.push_back({{"type", "image_url"},
                           {"image_url", {{"url", part.image_url}}}});
        }
        else if (part.type == MessageContent::Type::kImageBase64)
        {
          std::string data_url = "data:" + part.media_type + ";base64," + part.base64_data;
          parts.push_back({{"type", "image_url"},
                           {"image_url", {{"url", data_url}}}});
        }
      }
      msg["content"] = parts;
    }
    else if (!message.content.empty())
    {
      msg["content"] = message.content;
    }
    if (!message.tool_call_id.empty())
    {
      msg["tool_call_id"] = message.tool_call_id;
    }
    if (!message.name.empty())
    {
      msg["name"] = message.name;
    }
    json.push_back(std::move(msg));
  }
  return json;
}

nlohmann::json tools_to_json(const std::vector<ToolDefinition>& tools)
{
  nlohmann::json json = nlohmann::json::array();
  for (const auto& tool : tools)
  {
    nlohmann::json fn;
    fn["name"] = tool.name;
    fn["description"] = tool.description;
    fn["parameters"] = tool.parameters;
    json.push_back({{"type", "function"}, {"function", fn}});
  }
  return json;
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
    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["messages"] = message_list_to_json(messages);
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    const std::string response = http_post_json(
        config_.endpoint, payload.dump(),
        {"Content-Type: application/json",
         "Authorization: Bearer " + config_.api_key});
    return extract_response_text(nlohmann::json::parse(response));
  }

  ChatResult chat_with_tools(const std::vector<Message>& messages,
                             const std::vector<ToolDefinition>& tools,
                             const std::string& response_format = "") override
  {
    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["messages"] = message_list_to_json(messages);
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }
    if (!tools.empty())
    {
      payload["tools"] = tools_to_json(tools);
    }
    if (response_format == "json_object")
    {
      payload["response_format"] = {{"type", "json_object"}};
    }

    const std::string response = http_post_json(
        config_.endpoint, payload.dump(),
        {"Content-Type: application/json",
         "Authorization: Bearer " + config_.api_key});
    return extract_chat_result(nlohmann::json::parse(response));
  }

  std::string chat_stream(const std::vector<Message>& messages,
                          const TokenCallback& token_callback) override
  {
    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["messages"] = message_list_to_json(messages);
    payload["stream"] = true;
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    std::string accumulated;
    std::string sse_buffer;

    http_post_stream(
        config_.endpoint, payload.dump(),
        {"Content-Type: application/json",
         "Authorization: Bearer " + config_.api_key},
        [&](const std::string& chunk) -> bool {
          sse_buffer += chunk;
          // Parse SSE events from buffer
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
            // Remove trailing \r
            if (!line.empty() && line.back() == '\r')
            {
              line.pop_back();
            }
            if (line.rfind("data: ", 0) == 0)
            {
              std::string data = line.substr(6);
              if (data == "[DONE]")
              {
                sse_buffer.erase(0, pos);
                return true;
              }
              try
              {
                auto j = nlohmann::json::parse(data);
                if (j.contains("choices") && !j["choices"].empty())
                {
                  const auto& delta = j["choices"][0].value("delta", nlohmann::json::object());
                  std::string content = delta.value("content", "");
                  if (!content.empty())
                  {
                    accumulated += content;
                    if (token_callback)
                    {
                      token_callback(content);
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

std::unique_ptr<LLMProvider> create_openai_provider(const ProviderConfig& config)
{
  return std::make_unique<OpenAIAdapter>(config);
}
}  // namespace neamc::llm
