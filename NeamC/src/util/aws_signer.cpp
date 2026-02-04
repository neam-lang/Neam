// SPDX-License-Identifier: Apache-2.0
//
// Neam Utility - AWS Signature V4 (v0.6.0)
//

#ifdef NEAM_BACKEND_AWS

#include "neamc/util/aws_signer.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <sstream>

#include <openssl/evp.h>
#include <openssl/hmac.h>

namespace neamc::util
{
namespace
{
std::string sha256_hex(const std::string& input)
{
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (ctx)
  {
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, input.data(), input.size());
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
  }

  std::ostringstream oss;
  for (unsigned int i = 0; i < hash_len; ++i)
  {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
  }
  return oss.str();
}

std::string hmac_sha256(const std::string& key, const std::string& data)
{
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int result_len = 0;

  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), result, &result_len);

  return std::string(reinterpret_cast<char*>(result), result_len);
}

std::string hmac_sha256_hex(const std::string& key, const std::string& data)
{
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int result_len = 0;

  HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), result, &result_len);

  std::ostringstream oss;
  for (unsigned int i = 0; i < result_len; ++i)
  {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(result[i]);
  }
  return oss.str();
}

std::string uri_encode(const std::string& input, bool encode_slash = true)
{
  std::ostringstream oss;
  for (unsigned char c : input)
  {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~')
    {
      oss << c;
    }
    else if (c == '/' && !encode_slash)
    {
      oss << '/';
    }
    else
    {
      oss << '%' << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
          << static_cast<int>(c);
    }
  }
  return oss.str();
}

std::string get_timestamp()
{
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &time_t);
#else
  gmtime_r(&time_t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y%m%dT%H%M%SZ");
  return oss.str();
}

std::string get_date(const std::string& timestamp)
{
  return timestamp.substr(0, 8);
}

// Extract host and path from URL
void parse_url(const std::string& url, std::string& host, std::string& path)
{
  auto start = url.find("://");
  if (start == std::string::npos) start = 0;
  else start += 3;

  auto path_start = url.find('/', start);
  if (path_start == std::string::npos)
  {
    host = url.substr(start);
    path = "/";
  }
  else
  {
    host = url.substr(start, path_start - start);
    path = url.substr(path_start);
  }
}
}  // namespace

AwsCredentials load_aws_credentials()
{
  AwsCredentials creds;
  if (const char* v = std::getenv("AWS_ACCESS_KEY_ID")) creds.access_key_id = v;
  if (const char* v = std::getenv("AWS_SECRET_ACCESS_KEY")) creds.secret_access_key = v;
  if (const char* v = std::getenv("AWS_SESSION_TOKEN")) creds.session_token = v;
  return creds;
}

std::unordered_map<std::string, std::string> sign_aws_request(
    const std::string& method, const std::string& url, const std::string& body,
    const std::string& service, const std::string& region, const AwsCredentials& credentials,
    const std::unordered_map<std::string, std::string>& extra_headers)
{
  std::string host, path;
  parse_url(url, host, path);

  std::string timestamp = get_timestamp();
  std::string date = get_date(timestamp);

  // Build canonical headers
  std::map<std::string, std::string> headers;
  headers["host"] = host;
  headers["x-amz-date"] = timestamp;
  if (!credentials.session_token.empty())
  {
    headers["x-amz-security-token"] = credentials.session_token;
  }
  for (const auto& [k, v] : extra_headers)
  {
    std::string lower_k = k;
    std::transform(lower_k.begin(), lower_k.end(), lower_k.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    headers[lower_k] = v;
  }

  std::string canonical_headers;
  std::string signed_headers;
  for (const auto& [k, v] : headers)
  {
    canonical_headers += k + ":" + v + "\n";
    if (!signed_headers.empty()) signed_headers += ";";
    signed_headers += k;
  }

  std::string payload_hash = sha256_hex(body);

  // Canonical request
  std::string canonical_request = method + "\n" + uri_encode(path, false) + "\n" + "\n" +
                                   canonical_headers + "\n" + signed_headers + "\n" + payload_hash;

  // String to sign
  std::string scope = date + "/" + region + "/" + service + "/aws4_request";
  std::string string_to_sign =
      "AWS4-HMAC-SHA256\n" + timestamp + "\n" + scope + "\n" + sha256_hex(canonical_request);

  // Signing key
  std::string k_date = hmac_sha256("AWS4" + credentials.secret_access_key, date);
  std::string k_region = hmac_sha256(k_date, region);
  std::string k_service = hmac_sha256(k_region, service);
  std::string k_signing = hmac_sha256(k_service, "aws4_request");

  std::string signature = hmac_sha256_hex(k_signing, string_to_sign);

  std::string authorization = "AWS4-HMAC-SHA256 Credential=" + credentials.access_key_id + "/" +
                               scope + ", SignedHeaders=" + signed_headers +
                               ", Signature=" + signature;

  std::unordered_map<std::string, std::string> result;
  result["Authorization"] = authorization;
  result["X-Amz-Date"] = timestamp;
  if (!credentials.session_token.empty())
  {
    result["X-Amz-Security-Token"] = credentials.session_token;
  }

  return result;
}

}  // namespace neamc::util

#endif  // NEAM_BACKEND_AWS
