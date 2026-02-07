//
// Neam LLM - AWS Bedrock adapter
//
// Implements AWS Signature V4 signing for Bedrock invoke_model API.
// v0.6.6: Added chat_with_tools() for native Claude tool use protocol.
//

#include "neamc/llm/bedrock_adapter.hpp"

#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"
#include "neamc/llm/llm_logger.hpp"
#include "neamc/vm/async/executor.hpp"

namespace neamc::llm
{
namespace
{
// ---- AWS Signature V4 helpers -----------------------------------------------

std::string sha256_hex(const std::string& data)
{
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
  EVP_DigestUpdate(ctx, data.data(), data.size());
  EVP_DigestFinal_ex(ctx, hash, &len);
  EVP_MD_CTX_free(ctx);

  std::ostringstream hex;
  hex << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < len; ++i)
  {
    hex << std::setw(2) << static_cast<int>(hash[i]);
  }
  return hex.str();
}

std::string hmac_sha256_raw(const std::string& key, const std::string& data)
{
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), result,
       &len);
  return std::string(reinterpret_cast<char*>(result), len);
}

std::string hmac_sha256_hex(const std::string& key, const std::string& data)
{
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int len = 0;
  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), result,
       &len);
  std::ostringstream hex;
  hex << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < len; ++i)
  {
    hex << std::setw(2) << static_cast<int>(result[i]);
  }
  return hex.str();
}

std::string uri_encode(const std::string& value)
{
  std::ostringstream encoded;
  encoded << std::hex << std::uppercase << std::setfill('0');
  for (unsigned char c : value)
  {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
    {
      encoded << c;
    }
    else
    {
      encoded << '%' << std::setw(2) << static_cast<int>(c);
    }
  }
  return encoded.str();
}

struct AwsCredentials
{
  std::string access_key;
  std::string secret_key;
  std::string session_token;
  std::string region;
};

AwsCredentials resolve_credentials(const ProviderConfig& config)
{
  AwsCredentials creds;
  creds.access_key =
      !config.api_key.empty() ? config.api_key : std::string();
  if (creds.access_key.empty())
  {
    if (const char* env = std::getenv("AWS_ACCESS_KEY_ID"))
      creds.access_key = env;
  }
  if (const char* env = std::getenv("AWS_SECRET_ACCESS_KEY"))
    creds.secret_key = env;
  if (const char* env = std::getenv("AWS_SESSION_TOKEN"))
    creds.session_token = env;

  // default_host may be an Ollama URL (e.g. "http://localhost:11434") from shared
  // config — only use it as a region if it looks like an AWS region, not a URL.
  if (!config.default_host.empty() &&
      config.default_host.find("://") == std::string::npos)
  {
    creds.region = config.default_host;
  }
  if (creds.region.empty())
  {
    if (const char* env = std::getenv("AWS_REGION"))
      creds.region = env;
    else if (const char* env = std::getenv("AWS_DEFAULT_REGION"))
      creds.region = env;
    else
      creds.region = "us-east-1";
  }

  if (creds.access_key.empty())
    throw std::runtime_error("Bedrock: AWS_ACCESS_KEY_ID is required");
  if (creds.secret_key.empty())
    throw std::runtime_error("Bedrock: AWS_SECRET_ACCESS_KEY is required");

  return creds;
}

struct SignedRequest
{
  std::string url;
  std::vector<std::string> headers;
};

SignedRequest sign_request(const AwsCredentials& creds,
                           const std::string& model_id,
                           const std::string& body)
{
  const std::string service = "bedrock";
  const std::string host =
      "bedrock-runtime." + creds.region + ".amazonaws.com";
  // URL path uses single URI-encoding
  const std::string path =
      "/model/" + uri_encode(model_id) + "/invoke";
  // AWS SigV4 canonical request requires double URI-encoding of path segments
  // (non-S3 services): %3A in URL becomes %253A in canonical path
  const std::string canonical_path =
      "/model/" + uri_encode(uri_encode(model_id)) + "/invoke";

  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &time_t_now);
#else
  gmtime_r(&time_t_now, &utc);
