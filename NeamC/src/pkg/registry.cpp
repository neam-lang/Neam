// SPDX-License-Identifier: Apache-2.0
//
// Neam Package Manager - Registry Client Implementation
//

#include "neamc/pkg/registry.hpp"
#include "neamc/llm/http_client.hpp"

// Use curl-based HTTP client for HTTPS support
using neamc::llm::http_get;
using neamc::llm::http_post_json;

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <cstdlib>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#endif

namespace neamc::pkg
{

// Simple JSON parser helpers
namespace json
{
std::string get_string(const std::string& json, const std::string& key)
{
  std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch match;
  if (std::regex_search(json, match, pattern) && match.size() > 1)
  {
    return match[1].str();
  }
  return "";
}

int get_int(const std::string& json, const std::string& key)
{
  std::regex pattern("\"" + key + "\"\\s*:\\s*(\\d+)");
  std::smatch match;
  if (std::regex_search(json, match, pattern) && match.size() > 1)
  {
    return std::stoi(match[1].str());
  }
  return 0;
}

bool get_bool(const std::string& json, const std::string& key)
{
  std::regex pattern("\"" + key + "\"\\s*:\\s*(true|false)");
  std::smatch match;
  if (std::regex_search(json, match, pattern) && match.size() > 1)
  {
    return match[1].str() == "true";
  }
  return false;
}

std::vector<std::string> get_string_array(const std::string& json, const std::string& key)
{
  std::vector<std::string> result;
  std::regex array_pattern("\"" + key + "\"\\s*:\\s*\\[([^\\]]*)\\]");
  std::smatch array_match;
  if (std::regex_search(json, array_match, array_pattern) && array_match.size() > 1)
  {
    std::string array_content = array_match[1].str();
    std::regex item_pattern("\"([^\"]*)\"");
    auto items_begin = std::sregex_iterator(array_content.begin(), array_content.end(), item_pattern);
    auto items_end = std::sregex_iterator();
    for (auto i = items_begin; i != items_end; ++i)
    {
      result.push_back((*i)[1].str());
    }
  }
  return result;
}

std::string to_json_string(const std::string& value)
{
  std::string result = "\"";
  for (char c : value)
  {
    switch (c)
    {
    case '"':
      result += "\\\"";
      break;
    case '\\':
      result += "\\\\";
      break;
    case '\n':
      result += "\\n";
      break;
    case '\r':
      result += "\\r";
      break;
    case '\t':
      result += "\\t";
      break;
    default:
      result += c;
    }
  }
  result += "\"";
  return result;
}
}  // namespace json

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
  std::string body = "{\"username\":" + json::to_json_string(username) + ",\"password\":" +
                     json::to_json_string(password) + "}";

  std::string response = http_post("/api/v1/auth/login", body);
  if (response.empty())
  {
    return false;
  }

