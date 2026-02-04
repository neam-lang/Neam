// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Ollama adapter
//

#include "neamc/llm/ollama_adapter.hpp"

#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"
#include "neamc/vm/async/executor.hpp"

namespace neamc::llm
{
namespace
{
std::string ensure_scheme(std::string host)
{
  if (host.rfind("http://", 0) == 0 || host.rfind("https://", 0) == 0)
  {
    return host;
  }
  return "http://" + host;
}

std::string trim_trailing_slash(std::string value)
{
  while (!value.empty() && value.back() == '/')
  {
    value.pop_back();
  }
  return value;
}

std::string build_url(const std::string& base, const std::string& path)
{
  return trim_trailing_slash(base) + path;
}

std::string extract_response_text(const nlohmann::json& json)
{
  if (json.contains("message"))
  {
    const auto& message = json.at("message");
    if (message.is_object() && message.contains("content"))
    {
      return message.at("content").get<std::string>();
    }
  }
  if (json.contains("response"))
  {
    return json.at("response").get<std::string>();
  }
  if (json.contains("choices") && json.at("choices").is_array() &&
      !json.at("choices").empty())
  {
    const auto& choice = json.at("choices").front();
    if (choice.contains("message"))
    {
      return choice.at("message").at("content").get<std::string>();
    }
    if (choice.contains("text"))
    {
      return choice.at("text").get<std::string>();
    }
  }
  throw std::runtime_error("Ollama response missing content");
}

nlohmann::json message_list_to_json(const std::vector<Message>& messages)
{
  nlohmann::json json = nlohmann::json::array();
  for (const auto& message : messages)
  {
    nlohmann::json msg;
    msg["role"] = message.role;
    if (!message.content.empty())
    {
      msg["content"] = message.content;
    }
    // Ollama vision: add images array for base64 image data
    if (!message.content_parts.empty() && message.has_images())
    {
      nlohmann::json images = nlohmann::json::array();
      for (const auto& part : message.content_parts)
      {
        if (part.type == MessageContent::Type::kText && msg["content"].empty())
        {
          msg["content"] = part.text;
        }
        else if (part.type == MessageContent::Type::kImageBase64)
        {
          images.push_back(part.base64_data);
        }
      }
      if (!images.empty())
      {
        msg["images"] = images;
      }
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

ChatResult extract_chat_result_ollama(const nlohmann::json& json)
{
  ChatResult result;
  if (json.contains("message"))
  {
    const auto& message = json.at("message");
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
          const auto& args = tc.at("function").value("arguments", nlohmann::json::object());
          call.arguments = args.is_string() ? args.get<std::string>() : args.dump();
        }
        result.tool_calls.push_back(std::move(call));
      }
    }
  }
  else if (json.contains("choices") && json.at("choices").is_array() &&
           !json.at("choices").empty())
  {
    const auto& choice = json.at("choices").front();
    if (choice.contains("message"))
    {
      const auto& msg = choice.at("message");
      result.content = msg.value("content", "");
      if (msg.contains("tool_calls") && msg.at("tool_calls").is_array())
      {
        for (const auto& tc : msg.at("tool_calls"))
        {
          ToolCall call;
          call.id = tc.value("id", "");
          if (tc.contains("function"))
          {
            call.name = tc.at("function").value("name", "");
            const auto& args = tc.at("function").value("arguments", nlohmann::json::object());
            call.arguments = args.is_string() ? args.get<std::string>() : args.dump();
          }
          result.tool_calls.push_back(std::move(call));
        }
      }
    }
  }
  // Extract usage info (Ollama native format)
  if (json.contains("prompt_eval_count"))
  {
    result.usage.prompt_tokens = json.value("prompt_eval_count", std::size_t{0});
  }
  if (json.contains("eval_count"))
  {
    result.usage.completion_tokens = json.value("eval_count", std::size_t{0});
  }
  result.usage.total_tokens = result.usage.prompt_tokens + result.usage.completion_tokens;
  // Also check OpenAI-compatible format
  if (json.contains("usage"))
  {
    result.usage.prompt_tokens = json["usage"].value("prompt_tokens", std::size_t{0});
    result.usage.completion_tokens = json["usage"].value("completion_tokens", std::size_t{0});
    result.usage.total_tokens = json["usage"].value("total_tokens", std::size_t{0});
  }
  return result;
}

nlohmann::json tools_to_json_ollama(const std::vector<ToolDefinition>& tools)
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

class OllamaAdapter final : public LLMProvider
{
public:
  explicit OllamaAdapter(ProviderConfig config)
      : config_(std::move(config))
  {
    if (config_.model.empty())
    {
      throw std::runtime_error("Ollama model is required");
    }
    if (config_.endpoint.empty())
    {
      const std::string fallback =
          config_.default_host.empty() ? "http://localhost:11434" : config_.default_host;
      config_.endpoint = fallback;
    }
    config_.endpoint = ensure_scheme(config_.endpoint);
  }

