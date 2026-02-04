// SPDX-License-Identifier: Apache-2.0
//
// Neam Utility - GCP Authentication (v0.6.0)
//

#ifdef NEAM_BACKEND_GCP

#include "neamc/util/gcp_auth.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

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

nlohmann::json load_service_account_json()
{
  const std::string path = get_env("GOOGLE_APPLICATION_CREDENTIALS");
  if (path.empty())
  {
    return {};
  }

  std::ifstream file(path);
  if (!file.is_open())
  {
    throw std::runtime_error("Cannot open GOOGLE_APPLICATION_CREDENTIALS file: " + path);
  }

  std::ostringstream ss;
  ss << file.rdbuf();
  return nlohmann::json::parse(ss.str());
}

std::string try_metadata_server_token()
{
  // GCE metadata server for access tokens
  const std::string url =
      "http://metadata.google.internal/computeMetadata/v1/instance/"
      "service-accounts/default/token";

  try
  {
    const std::string response = neamc::llm::http_post_json(
        url, "",
        {"Metadata-Flavor: Google"}, 5000);

    auto json = nlohmann::json::parse(response);
    if (json.contains("access_token"))
    {
      return json["access_token"].get<std::string>();
    }
  }
  catch (...)
  {
    // Metadata server not available
  }
  return {};
}

std::string try_metadata_server_project()
{
  const std::string url =
      "http://metadata.google.internal/computeMetadata/v1/project/project-id";

  try
  {
    const std::string response = neamc::llm::http_post_json(
        url, "",
        {"Metadata-Flavor: Google"}, 5000);
    return response;
  }
  catch (...)
  {
    // Metadata server not available
  }
  return {};
}
}  // namespace

std::string get_gcp_access_token()
{
  // 1. Try metadata server (GCE, Cloud Run, GKE)
  std::string token = try_metadata_server_token();
  if (!token.empty())
  {
    return token;
  }

  // 2. Try service account JSON + OAuth2 token exchange
  // This is a placeholder; full implementation would use JWT signing
  // with the service account private key to get an access token.
  auto sa_json = load_service_account_json();
  if (!sa_json.empty() && sa_json.contains("client_email"))
  {
    // In production, sign a JWT with the private_key and exchange at
    // https://oauth2.googleapis.com/token for an access_token.
    // For now, check for a pre-configured token in the environment.
    std::string env_token = get_env("GOOGLE_ACCESS_TOKEN");
    if (!env_token.empty())
    {
      return env_token;
    }

    throw std::runtime_error(
        "GCP service account found but JWT signing not yet implemented. "
        "Set GOOGLE_ACCESS_TOKEN or run on GCE/Cloud Run for automatic auth.");
  }

  // 3. Fallback to environment variable
  std::string env_token = get_env("GOOGLE_ACCESS_TOKEN");
  if (!env_token.empty())
  {
    return env_token;
  }

  throw std::runtime_error(
      "GCP authentication failed. Set GOOGLE_APPLICATION_CREDENTIALS, "
      "GOOGLE_ACCESS_TOKEN, or run on GCE/Cloud Run/GKE.");
}

std::string get_gcp_project_id()
{
  // 1. Environment variable
  std::string project = get_env("GOOGLE_CLOUD_PROJECT");
  if (!project.empty())
  {
    return project;
  }
  project = get_env("GCLOUD_PROJECT");
  if (!project.empty())
  {
    return project;
  }

  // 2. Service account JSON
  auto sa_json = load_service_account_json();
  if (!sa_json.empty() && sa_json.contains("project_id"))
  {
    return sa_json["project_id"].get<std::string>();
  }

  // 3. Metadata server
  project = try_metadata_server_project();
  if (!project.empty())
  {
    return project;
  }

  return {};
}

}  // namespace neamc::util

#endif  // NEAM_BACKEND_GCP
