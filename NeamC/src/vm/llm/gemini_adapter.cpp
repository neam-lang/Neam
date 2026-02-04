// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Google Gemini adapter
//

#include "neamc/llm/gemini_adapter.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"
#include "neamc/vm/async/executor.hpp"

namespace neamc::llm
{
namespace
{
std::string build_generate_url(const std::string& model, const std::string& api_key,
                               bool streaming = false)
{
  std::string base = "https://generativelanguage.googleapis.com/v1beta/models/";
  base += model;
  if (streaming)
  {
    base += ":streamGenerateContent?alt=sse&key=";
  }
  else
  {
    base += ":generateContent?key=";
  }
  base += api_key;
  return base;
}

nlohmann::json messages_to_gemini_json(const std::vector<Message>& messages,
                                       std::string& system_prompt_out)
{
  nlohmann::json contents = nlohmann::json::array();
  for (const auto& message : messages)
  {
    if (message.role == "system")
    {
      system_prompt_out = message.content;
      continue;
    }

    nlohmann::json msg;
    // Gemini uses "model" instead of "assistant"
    if (message.role == "assistant")
    {
      msg["role"] = "model";
    }
    else if (message.role == "tool" || !message.tool_call_id.empty())
    {
      // Function response
      msg["role"] = "function";
      msg["parts"] = nlohmann::json::array(
          {{{"functionResponse",
             {{"name", message.name.empty() ? "tool" : message.name},
              {"response", {{"result", message.content}}}}}}});
      contents.push_back(std::move(msg));
      continue;
    }
    else
    {
      msg["role"] = message.role;
    }

    if (!message.content_parts.empty() && message.has_images())
    {
      // Multi-part content with images (vision)
      nlohmann::json parts = nlohmann::json::array();
      for (const auto& part : message.content_parts)
      {
        if (part.type == MessageContent::Type::kText)
        {
          parts.push_back({{"text", part.text}});
        }
        else if (part.type == MessageContent::Type::kImageBase64)
        {
          parts.push_back({{"inlineData", {{"mimeType", part.media_type},
                                            {"data", part.base64_data}}}});
        }
        else if (part.type == MessageContent::Type::kImageUrl)
        {
          parts.push_back({{"text", "[Image URL: " + part.image_url + "]"}});
        }
      }
      msg["parts"] = parts;
    }
    else
    {
      msg["parts"] = nlohmann::json::array({{{"text", message.content}}});
    }
    contents.push_back(std::move(msg));
  }
  return contents;
}

nlohmann::json tools_to_gemini_json(const std::vector<ToolDefinition>& tools)
{
  nlohmann::json function_declarations = nlohmann::json::array();
  for (const auto& tool : tools)
  {
    nlohmann::json fn;
    fn["name"] = tool.name;
    fn["description"] = tool.description;
    fn["parameters"] = tool.parameters;
    function_declarations.push_back(std::move(fn));
  }
  return nlohmann::json::array({{{"functionDeclarations", function_declarations}}});
}

std::string extract_gemini_text(const nlohmann::json& json)
{
  if (!json.contains("candidates") || !json.at("candidates").is_array() ||
      json.at("candidates").empty())
  {
    throw std::runtime_error("Gemini response missing candidates");
  }
  const auto& candidate = json.at("candidates").front();
  if (!candidate.contains("content") || !candidate.at("content").contains("parts"))
  {
    throw std::runtime_error("Gemini response missing content.parts");
  }
  for (const auto& part : candidate.at("content").at("parts"))
  {
    if (part.contains("text"))
    {
      return part.at("text").get<std::string>();
    }
  }
  return "";
}

ChatResult extract_gemini_result(const nlohmann::json& json)
{
  ChatResult result;
  if (!json.contains("candidates") || !json.at("candidates").is_array() ||
      json.at("candidates").empty())
  {
    throw std::runtime_error("Gemini response missing candidates");
  }
  const auto& candidate = json.at("candidates").front();
  if (!candidate.contains("content") || !candidate.at("content").contains("parts"))
  {
    throw std::runtime_error("Gemini response missing content.parts");
  }
  for (const auto& part : candidate.at("content").at("parts"))
  {
    if (part.contains("text"))
    {
      result.content += part.at("text").get<std::string>();
    }
    else if (part.contains("functionCall"))
    {
      ToolCall call;
      call.id = "gemini_call_" + std::to_string(result.tool_calls.size());
      call.name = part.at("functionCall").value("name", "");
      if (part.at("functionCall").contains("args"))
      {
        call.arguments = part.at("functionCall").at("args").dump();
      }
      else
      {
        call.arguments = "{}";
      }
      result.tool_calls.push_back(std::move(call));
    }
  }
  // Extract usage info
  if (json.contains("usageMetadata"))
  {
    result.usage.prompt_tokens = json["usageMetadata"].value("promptTokenCount", std::size_t{0});
    result.usage.completion_tokens = json["usageMetadata"].value("candidatesTokenCount", std::size_t{0});
    result.usage.total_tokens = json["usageMetadata"].value("totalTokenCount", std::size_t{0});
  }
  return result;
}
}  // namespace

class GeminiAdapter final : public LLMProvider
{
public:
  explicit GeminiAdapter(ProviderConfig config)
      : config_(std::move(config))
  {
    if (config_.model.empty())
    {
      throw std::runtime_error("Gemini model is required");
    }
    if (config_.api_key.empty())
    {
      throw std::runtime_error("Gemini API key is required");
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
    payload["contents"] = messages_to_gemini_json(messages, system_prompt);
    if (!system_prompt.empty())
    {
      payload["systemInstruction"] = {{"parts", nlohmann::json::array({{{"text", system_prompt}}})}};
    }
    if (config_.temperature > 0.0)
    {
      payload["generationConfig"] = {{"temperature", config_.temperature}};
    }

    const std::string url = build_generate_url(config_.model, config_.api_key);
    const std::string response = http_post_json(
        url, payload.dump(),
        {"Content-Type: application/json"});
    return extract_gemini_text(nlohmann::json::parse(response));
  }

  ChatResult chat_with_tools(const std::vector<Message>& messages,
                             const std::vector<ToolDefinition>& tools,
                             const std::string& response_format = "") override
  {
    std::string system_prompt;
    nlohmann::json payload;
    payload["contents"] = messages_to_gemini_json(messages, system_prompt);
    if (!system_prompt.empty())
    {
      payload["systemInstruction"] = {{"parts", nlohmann::json::array({{{"text", system_prompt}}})}};
    }
    if (config_.temperature > 0.0)
    {
      payload["generationConfig"] = {{"temperature", config_.temperature}};
    }
    if (!tools.empty())
    {
      payload["tools"] = tools_to_gemini_json(tools);
    }
    if (response_format == "json_object")
    {
      payload["generationConfig"]["responseMimeType"] = "application/json";
    }

    const std::string url = build_generate_url(config_.model, config_.api_key);
    const std::string response = http_post_json(
        url, payload.dump(),
        {"Content-Type: application/json"});
    return extract_gemini_result(nlohmann::json::parse(response));
  }

  std::string chat_stream(const std::vector<Message>& messages,
                          const TokenCallback& token_callback) override
  {
    std::string system_prompt;
    nlohmann::json payload;
    payload["contents"] = messages_to_gemini_json(messages, system_prompt);
    if (!system_prompt.empty())
    {
      payload["systemInstruction"] = {{"parts", nlohmann::json::array({{{"text", system_prompt}}})}};
    }
    if (config_.temperature > 0.0)
    {
      payload["generationConfig"] = {{"temperature", config_.temperature}};
    }

    const std::string url = build_generate_url(config_.model, config_.api_key, true);
    std::string accumulated;
    std::string sse_buffer;

    http_post_stream(
        url, payload.dump(),
        {"Content-Type: application/json"},
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
            if (line.rfind("data: ", 0) == 0)
            {
              std::string data = line.substr(6);
              try
              {
                auto j = nlohmann::json::parse(data);
                if (j.contains("candidates") && !j["candidates"].empty())
                {
                  const auto& candidate = j["candidates"][0];
                  if (candidate.contains("content") &&
                      candidate["content"].contains("parts"))
                  {
                    for (const auto& part : candidate["content"]["parts"])
                    {
                      if (part.contains("text"))
                      {
                        std::string text = part["text"].get<std::string>();
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

std::unique_ptr<LLMProvider> create_gemini_provider(const ProviderConfig& config)
{
  return std::make_unique<GeminiAdapter>(config);
}
}  // namespace neamc::llm