  auth_token_ = json::get_string(response, "token");
  if (auth_token_.empty())
  {
    last_error_ = json::get_string(response, "error");
    if (last_error_.empty())
    {
      last_error_ = "Login failed: invalid response";
    }
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

  if (response.find("\"error\"") != std::string::npos)
  {
    last_error_ = json::get_string(response, "error");
    return std::nullopt;
  }

  // Supabase API wraps response in {"package": {...}}
  // Extract the inner package object for parsing
  std::string pkg_data = response;
  std::size_t pkg_start = response.find("\"package\"");
  if (pkg_start != std::string::npos)
  {
    std::size_t obj_start = response.find('{', pkg_start + 9);
    if (obj_start != std::string::npos)
    {
      // Find matching closing brace (handle nested objects)
      int depth = 1;
      std::size_t pos = obj_start + 1;
      while (pos < response.size() && depth > 0)
      {
        if (response[pos] == '{') depth++;
        else if (response[pos] == '}') depth--;
        pos++;
      }
      pkg_data = response.substr(obj_start, pos - obj_start);
    }
  }

  PackageInfo info;
  info.name = json::get_string(pkg_data, "name");
  info.description = json::get_string(pkg_data, "description");
  info.repository = json::get_string(pkg_data, "repository");
  info.documentation = json::get_string(pkg_data, "documentation");
  info.homepage = json::get_string(pkg_data, "homepage");
  info.license = json::get_string(pkg_data, "license");
  info.authors = json::get_string_array(pkg_data, "authors");
  info.keywords = json::get_string_array(pkg_data, "keywords");
  info.downloads = json::get_int(pkg_data, "downloads");

  // Parse versions array (simplified)
  // Find versions array manually since dotall is not supported
  std::size_t versions_start = pkg_data.find("\"versions\"");
  if (versions_start != std::string::npos)
  {
    std::size_t array_start = pkg_data.find('[', versions_start);
    std::size_t array_end = pkg_data.find(']', array_start);
    if (array_start != std::string::npos && array_end != std::string::npos)
    {
      std::string versions_content = pkg_data.substr(array_start + 1, array_end - array_start - 1);

      std::regex version_obj_pattern("\\{([^}]+)\\}");
      auto begin = std::sregex_iterator(versions_content.begin(), versions_content.end(), version_obj_pattern);
      auto end = std::sregex_iterator();

      for (auto i = begin; i != end; ++i)
      {
        std::string ver_json = (*i)[1].str();
        VersionInfo ver;
        ver.version = json::get_string(ver_json, "version");
        ver.checksum = json::get_string(ver_json, "checksum");
        ver.neam_version = json::get_string(ver_json, "neam_version");
        ver.size_bytes = json::get_int(ver_json, "size_bytes");
        ver.published_at = json::get_string(ver_json, "published_at");
        ver.yanked = json::get_bool(ver_json, "yanked");
        info.versions.push_back(ver);
      }
    }
  }

  return info;
}

std::optional<VersionInfo> RegistryClient::get_version(const std::string& name, const std::string& version)
{
  std::string response = http_get("/api/v1/packages/" + name + "/" + version);
  if (response.empty())
  {
    return std::nullopt;
  }

  if (response.find("\"error\"") != std::string::npos)
  {
    last_error_ = json::get_string(response, "error");
    return std::nullopt;
  }

  VersionInfo info;
  info.version = json::get_string(response, "version");
  info.checksum = json::get_string(response, "checksum");
  info.neam_version = json::get_string(response, "neam_version");
  info.size_bytes = json::get_int(response, "size_bytes");
  info.published_at = json::get_string(response, "published_at");
  info.yanked = json::get_bool(response, "yanked");

  return info;
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

  // Supabase API wraps results in {"packages": [...]}
  // Extract the packages array content
  std::string packages_content = response;
  std::size_t pkg_start = response.find("\"packages\"");
  if (pkg_start != std::string::npos)
  {
    std::size_t array_start = response.find('[', pkg_start);
    std::size_t array_end = response.rfind(']');
    if (array_start != std::string::npos && array_end != std::string::npos && array_end > array_start)
    {
      packages_content = response.substr(array_start + 1, array_end - array_start - 1);
    }
  }

  std::regex result_pattern("\\{([^}]+)\\}");
  auto begin = std::sregex_iterator(packages_content.begin(), packages_content.end(), result_pattern);
  auto end = std::sregex_iterator();

  for (auto i = begin; i != end; ++i)
  {
    std::string item_json = (*i)[1].str();
    SearchResult result;
    result.name = json::get_string(item_json, "name");
    result.description = json::get_string(item_json, "description");
    result.latest_version = json::get_string(item_json, "latest_version");
    result.downloads = json::get_int(item_json, "downloads");

    if (!result.name.empty())
    {
      results.push_back(result);
    }
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

  // Check if it's an error response
  if (response.find("\"error\"") != std::string::npos)
  {
    last_error_ = json::get_string(response, "error");
    return false;
  }

  // Write to file
  std::ofstream out(dest_path, std::ios::binary);
  if (!out.is_open())
  {
    last_error_ = "Failed to open destination file: " + dest_path;
    return false;
  }

  out.write(response.data(), response.size());
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

  if (response.find("\"error\"") != std::string::npos)
  {
    last_error_ = json::get_string(response, "error");
    return false;
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

  if (response.find("\"error\"") != std::string::npos)
  {
    last_error_ = json::get_string(response, "error");
    return false;
  }

  return true;
}

std::string RegistryClient::http_get(const std::string& endpoint)
{
  std::string full_url = registry_url_ + endpoint;
  std::vector<std::string> headers = {
    "Accept: application/json",
    "User-Agent: neam-pkg/1.0"
  };
  if (!auth_token_.empty())
  {
    headers.push_back("Authorization: Bearer " + auth_token_);
  }
  try
  {
    return neamc::llm::http_get(full_url, headers);
  }
  catch (const std::exception& e)
  {
    last_error_ = e.what();
    return "";
  }
}

std::string RegistryClient::http_post(const std::string& endpoint, const std::string& body)
{
  std::string full_url = registry_url_ + endpoint;
  std::vector<std::string> headers = {
    "Content-Type: application/json",
    "Accept: application/json",
    "User-Agent: neam-pkg/1.0"
  };
  if (!auth_token_.empty())
  {
    headers.push_back("Authorization: Bearer " + auth_token_);
  }
  try
  {
    return neamc::llm::http_post_json(full_url, body, headers);
  }
  catch (const std::exception& e)
  {
    last_error_ = e.what();
    return "";
  }
}

std::string RegistryClient::http_delete(const std::string& endpoint)
{
  // For now, DELETE uses the old make_request which we'll keep for compatibility
  return make_request("DELETE", endpoint, "");
}

std::string RegistryClient::make_request(const std::string& method, const std::string& endpoint,
                                         const std::string& body)
{
  // Parse URL
  std::string host;
  int port = 443;
  bool use_https = true;

  std::string url = registry_url_;
  if (url.find("https://") == 0)
  {
    url = url.substr(8);
    use_https = true;
    port = 443;
  }
  else if (url.find("http://") == 0)
  {
    url = url.substr(7);
    use_https = false;
    port = 80;
  }

  std::size_t port_pos = url.find(':');
  std::size_t path_pos = url.find('/');
  if (port_pos != std::string::npos && (path_pos == std::string::npos || port_pos < path_pos))
  {
    host = url.substr(0, port_pos);
    std::size_t port_end = (path_pos != std::string::npos) ? path_pos : url.length();
    port = std::stoi(url.substr(port_pos + 1, port_end - port_pos - 1));
  }
  else
  {
    host = (path_pos != std::string::npos) ? url.substr(0, path_pos) : url;
  }

  // For now, use a simple HTTP implementation
  // In production, you'd want to use libcurl or similar
#ifdef _WIN32
  WSADATA wsaData;
  WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

  struct hostent* server = gethostbyname(host.c_str());
  if (!server)
  {
    last_error_ = "Failed to resolve host: " + host;
    return "";
  }

  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0)
  {
    last_error_ = "Failed to create socket";
    return "";
  }

  struct sockaddr_in serv_addr;
  memset(&serv_addr, 0, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
  serv_addr.sin_port = htons(port);

  if (connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
  {
    last_error_ = "Failed to connect to " + host + ":" + std::to_string(port);
#ifdef _WIN32
    closesocket(sockfd);
#else
    close(sockfd);
#endif
    return "";
  }

  // Build HTTP request
  std::stringstream request;
  request << method << " " << endpoint << " HTTP/1.1\r\n";
  request << "Host: " << host << "\r\n";
  request << "User-Agent: neam-pkg/1.0\r\n";
  request << "Accept: application/json\r\n";

  if (!auth_token_.empty())
  {
    request << "Authorization: Bearer " << auth_token_ << "\r\n";
  }

  if (!body.empty())
  {
    request << "Content-Type: application/json\r\n";
    request << "Content-Length: " << body.size() << "\r\n";
  }

  request << "Connection: close\r\n\r\n";

  if (!body.empty())
  {
    request << body;
  }

  std::string req_str = request.str();
  send(sockfd, req_str.c_str(), req_str.size(), 0);

  // Read response
  std::string response;
  char buffer[4096];
  int bytes_read;

  while ((bytes_read = recv(sockfd, buffer, sizeof(buffer) - 1, 0)) > 0)
  {
    buffer[bytes_read] = '\0';
    response += buffer;
  }

#ifdef _WIN32
  closesocket(sockfd);
  WSACleanup();
#else
  close(sockfd);
#endif

  // Extract body from HTTP response
  std::size_t body_start = response.find("\r\n\r\n");
  if (body_start != std::string::npos)
  {
    return response.substr(body_start + 4);
  }

  return response;
}

}  // namespace neamc::pkg