  std::string complete(const std::string& prompt) override
  {
    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["prompt"] = prompt;
    payload["stream"] = false;
    if (config_.temperature > 0.0)
    {
      payload["options"] = { {"temperature", config_.temperature} };
    }

    const std::string response = http_post_json(
        build_url(config_.endpoint, "/api/generate"), payload.dump(),
        {"Content-Type: application/json"});
    return extract_response_text(nlohmann::json::parse(response));
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
    payload["stream"] = false;
    if (config_.temperature > 0.0)
    {
      payload["options"] = { {"temperature", config_.temperature} };
    }

    const std::string response = http_post_json(
        build_url(config_.endpoint, "/api/chat"), payload.dump(),
        {"Content-Type: application/json"});
    return extract_response_text(nlohmann::json::parse(response));
  }

  ChatResult chat_with_tools(const std::vector<Message>& messages,
                             const std::vector<ToolDefinition>& tools,
                             const std::string& response_format = "") override
  {
    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["messages"] = message_list_to_json(messages);
    payload["stream"] = false;
    if (config_.temperature > 0.0)
    {
      payload["options"] = { {"temperature", config_.temperature} };
    }
    if (!tools.empty())
    {
      payload["tools"] = tools_to_json_ollama(tools);
    }
    if (response_format == "json_object")
    {
      payload["format"] = "json";
    }

    const std::string response = http_post_json(
        build_url(config_.endpoint, "/api/chat"), payload.dump(),
        {"Content-Type: application/json"});
    return extract_chat_result_ollama(nlohmann::json::parse(response));
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
      payload["options"] = { {"temperature", config_.temperature} };
    }

    std::string accumulated;
    std::string line_buffer;

    http_post_stream(
        build_url(config_.endpoint, "/api/chat"), payload.dump(),
        {"Content-Type: application/json"},
        [&](const std::string& chunk) -> bool {
          line_buffer += chunk;
          // Ollama streams newline-delimited JSON
          std::size_t pos = 0;
          while (true)
          {
            auto nl = line_buffer.find('\n', pos);
            if (nl == std::string::npos)
            {
              break;
            }
            std::string line = line_buffer.substr(pos, nl - pos);
            pos = nl + 1;
            if (line.empty())
            {
              continue;
            }
            try
            {
              auto j = nlohmann::json::parse(line);
              if (j.contains("message"))
              {
                std::string content = j["message"].value("content", "");
                if (!content.empty())
                {
                  accumulated += content;
                  if (token_callback)
                  {
                    token_callback(content);
                  }
                }
              }
              if (j.value("done", false))
              {
                line_buffer.erase(0, pos);
                return true;
              }
            }
            catch (...)
            {
              // Skip malformed lines
            }
          }
          line_buffer.erase(0, pos);
          return true;
        });

    return accumulated;
  }

private:
  ProviderConfig config_;
};

std::unique_ptr<LLMProvider> create_ollama_provider(const ProviderConfig& config)
{
  return std::make_unique<OllamaAdapter>(config);
}
}  // namespace neamc::llm
