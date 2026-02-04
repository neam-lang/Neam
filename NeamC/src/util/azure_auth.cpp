// SPDX-License-Identifier: Apache-2.0
//
// Neam Utility - Azure Authentication (v0.6.0)
//

#ifdef NEAM_BACKEND_AZURE

#include "neamc/util/azure_auth.hpp"

#include <cstdlib>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"

namespace neamc::util
{
namespace
{
std::string get_env(const char* name)
{
  if (const char* v = std::getenv(name))
  {
    if (*v != '\0')
    {
      return std::string(v);
    }
  }
  return {};
}

std::string try_managed_identity(const std::string& resource)
{
  // Azure IMDS endpoint for managed identity token
  const std::string url =
      "http://169.254.169.254/metadata/identity/oauth2/token"
      "?api-version=2018-02-01&resource=" +
      resource;

  try
  {
    const std::string response = neamc::llm::http_post_json(
        url, "",
        {"Metadata: true"}, 5000);

    auto json = nlohmann::json::parse(response);
    if (json.contains("access_token"))
    {
      return json["access_token"].get<std::string>();
    }
  }
  catch (...)
  {
    // Managed identity not available, fall through
  }
  return {};
}

std::string try_client_credentials(const std::string& resource)
{
  const std::string tenant_id = get_env("AZURE_TENANT_ID");
  const std::string client_id = get_env("AZURE_CLIENT_ID");
  const std::string client_secret = get_env("AZURE_CLIENT_SECRET");

  if (tenant_id.empty() || client_id.empty() || client_secret.empty())
  {
    return {};
  }

  const std::string url =
      "https://login.microsoftonline.com/" + tenant_id + "/oauth2/v2.0/token";

  const std::string body = "grant_type=client_credentials"
                            "&client_id=" + client_id +
                            "&client_secret=" + client_secret +
                            "&scope=" + resource + ".default";

  try
  {
    const std::string response = neamc::llm::http_post_json(
        url, body,
        {"Content-Type: application/x-www-form-urlencoded"});

    auto json = nlohmann::json::parse(response);
    if (json.contains("access_token"))
    {
      return json["access_token"].get<std::string>();
    }
  }
  catch (...)
  {
    // Client credentials flow failed
  }
  return {};
}
}  // namespace

std::string get_azure_token(const std::string& resource)
{
  // 1. Try Managed Identity (available in Azure VM / App Service / Container Apps)
  std::string token = try_managed_identity(resource);
  if (!token.empty())
  {
    return token;
  }

  // 2. Try client credentials via env vars
  token = try_client_credentials(resource);
  if (!token.empty())
  {
    return token;
  }

  throw std::runtime_error(
      "Azure authentication failed. Set AZURE_TENANT_ID, AZURE_CLIENT_ID, "
      "AZURE_CLIENT_SECRET or use Managed Identity.");
}

std::string get_cosmos_key()
{
  std::string key = get_env("AZURE_COSMOS_KEY");
  if (key.empty())
  {
    key = get_env("NEAM_COSMOS_KEY");
  }
  if (key.empty())
  {
    throw std::runtime_error("Azure Cosmos DB key not found. Set AZURE_COSMOS_KEY.");
  }
  return key;
}

}  // namespace neamc::util

#endif  // NEAM_BACKEND_AZURE
