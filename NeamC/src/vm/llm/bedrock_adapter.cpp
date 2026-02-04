// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - AWS Bedrock adapter (v0.6.0)
//
// Invokes foundation models via AWS Bedrock Runtime REST API.
// Uses Anthropic Messages format for Claude models.
// Requires AWS Signature V4 signing.
//

#ifdef NEAM_BACKEND_AWS

#include "neamc/llm/bedrock_adapter.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"
#include "neamc/util/aws_signer.hpp"
#include "neamc/vm/async/executor.hpp"

namespace neamc::llm
{
namespace
{

// Map friendly model names to Bedrock model IDs
std::string resolve_model_id(const std::string& model)
{
  // Allow pass-through of full ARNs or qualified IDs
  if (model.find("arn:") == 0 || model.find('.') != std::string::npos)
  {
    return model;
  }
  // Friendly aliases
  if (model == "claude-3-opus" || model == "claude-3-opus-20240229")
    return "anthropic.claude-3-opus-20240229-v1:0";
  if (model == "claude-3-sonnet" || model == "claude-3-sonnet-20240229")
    return "anthropic.claude-3-sonnet-20240229-v1:0";
  if (model == "claude-3-haiku" || model == "claude-3-haiku-20240307")
    return "anthropic.claude-3-haiku-20240307-v1:0";
  if (model == "claude-3.5-sonnet" || model == "claude-3-5-sonnet-20241022")
    return "anthropic.claude-3-5-sonnet-20241022-v2:0";
  if (model == "claude-3.5-haiku" || model == "claude-3-5-haiku-20241022")
    return "anthropic.claude-3-5-haiku-20241022-v1:0";
  // Default: treat as raw Bedrock model ID
  return model;
}

std::string extract_region(const std::string& endpoint)
{
  // Parse region from endpoint like "https://bedrock-runtime.us-east-1.amazonaws.com"
  auto pos = endpoint.find("bedrock-runtime.");
  if (pos != std::string::npos)
  {
    pos += 16;  // length of "bedrock-runtime."
    auto end = endpoint.find('.', pos);
    if (end != std::string::npos)
    {
      return endpoint.substr(pos, end - pos);
    }
  }
  return "us-east-1";
}

std::string build_invoke_url(const std::string& endpoint, const std::string& model_id)
{
  std::string base = endpoint;
  if (!base.empty() && base.back() == '/')
  {
    base.pop_back();
  }
  return base + "/model/" + model_id + "/invoke";
}

std::string build_invoke_stream_url(const std::string& endpoint, const std::string& model_id)
{
  std::string base = endpoint;
  if (!base.empty() && base.back() == '/')
  {
    base.pop_back();
  }
  return base + "/model/" + model_id + "/invoke-with-response-stream";
}

nlohmann::json messages_to_bedrock_json(const std::vector<Message>& messages,
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

nlohmann::json tools_to_bedrock_json(const std::vector<ToolDefinition>& tools)
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

std::string extract_bedrock_text(const nlohmann::json& json)
{
  if (!json.contains("content") || !json.at("content").is_array())
  {
    throw std::runtime_error("Bedrock response missing content array");
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

ChatResult extract_bedrock_result(const nlohmann::json& json)
{
  ChatResult result;
  if (!json.contains("content") || !json.at("content").is_array())
  {
    throw std::runtime_error("Bedrock response missing content array");
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

std::vector<std::string> build_signed_headers(const std::string& url, const std::string& body,
                                               const std::string& region,
                                               const util::AwsCredentials& creds)
{
  auto sig_headers = util::sign_aws_request("POST", url, body, "bedrock", region, creds,
                                             {{"content-type", "application/json"},
                                              {"accept", "application/json"}});
  std::vector<std::string> headers;
  headers.push_back("Content-Type: application/json");
  headers.push_back("Accept: application/json");
  for (const auto& [k, v] : sig_headers)
  {
    headers.push_back(k + ": " + v);
  }
  return headers;
}
}  // namespace

class BedrockAdapter final : public LLMProvider
{
public:
  explicit BedrockAdapter(ProviderConfig config)
      : config_(std::move(config)), credentials_(util::load_aws_credentials())
  {
    if (config_.model.empty())
    {
      throw std::runtime_error("Bedrock model is required");
    }
    model_id_ = resolve_model_id(config_.model);
    region_ = extract_region(config_.endpoint);
    if (config_.endpoint.empty())
    {
      config_.endpoint = "https://bedrock-runtime." + region_ + ".amazonaws.com";
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
    payload["anthropic_version"] = "bedrock-2023-05-31";
    payload["max_tokens"] = 4096;
    payload["messages"] = messages_to_bedrock_json(messages, system_prompt);
    if (!system_prompt.empty())
    {
      payload["system"] = system_prompt;
    }
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    const std::string url = build_invoke_url(config_.endpoint, model_id_);
    const std::string body = payload.dump();
    const auto headers = build_signed_headers(url, body, region_, credentials_);

    const std::string response = http_post_json(url, body, headers);
    return extract_bedrock_text(nlohmann::json::parse(response));
  }

  ChatResult chat_with_tools(const std::vector<Message>& messages,
                             const std::vector<ToolDefinition>& tools,
                             const std::string& response_format = "") override
  {
    std::string system_prompt;
    nlohmann::json payload;
    payload["anthropic_version"] = "bedrock-2023-05-31";
    payload["max_tokens"] = 4096;
    payload["messages"] = messages_to_bedrock_json(messages, system_prompt);
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
      payload["tools"] = tools_to_bedrock_json(tools);
    }
    if (response_format == "json_object")
    {
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

    const std::string url = build_invoke_url(config_.endpoint, model_id_);
    const std::string body = payload.dump();
    const auto headers = build_signed_headers(url, body, region_, credentials_);

    const std::string response = http_post_json(url, body, headers);
    return extract_bedrock_result(nlohmann::json::parse(response));
  }

  std::string chat_stream(const std::vector<Message>& messages,
                          const TokenCallback& token_callback) override
  {
    std::string system_prompt;
    nlohmann::json payload;
    payload["anthropic_version"] = "bedrock-2023-05-31";
    payload["max_tokens"] = 4096;
    payload["messages"] = messages_to_bedrock_json(messages, system_prompt);
    if (!system_prompt.empty())
    {
      payload["system"] = system_prompt;
    }
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    const std::string url = build_invoke_stream_url(config_.endpoint, model_id_);
    const std::string body = payload.dump();
    const auto headers = build_signed_headers(url, body, region_, credentials_);

    std::string accumulated;
    std::string sse_buffer;

    http_post_stream(
        url, body, headers,
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
  util::AwsCredentials credentials_;
  std::string model_id_;
  std::string region_;
};

std::unique_ptr<LLMProvider> create_bedrock_provider(const ProviderConfig& config)
{
  return std::make_unique<BedrockAdapter>(config);
}
}  // namespace neamc::llm

#endif  // NEAM_BACKEND_AWS
