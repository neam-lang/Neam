//
// Neam Package Manager - Registry Client Implementation
//
// v0.6.5: Replaced raw POSIX sockets with libcurl (via http_client),
// replaced regex JSON parsing with nlohmann::json.
//

#include "neamc/pkg/registry.hpp"
#include "neamc/llm/http_client.hpp"
#include "neamc/version.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

namespace neamc::pkg
{

RegistryClient::RegistryClient(const std::string& registry_url) : registry_url_(registry_url)
{
  // Load saved credentials if available
  const char* home = std::getenv("HOME");
  if (home)
  {
    std::string creds_path = std::string(home) + "/.neam/credentials.toml";
    std::ifstream creds_file(creds_path);
    if (creds_file.is_open())
    {
      std::string line;
      while (std::getline(creds_file, line))
      {
        if (line.find("token = ") != std::string::npos)
        {
          std::size_t start = line.find('"') + 1;
          std::size_t end = line.rfind('"');
          if (start < end)
          {
            auth_token_ = line.substr(start, end - start);
          }
        }
        if (line.find("username = ") != std::string::npos)
        {
          std::size_t start = line.find('"') + 1;
          std::size_t end = line.rfind('"');
          if (start < end)
          {
            username_ = line.substr(start, end - start);
          }
        }
      }
    }
  }
}

bool RegistryClient::login(const std::string& username, const std::string& password)
{
  nlohmann::json body;
  body["username"] = username;
  body["password"] = password;

  std::string response = http_post("/api/v1/auth/login", body.dump());
  if (response.empty())
  {
    return false;
  }

  try
  {
    auto json = nlohmann::json::parse(response);
    if (json.contains("error"))
    {
      last_error_ = json["error"].get<std::string>();
      return false;
    }
    auth_token_ = json.value("token", "");
    if (auth_token_.empty())
    {
      last_error_ = "Login failed: invalid response";
      return false;
    }
  }
  catch (const nlohmann::json::exception& e)
  {
    last_error_ = std::string("Login failed: invalid JSON: ") + e.what();
    return false;
  }

  username_ = username;

  // Save credentials
  const char* home = std::getenv("HOME");
  if (home)
  {
    std::string neam_dir = std::string(home) + "/.neam";
    std::filesystem::create_directories(neam_dir);

    std::string creds_path = neam_dir + "/credentials.toml";
    std::ofstream creds_file(creds_path);
    if (creds_file.is_open())
    {
      creds_file << "[registries.default]\n";
      creds_file << "token = \"" << auth_token_ << "\"\n";
      creds_file << "username = \"" << username_ << "\"\n";
    }
  }

  return true;
}

bool RegistryClient::logout()
{
  auth_token_.clear();
  username_.clear();

  // Remove saved credentials
  const char* home = std::getenv("HOME");
  if (home)
  {
    std::string creds_path = std::string(home) + "/.neam/credentials.toml";
    std::filesystem::remove(creds_path);
  }

  return true;
}

bool RegistryClient::is_authenticated() const
{
  return !auth_token_.empty();
}

std::string RegistryClient::get_username() const
{
  return username_;
}

std::optional<PackageInfo> RegistryClient::get_package(const std::string& name)
{
  std::string response = http_get("/api/v1/packages/" + name);
  if (response.empty())
  {
    return std::nullopt;
  }

  try
  {
    auto json = nlohmann::json::parse(response);
    if (json.contains("error"))
    {
      last_error_ = json["error"].get<std::string>();
      return std::nullopt;
    }

    PackageInfo info;
    info.name = json.value("name", "");
    info.description = json.value("description", "");
    info.repository = json.value("repository", "");
    info.documentation = json.value("documentation", "");
    info.homepage = json.value("homepage", "");
    info.license = json.value("license", "");
    info.downloads = json.value("downloads", 0);

    if (json.contains("authors") && json["authors"].is_array())
    {
      for (const auto& a : json["authors"])
      {
        info.authors.push_back(a.get<std::string>());
      }
    }
    if (json.contains("keywords") && json["keywords"].is_array())
    {
      for (const auto& k : json["keywords"])
      {
        info.keywords.push_back(k.get<std::string>());
      }
    }
    if (json.contains("versions") && json["versions"].is_array())
    {
      for (const auto& v : json["versions"])
      {
        VersionInfo ver;
        ver.version = v.value("version", "");
        ver.checksum = v.value("checksum", "");
        ver.neam_version = v.value("neam_version", "");
        ver.size_bytes = v.value("size_bytes", 0);
        ver.published_at = v.value("published_at", "");
        ver.yanked = v.value("yanked", false);
        info.versions.push_back(ver);
      }
    }

    return info;
  }
  catch (const nlohmann::json::exception& e)
  {
    last_error_ = std::string("Failed to parse package info: ") + e.what();
    return std::nullopt;
  }
}

std::optional<VersionInfo> RegistryClient::get_version(const std::string& name, const std::string& version)
{
  std::string response = http_get("/api/v1/packages/" + name + "/" + version);
  if (response.empty())
  {
    return std::nullopt;
  }

  try
  {
    auto json = nlohmann::json::parse(response);
    if (json.contains("error"))
    {
      last_error_ = json["error"].get<std::string>();
      return std::nullopt;
    }

    VersionInfo info;
    info.version = json.value("version", "");
    info.checksum = json.value("checksum", "");
    info.neam_version = json.value("neam_version", "");
    info.size_bytes = json.value("size_bytes", 0);
    info.published_at = json.value("published_at", "");
    info.yanked = json.value("yanked", false);

    return info;
  }
  catch (const nlohmann::json::exception& e)
  {
    last_error_ = std::string("Failed to parse version info: ") + e.what();
    return std::nullopt;
  }
}

std::vector<SearchResult> RegistryClient::search(const std::string& query, int limit)
{
  std::string response = http_get("/api/v1/packages/search?q=" + query + "&limit=" + std::to_string(limit));
  return parse_search_results(response);
}

std::vector<SearchResult> RegistryClient::get_popular(int limit)
{
  std::string response = http_get("/api/v1/stats/popular?limit=" + std::to_string(limit));
  return parse_search_results(response);
}

std::vector<SearchResult> RegistryClient::get_recent(int limit)
{
  std::string response = http_get("/api/v1/stats/recent?limit=" + std::to_string(limit));
  return parse_search_results(response);
}

std::vector<SearchResult> RegistryClient::parse_search_results(const std::string& response)
{
  std::vector<SearchResult> results;
  if (response.empty())
  {
    return results;
  }

  try
  {
    auto json = nlohmann::json::parse(response);
    if (!json.is_array())
    {
      return results;
    }

    for (const auto& item : json)
    {
      SearchResult result;
      result.name = item.value("name", "");
      result.description = item.value("description", "");
      result.latest_version = item.value("latest_version", "");
      result.downloads = item.value("downloads", 0);
      if (!result.name.empty())
      {
        results.push_back(result);
      }
    }
  }
  catch (const nlohmann::json::exception&)
  {
    // Graceful degradation — return empty results on parse failure
  }

  return results;
}

bool RegistryClient::download_package(const std::string& name, const std::string& version,
                                      const std::string& dest_path)
{
  std::string endpoint = "/api/v1/download/" + name + "/" + version;
  std::string response = http_get(endpoint);

  if (response.empty())
  {
    last_error_ = "Failed to download package: empty response";
    return false;
  }

  // Check if it's a JSON error response
  if (response.size() < 4096 && response.find('{') == 0)
  {
    try
    {
      auto json = nlohmann::json::parse(response);
      if (json.contains("error"))
      {
        last_error_ = json["error"].get<std::string>();
        return false;
      }
    }
    catch (const nlohmann::json::exception&)
    {
      // Not JSON — treat as binary package data
    }
  }

  // Write to file
  std::ofstream out(dest_path, std::ios::binary);
  if (!out.is_open())
  {
    last_error_ = "Failed to open destination file: " + dest_path;
    return false;
  }

  out.write(response.data(), static_cast<std::streamsize>(response.size()));
  return true;
}

bool RegistryClient::publish_package(const std::string& package_path)
{
  if (!is_authenticated())
  {
    last_error_ = "Not authenticated. Please login first.";
    return false;
  }

  // Read package file
  std::ifstream file(package_path, std::ios::binary);
  if (!file.is_open())
  {
    last_error_ = "Failed to open package file: " + package_path;
    return false;
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  std::string package_data = buffer.str();

  // POST to registry
  std::string response = http_post("/api/v1/packages", package_data);

  try
  {
    auto json = nlohmann::json::parse(response);
    if (json.contains("error"))
    {
      last_error_ = json["error"].get<std::string>();
      return false;
    }
  }
  catch (const nlohmann::json::exception&)
  {
    // Not JSON — assume success if no error
  }

  return true;
}

bool RegistryClient::yank_version(const std::string& name, const std::string& version)
{
  if (!is_authenticated())
  {
    last_error_ = "Not authenticated. Please login first.";
    return false;
  }

  std::string response = http_delete("/api/v1/packages/" + name + "/" + version);

  if (!response.empty())
  {
    try
    {
      auto json = nlohmann::json::parse(response);
      if (json.contains("error"))
      {
        last_error_ = json["error"].get<std::string>();
        return false;
      }
    }
    catch (const nlohmann::json::exception&)
    {
      // Not JSON — assume success
    }
  }

  return true;
}

std::string RegistryClient::http_get(const std::string& endpoint)
{
  return make_request("GET", endpoint, "");
}

std::string RegistryClient::http_post(const std::string& endpoint, const std::string& body)
{
  return make_request("POST", endpoint, body);
}

std::string RegistryClient::http_delete(const std::string& endpoint)
{
  return make_request("DELETE", endpoint, "");
}

std::string RegistryClient::make_request(const std::string& method, const std::string& endpoint,
                                         const std::string& body)
{
  std::string url = registry_url_ + endpoint;

  std::vector<std::string> headers;
  headers.push_back("Accept: application/json");
  headers.push_back("User-Agent: neam-pkg/" NEAM_VERSION);

  if (!auth_token_.empty())
  {
    headers.push_back("Authorization: Bearer " + auth_token_);
  }

  if (!body.empty())
  {
    headers.push_back("Content-Type: application/json");
  }

  try
  {
    auto result = neamc::llm::http_request(method, url, body, headers, 30000);
    if (result.status >= 400)
    {
      last_error_ = "HTTP " + std::to_string(result.status) + ": " + result.body;
    }
    return result.body;
  }
  catch (const std::exception& e)
  {
    last_error_ = e.what();
    return "";
  }
}

}  // namespace neamc::pkg
