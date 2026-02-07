//
// Neam LLM - AWS Bedrock adapter
//
// Implements AWS Signature V4 signing for Bedrock invoke_model API.
// Reads AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, AWS_SESSION_TOKEN (optional),
// and AWS_REGION from environment variables.
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

  creds.region = !config.default_host.empty() ? config.default_host : "";
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
  const std::string path =
      "/model/" + uri_encode(model_id) + "/invoke";

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

  // Canonical request
  std::ostringstream canonical;
  canonical << "POST\n"
            << path << "\n"
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

nlohmann::json message_list_to_json(const std::vector<Message>& messages)
{
  nlohmann::json json = nlohmann::json::array();
  for (const auto& message : messages)
  {
    json.push_back({{"role", message.role},
                    {"content", message.content}});
  }
  return json;
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
    payload["max_tokens"] = 1024;
    payload["messages"] = message_list_to_json(messages);
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
