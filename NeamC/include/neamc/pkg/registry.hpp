//
// Neam Package Manager - Registry Client
//

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace neamc::pkg
{

// Package version info
struct VersionInfo
{
  std::string version;
  std::string checksum;
  std::string neam_version;
  std::size_t size_bytes = 0;
  std::string published_at;
  bool yanked = false;
  std::vector<std::pair<std::string, std::string>> dependencies;  // name, version_req
};

// Package metadata
struct PackageInfo
{
  std::string name;
  std::string description;
  std::string repository;
  std::string documentation;
  std::string homepage;
  std::string license;
  std::vector<std::string> authors;
  std::vector<std::string> keywords;
  std::vector<VersionInfo> versions;
  std::size_t downloads = 0;
};

// Search result
struct SearchResult
{
  std::string name;
  std::string description;
  std::string latest_version;
  std::size_t downloads = 0;
};

// Registry client
class RegistryClient
{
public:
  explicit RegistryClient(const std::string& registry_url = "https://registry.neam.dev");

  // Authentication
  bool login(const std::string& username, const std::string& password);
  bool logout();
  bool is_authenticated() const;
  std::string get_username() const;

  // Package queries
  std::optional<PackageInfo> get_package(const std::string& name);
  std::optional<VersionInfo> get_version(const std::string& name, const std::string& version);
  std::vector<SearchResult> search(const std::string& query, int limit = 20);
  std::vector<SearchResult> get_popular(int limit = 20);
  std::vector<SearchResult> get_recent(int limit = 20);

  // Package download
  bool download_package(const std::string& name, const std::string& version,
                        const std::string& dest_path);

  // Package publishing
  bool publish_package(const std::string& package_path);
  bool yank_version(const std::string& name, const std::string& version);

  // Error handling
  std::string last_error() const { return last_error_; }

private:
  std::string registry_url_;
  std::string auth_token_;
  std::string username_;
  std::string last_error_;

  std::string http_get(const std::string& endpoint);
  std::string http_post(const std::string& endpoint, const std::string& body);
  std::string http_delete(const std::string& endpoint);
  std::string make_request(const std::string& method, const std::string& endpoint, const std::string& body);
  std::vector<SearchResult> parse_search_results(const std::string& response);
};

}  // namespace neamc::pkg