#endif

  char date_stamp[9];
  std::strftime(date_stamp, sizeof(date_stamp), "%Y%m%d", &utc);
  char amz_date[17];
  std::strftime(amz_date, sizeof(amz_date), "%Y%m%dT%H%M%SZ", &utc);

  const std::string content_type = "application/json";
  const std::string payload_hash = sha256_hex(body);

  // Canonical request (uses double-encoded path per SigV4 spec)
  std::ostringstream canonical;
  canonical << "POST\n"
            << canonical_path << "\n"
            << "\n"  // empty query string
            << "content-type:" << content_type << "\n"
            << "host:" << host << "\n"
            << "x-amz-content-sha256:" << payload_hash << "\n"
            << "x-amz-date:" << amz_date << "\n";
  if (!creds.session_token.empty())
  {
    canonical << "x-amz-security-token:" << creds.session_token << "\n";
  }
  canonical << "\n";  // end of headers

  // Signed headers
  std::string signed_headers =
      "content-type;host;x-amz-content-sha256;x-amz-date";
  if (!creds.session_token.empty())
  {
    signed_headers += ";x-amz-security-token";
  }
  canonical << signed_headers << "\n" << payload_hash;

  const std::string canonical_hash = sha256_hex(canonical.str());

  // String to sign
  const std::string scope =
      std::string(date_stamp) + "/" + creds.region + "/" + service + "/aws4_request";
  const std::string string_to_sign =
      "AWS4-HMAC-SHA256\n" + std::string(amz_date) + "\n" + scope + "\n" +
      canonical_hash;

  // Signing key
  const std::string k_date =
      hmac_sha256_raw("AWS4" + creds.secret_key, std::string(date_stamp));
  const std::string k_region = hmac_sha256_raw(k_date, creds.region);
  const std::string k_service = hmac_sha256_raw(k_region, service);
  const std::string k_signing = hmac_sha256_raw(k_service, "aws4_request");

  const std::string signature = hmac_sha256_hex(k_signing, string_to_sign);

  const std::string authorization =
      "AWS4-HMAC-SHA256 Credential=" + creds.access_key + "/" + scope +
      ", SignedHeaders=" + signed_headers + ", Signature=" + signature;

  SignedRequest req;
  req.url = "https://" + host + path;
  req.headers = {
      "Content-Type: " + content_type,
      "Host: " + host,
      "X-Amz-Content-Sha256: " + payload_hash,
      "X-Amz-Date: " + std::string(amz_date),
      "Authorization: " + authorization,
  };
  if (!creds.session_token.empty())
  {
    req.headers.push_back("X-Amz-Security-Token: " + creds.session_token);
  }

  return req;
}

std::string extract_response_text(const nlohmann::json& json)
{
  // Anthropic Claude Bedrock response format
  if (json.contains("content") && json.at("content").is_array() &&
      !json.at("content").empty())
  {
    const auto& block = json.at("content").front();
    if (block.contains("text"))
    {
      return block.at("text").get<std::string>();
    }
  }
  // Fallback: top-level "completion" (older Bedrock models)
  if (json.contains("completion"))
  {
    return json.at("completion").get<std::string>();
  }
  throw std::runtime_error("Bedrock response missing content");
}

// Build messages array for Claude Messages API.
// Handles both plain text messages and multi-block content (tool results).
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
      entry["content"] = msg.content_blocks;
    }
    else
    {
      entry["content"] = msg.content;
    }
    arr.push_back(std::move(entry));
  }
  return arr;
}

// Parse Claude response into ChatResponse, detecting tool_use blocks.
ChatResponse parse_claude_response(const nlohmann::json& parsed)
{
  ChatResponse resp;
  resp.stop_reason = parsed.value("stop_reason", "end_turn");

  if (parsed.contains("content") && parsed.at("content").is_array())
  {
    for (const auto& block : parsed.at("content"))
    {
      const auto type = block.value("type", "");
      if (type == "text")
      {
        if (!resp.text.empty())
        {
          resp.text += "\n";
        }
        resp.text += block.value("text", "");
      }
      else if (type == "tool_use")
      {
        ToolCall tc;
        tc.id = block.value("id", "");
        tc.name = block.value("name", "");
        tc.input = block.value("input", nlohmann::json::object());
        resp.tool_calls.push_back(std::move(tc));
      }
    }
  }

  // Fallback for older response formats
  if (resp.text.empty() && resp.tool_calls.empty())
  {
    if (parsed.contains("completion"))
    {
      resp.text = parsed.at("completion").get<std::string>();
    }
  }

  return resp;
}

// Build tool_result content blocks for returning skill results to Claude.
nlohmann::json build_tool_result_blocks(const std::vector<ToolResult>& results)
{
  nlohmann::json blocks = nlohmann::json::array();
  for (const auto& r : results)
  {
    nlohmann::json block;
    block["type"] = "tool_result";
    block["tool_use_id"] = r.tool_use_id;
    block["content"] = r.content;
    if (r.is_error)
    {
      block["is_error"] = true;
    }
    blocks.push_back(std::move(block));
  }
  return blocks;
}

