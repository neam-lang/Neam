// SPDX-License-Identifier: Apache-2.0
//
// Neam v0.6.0 - Secrets Resolver Implementations
//

#include "neamc/config/secrets_resolver.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

#ifdef NEAM_BACKEND_AWS
#include <curl/curl.h>
#endif
#ifdef NEAM_BACKEND_AZURE
#include <curl/curl.h>
#endif
#ifdef NEAM_BACKEND_GCP
#include <curl/curl.h>
#endif

// Vault always uses libcurl when available
#if defined(NEAM_BACKEND_AWS) || defined(NEAM_BACKEND_AZURE) || \
    defined(NEAM_BACKEND_GCP) || defined(NEAM_HAS_CURL)
#ifndef NEAM_CURL_INCLUDED
#define NEAM_CURL_INCLUDED
#include <curl/curl.h>
#endif
#endif

namespace neamc::config
{
namespace
{

// Extract the scheme portion before "://" from a URI
std::string uri_scheme(const std::string& uri)
{
  auto pos = uri.find("://");
  if (pos == std::string::npos)
  {
    return "";
  }
  return uri.substr(0, pos);
}

// Extract the path portion after "://" from a URI
std::string uri_path(const std::string& uri)
{
  auto pos = uri.find("://");
  if (pos == std::string::npos)
  {
    return uri;
  }
  return uri.substr(pos + 3);
}

// Extract fragment after '#' from a URI path
std::string uri_fragment(const std::string& path)
{
  auto pos = path.find('#');
  if (pos == std::string::npos)
  {
    return "";
  }
  return path.substr(pos + 1);
}

// Remove fragment from path
std::string uri_path_without_fragment(const std::string& path)
{
  auto pos = path.find('#');
  if (pos == std::string::npos)
  {
    return path;
  }
  return path.substr(0, pos);
}

// Read an entire file to string
std::string read_file(const std::string& path)
{
  std::ifstream file(path);
  if (!file.is_open())
  {
    throw std::runtime_error("secrets_resolver: cannot open file: " + path);
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

#if defined(NEAM_BACKEND_AWS) || defined(NEAM_BACKEND_AZURE) || \
    defined(NEAM_BACKEND_GCP) || defined(NEAM_HAS_CURL)
// libcurl write callback
std::size_t curl_write_callback(char* ptr, std::size_t size, std::size_t nmemb, void* userdata)
{
  auto* response = static_cast<std::string*>(userdata);
  response->append(ptr, size * nmemb);
  return size * nmemb;
}

// Perform an HTTPS GET request
std::string https_get(const std::string& url,
                      const std::vector<std::string>& headers = {})
{
  CURL* curl = curl_easy_init();
  if (!curl)
  {
    throw std::runtime_error("secrets_resolver: failed to init curl");
  }

  std::string response_body;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

  struct curl_slist* header_list = nullptr;
  for (const auto& h : headers)
  {
    header_list = curl_slist_append(header_list, h.c_str());
  }
  if (header_list)
  {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
  }

  CURLcode res = curl_easy_perform(curl);

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  if (header_list)
  {
    curl_slist_free_all(header_list);
  }
  curl_easy_cleanup(curl);

  if (res != CURLE_OK)
  {
    throw std::runtime_error("secrets_resolver: curl error: " +
                             std::string(curl_easy_strerror(res)));
  }
  if (http_code < 200 || http_code >= 300)
  {
    throw std::runtime_error("secrets_resolver: HTTP " + std::to_string(http_code) +
                             " from " + url);
  }

  return response_body;
}
#endif

}  // namespace

// ---------- EnvSecretsResolver ----------

std::string EnvSecretsResolver::resolve(const std::string& uri)
{
  const auto var_name = uri_path(uri);
  const char* val = std::getenv(var_name.c_str());
  if (!val)
  {
    throw std::runtime_error("secrets_resolver: env var not set: " + var_name);
  }
  return std::string(val);
}

bool EnvSecretsResolver::can_resolve(const std::string& uri) const
{
  return uri_scheme(uri) == "env";
}

// ---------- AWS Secrets Manager ----------

#ifdef NEAM_BACKEND_AWS
AwsSecretsResolver::AwsSecretsResolver(const std::string& region)
    : region_(region)
{
  if (region_.empty())
  {
    const char* env_region = std::getenv("AWS_DEFAULT_REGION");
    region_ = env_region ? env_region : "us-east-1";
  }
}

std::string AwsSecretsResolver::resolve(const std::string& uri)
{
  const auto secret_name = uri_path(uri);
  // Use the AWS Secrets Manager HTTP API endpoint
  const std::string url = "https://secretsmanager." + region_ +
                          ".amazonaws.com";
  // In production this would use SigV4 signing; for now we rely on
  // the AWS SDK credential chain via environment or instance profile.
  // This is a stub that documents the intended integration point.
  const std::string body = "{\"SecretId\":\"" + secret_name + "\"}";

  CURL* curl = curl_easy_init();
  if (!curl)
  {
    throw std::runtime_error("secrets_resolver: failed to init curl for AWS");
  }

  std::string response;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/x-amz-json-1.1");
  headers = curl_slist_append(headers,
    "X-Amz-Target: secretsmanager.GetSecretValue");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK)
  {
    throw std::runtime_error("secrets_resolver: AWS curl error: " +
                             std::string(curl_easy_strerror(res)));
  }

  // Minimal JSON extraction for SecretString field
  auto pos = response.find("\"SecretString\"");
  if (pos == std::string::npos)
  {
    throw std::runtime_error("secrets_resolver: AWS response missing SecretString");
  }
  auto val_start = response.find('"', pos + 14);
  auto val_end = response.find('"', val_start + 1);
  if (val_start == std::string::npos || val_end == std::string::npos)
  {
    throw std::runtime_error("secrets_resolver: malformed AWS response");
  }
  return response.substr(val_start + 1, val_end - val_start - 1);
}

bool AwsSecretsResolver::can_resolve(const std::string& uri) const
{
  return uri_scheme(uri) == "aws-sm";
}
#endif

// ---------- Azure Key Vault ----------

#ifdef NEAM_BACKEND_AZURE
AzureKeyVaultResolver::AzureKeyVaultResolver(const std::string& vault_url)
    : vault_url_(vault_url)
{
  if (vault_url_.empty())
  {
    const char* env_url = std::getenv("AZURE_KEYVAULT_URL");
    vault_url_ = env_url ? env_url : "";
  }
  // Remove trailing slash
  if (!vault_url_.empty() && vault_url_.back() == '/')
  {
    vault_url_.pop_back();
  }
}

std::string AzureKeyVaultResolver::resolve(const std::string& uri)
{
  const auto path = uri_path(uri);
  // Path format: vault-name/secret-name  or  just secret-name
  std::string secret_name = path;
  auto slash_pos = path.find('/');
  if (slash_pos != std::string::npos)
  {
    secret_name = path.substr(slash_pos + 1);
  }

  const std::string url = vault_url_ + "/secrets/" + secret_name +
                          "?api-version=7.4";

  // Requires a bearer token from Azure Identity; placeholder for
  // managed identity or service principal token acquisition.
  const char* token = std::getenv("AZURE_ACCESS_TOKEN");
  std::string auth_header = "Authorization: Bearer ";
  auth_header += token ? token : "";

  auto response = https_get(url, {auth_header});

  // Minimal JSON extraction for "value" field
  auto pos = response.find("\"value\"");
  if (pos == std::string::npos)
  {
    throw std::runtime_error("secrets_resolver: Azure response missing value");
  }
  auto val_start = response.find('"', pos + 7);
  auto val_end = response.find('"', val_start + 1);
  if (val_start == std::string::npos || val_end == std::string::npos)
  {
    throw std::runtime_error("secrets_resolver: malformed Azure response");
  }
  return response.substr(val_start + 1, val_end - val_start - 1);
}

bool AzureKeyVaultResolver::can_resolve(const std::string& uri) const
{
  return uri_scheme(uri) == "azure-kv";
}
#endif

// ---------- GCP Secret Manager ----------

#ifdef NEAM_BACKEND_GCP
GcpSecretsResolver::GcpSecretsResolver(const std::string& project)
    : project_(project)
{
  if (project_.empty())
  {
    const char* env_project = std::getenv("GCP_PROJECT");
    project_ = env_project ? env_project : "";
  }
}

std::string GcpSecretsResolver::resolve(const std::string& uri)
{
  auto path = uri_path(uri);
  // Path format: project/secret-name  or  just secret-name
  std::string secret_name = path;
  auto slash_pos = path.find('/');
  if (slash_pos != std::string::npos)
  {
    project_ = path.substr(0, slash_pos);
    secret_name = path.substr(slash_pos + 1);
  }

  const std::string url =
      "https://secretmanager.googleapis.com/v1/projects/" + project_ +
      "/secrets/" + secret_name + "/versions/latest:access";

  const char* token = std::getenv("GCP_ACCESS_TOKEN");
  std::string auth_header = "Authorization: Bearer ";
  auth_header += token ? token : "";

  auto response = https_get(url, {auth_header});

  // Minimal JSON extraction for "data" field (base64 encoded)
  auto pos = response.find("\"data\"");
  if (pos == std::string::npos)
  {
    throw std::runtime_error("secrets_resolver: GCP response missing data");
  }
  auto val_start = response.find('"', pos + 6);
  auto val_end = response.find('"', val_start + 1);
  if (val_start == std::string::npos || val_end == std::string::npos)
  {
    throw std::runtime_error("secrets_resolver: malformed GCP response");
  }
  // Note: in production, the data field is base64-encoded and should be decoded
  return response.substr(val_start + 1, val_end - val_start - 1);
}

bool GcpSecretsResolver::can_resolve(const std::string& uri) const
{
  return uri_scheme(uri) == "gcp-sm";
}
#endif

// ---------- HashiCorp Vault ----------

VaultSecretsResolver::VaultSecretsResolver(const std::string& vault_addr,
                                           const std::string& vault_token)
    : vault_addr_(vault_addr), vault_token_(vault_token)
{
  if (vault_addr_.empty())
  {
    const char* env_addr = std::getenv("VAULT_ADDR");
    vault_addr_ = env_addr ? env_addr : "http://127.0.0.1:8200";
  }
  if (vault_token_.empty())
  {
    const char* env_token = std::getenv("VAULT_TOKEN");
    vault_token_ = env_token ? env_token : "";
  }
  // Remove trailing slash
  if (!vault_addr_.empty() && vault_addr_.back() == '/')
  {
    vault_addr_.pop_back();
  }
}

std::string VaultSecretsResolver::resolve(const std::string& uri)
{
  auto raw_path = uri_path(uri);
  auto fragment = uri_fragment(raw_path);
  auto path = uri_path_without_fragment(raw_path);

  const std::string url = vault_addr_ + "/v1/" + path;

#if defined(NEAM_BACKEND_AWS) || defined(NEAM_BACKEND_AZURE) || \
    defined(NEAM_BACKEND_GCP) || defined(NEAM_HAS_CURL)
  auto response = https_get(url, {"X-Vault-Token: " + vault_token_});

  // Minimal JSON extraction for the key within data.data
  // For KV v2 the structure is: {"data":{"data":{"key":"value"}}}
  if (!fragment.empty())
  {
    auto key_search = "\"" + fragment + "\"";
    auto pos = response.rfind(key_search);
    if (pos == std::string::npos)
    {
      throw std::runtime_error("secrets_resolver: Vault key not found: " + fragment);
    }
    auto colon_pos = response.find(':', pos);
    auto val_start = response.find('"', colon_pos + 1);
    auto val_end = response.find('"', val_start + 1);
    if (val_start == std::string::npos || val_end == std::string::npos)
    {
      throw std::runtime_error("secrets_resolver: malformed Vault response");
    }
    return response.substr(val_start + 1, val_end - val_start - 1);
  }

  return response;
#else
  throw std::runtime_error(
      "secrets_resolver: Vault resolver requires libcurl (compile with NEAM_HAS_CURL)");
#endif
}

bool VaultSecretsResolver::can_resolve(const std::string& uri) const
{
  return uri_scheme(uri) == "vault";
}

// ---------- Kubernetes Mounted Secrets ----------

K8sSecretsResolver::K8sSecretsResolver(const std::string& mount_path)
    : mount_path_(mount_path)
{
  if (mount_path_.empty())
  {
    mount_path_ = "/var/run/secrets";
  }
  // Remove trailing slash
  if (!mount_path_.empty() && mount_path_.back() == '/')
  {
    mount_path_.pop_back();
  }
}

std::string K8sSecretsResolver::resolve(const std::string& uri)
{
  // URI format: k8s://namespace/secret-name/key
  auto path = uri_path(uri);

  // Build filesystem path: mount_path/namespace/secret-name/key
  std::string file_path = mount_path_ + "/" + path;

  return read_file(file_path);
}

bool K8sSecretsResolver::can_resolve(const std::string& uri) const
{
  return uri_scheme(uri) == "k8s";
}

// ---------- CompositeSecretsResolver ----------

void CompositeSecretsResolver::add_resolver(std::unique_ptr<SecretsResolver> resolver)
{
  resolvers_.push_back(std::move(resolver));
}

std::string CompositeSecretsResolver::resolve(const std::string& uri)
{
  for (auto& resolver : resolvers_)
  {
    if (resolver->can_resolve(uri))
    {
      return resolver->resolve(uri);
    }
  }
  throw std::runtime_error("secrets_resolver: no resolver found for URI: " + uri);
}

bool CompositeSecretsResolver::can_resolve(const std::string& uri) const
{
  for (const auto& resolver : resolvers_)
  {
    if (resolver->can_resolve(uri))
    {
      return true;
    }
  }
  return false;
}

// ---------- Factory ----------

std::unique_ptr<SecretsResolver> create_secrets_resolver(const std::string& provider)
{
  if (provider == "env")
  {
    return std::make_unique<EnvSecretsResolver>();
  }
#ifdef NEAM_BACKEND_AWS
  if (provider == "aws")
  {
    return std::make_unique<AwsSecretsResolver>();
  }
#endif
#ifdef NEAM_BACKEND_AZURE
  if (provider == "azure")
  {
    return std::make_unique<AzureKeyVaultResolver>();
  }
#endif
#ifdef NEAM_BACKEND_GCP
  if (provider == "gcp")
  {
    return std::make_unique<GcpSecretsResolver>();
  }
#endif
  if (provider == "vault")
  {
    return std::make_unique<VaultSecretsResolver>();
  }
  if (provider == "k8s")
  {
    return std::make_unique<K8sSecretsResolver>();
  }
  if (provider == "composite")
  {
    auto composite = std::make_unique<CompositeSecretsResolver>();
    // Default composite chain: env -> k8s -> vault
    composite->add_resolver(std::make_unique<EnvSecretsResolver>());
    composite->add_resolver(std::make_unique<K8sSecretsResolver>());
    composite->add_resolver(std::make_unique<VaultSecretsResolver>());
#ifdef NEAM_BACKEND_AWS
    composite->add_resolver(std::make_unique<AwsSecretsResolver>());
#endif
#ifdef NEAM_BACKEND_AZURE
    composite->add_resolver(std::make_unique<AzureKeyVaultResolver>());
#endif
#ifdef NEAM_BACKEND_GCP
    composite->add_resolver(std::make_unique<GcpSecretsResolver>());
#endif
    return composite;
  }

  // Default fallback: env resolver
  std::cerr << "[WARN] secrets_resolver: unknown provider '" << provider
            << "', falling back to env\n";
  return std::make_unique<EnvSecretsResolver>();
}

}  // namespace neamc::config