// Build assistant content blocks that include both text and tool_use entries,
// so the conversation history is well-formed for Claude.
nlohmann::json build_assistant_content_blocks(const ChatResponse& resp)
{
  nlohmann::json blocks = nlohmann::json::array();
  if (!resp.text.empty())
  {
    blocks.push_back({{"type", "text"}, {"text", resp.text}});
  }
  for (const auto& tc : resp.tool_calls)
  {
    blocks.push_back(
        {{"type", "tool_use"}, {"id", tc.id}, {"name", tc.name}, {"input", tc.input}});
  }
  return blocks;
}
}  // namespace

class BedrockAdapter final : public LLMProvider
{
public:
  explicit BedrockAdapter(ProviderConfig config)
      : config_(std::move(config)), creds_(resolve_credentials(config_))
  {
    if (config_.model.empty())
    {
      throw std::runtime_error("Bedrock model is required");
    }
  }

  std::string complete(const std::string& prompt) override
  {
    std::vector<Message> messages;
    messages.push_back({"user", prompt});
    return chat(messages);
  }

  vm::async::Future<std::string> complete_async(
      const std::string& prompt) override
  {
    return vm::async::Executor::global().submit(
        [this, prompt]() { return complete(prompt); });
  }

  std::string chat(const std::vector<Message>& messages) override
  {
    LLMLogger::info("bedrock", "Chat request: model=" + config_.model +
                                    ", region=" + creds_.region +
                                    ", messages=" + std::to_string(messages.size()));

    nlohmann::json payload;
    payload["anthropic_version"] = "bedrock-2023-10-31";
    payload["max_tokens"] = 4096;
    payload["messages"] = build_messages_json(messages);
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    const std::string body = payload.dump();
    const auto signed_req = sign_request(creds_, config_.model, body);

    try
    {
      const std::string response =
          http_post_json(signed_req.url, body, signed_req.headers, 60000);
      auto parsed = nlohmann::json::parse(response);
      auto text = extract_response_text(parsed);
      LLMLogger::debug("bedrock", "Response: " + std::to_string(text.size()) + " chars");
      return text;
    }
    catch (const std::exception& e)
    {
      LLMLogger::error("bedrock", "Chat failed: " + std::string(e.what()));
      throw;
    }
  }

  // v0.6.6: Native Claude tool use via Bedrock Messages API
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

    LLMLogger::info("bedrock", "Tool-aware chat: model=" + config_.model +
                                    ", tools=" + std::to_string(tools.size()) +
                                    ", messages=" + std::to_string(messages.size()));

    nlohmann::json payload;
    payload["anthropic_version"] = "bedrock-2023-10-31";
    payload["max_tokens"] = 4096;
    payload["messages"] = build_messages_json(messages);
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    // Build tools array in Claude format
    nlohmann::json tools_json = nlohmann::json::array();
    for (const auto& tool : tools)
    {
      tools_json.push_back(
          {{"name", tool.name},
           {"description", tool.description},
           {"input_schema", tool.input_schema}});
    }
    payload["tools"] = std::move(tools_json);

    // Tool choice
    if (tool_choice == "any")
    {
      payload["tool_choice"] = {{"type", "any"}};
    }
    else if (tool_choice == "none")
    {
      payload["tool_choice"] = {{"type", "none"}};
    }
    else if (tool_choice != "auto" && !tool_choice.empty())
    {
      payload["tool_choice"] = {{"type", "tool"}, {"name", tool_choice}};
    }
    else
    {
      payload["tool_choice"] = {{"type", "auto"}};
    }

    const std::string body = payload.dump();
    const auto signed_req = sign_request(creds_, config_.model, body);

    try
    {
      const std::string response =
          http_post_json(signed_req.url, body, signed_req.headers, 60000);
      auto parsed = nlohmann::json::parse(response);
      auto chat_resp = parse_claude_response(parsed);
      LLMLogger::debug("bedrock", "Tool response: stop_reason=" + chat_resp.stop_reason +
                                       ", tool_calls=" + std::to_string(chat_resp.tool_calls.size()));
      return chat_resp;
    }
    catch (const std::exception& e)
    {
      LLMLogger::error("bedrock", "Tool chat failed: " + std::string(e.what()));
      throw;
    }
  }

private:
  ProviderConfig config_;
  AwsCredentials creds_;
};

std::unique_ptr<LLMProvider> create_bedrock_provider(
    const ProviderConfig& config)
{
  return std::make_unique<BedrockAdapter>(config);
}
}  // namespace neamc::llm
