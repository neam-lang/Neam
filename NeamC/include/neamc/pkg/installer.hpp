//
// Neam Package Manager - Package Installer
//

#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace neamc::pkg
{

// Resolved dependency
struct ResolvedDependency
{
  std::string name;
  std::string version;
  std::string source;  // "registry", "git", "path"
  std::string location;
  std::vector<std::string> features;
  bool dev_only = false;
};

// Installation progress callback
using ProgressCallback = std::function<void(const std::string& package, const std::string& status,
                                            double progress)>;

// Package installer
class Installer
{
public:
  explicit Installer(const std::filesystem::path& project_root);

  // Set progress callback
  void set_progress_callback(ProgressCallback callback);

  // Install operations
  bool install_all();  // Install all dependencies from neam.toml
  bool install_package(const std::string& name, const std::string& version = "");
  bool install_dev_package(const std::string& name, const std::string& version = "");

  // Update operations
  bool update_all();
  bool update_package(const std::string& name);

  // Remove operations
  bool remove_package(const std::string& name);

  // Query operations
  std::vector<ResolvedDependency> list_installed();
  std::vector<std::pair<std::string, std::string>> list_outdated();

  // Lock file
  bool generate_lock_file();
  bool verify_lock_file();

  // Error handling
  std::string last_error() const { return last_error_; }

private:
  std::filesystem::path project_root_;
  std::filesystem::path packages_dir_;
  ProgressCallback progress_callback_;
  std::string last_error_;

  // Progress reporting
  void report_progress(const std::string& package, const std::string& status, double progress);

  // Installation implementation
  bool install_package_impl(const std::string& name, const std::string& version, bool dev);

  // Dependency resolution
  std::vector<ResolvedDependency> resolve_dependencies();
  bool resolve_version(const std::string& name, const std::string& version_req,
                       std::string& resolved_version);

  // Installation helpers
  bool download_and_extract(const ResolvedDependency& dep);
  bool link_package(const ResolvedDependency& dep);
  bool verify_checksum(const std::filesystem::path& file, const std::string& expected);

  // Lock file helpers
  bool read_lock_file(std::vector<ResolvedDependency>& deps);
  bool write_lock_file(const std::vector<ResolvedDependency>& deps);
};

// Global package cache
class PackageCache
{
public:
  static PackageCache& instance();

  std::filesystem::path cache_dir() const;
  std::filesystem::path package_path(const std::string& name, const std::string& version) const;

  bool has_package(const std::string& name, const std::string& version) const;
  bool add_package(const std::string& name, const std::string& version,
                   const std::filesystem::path& source);
  bool remove_package(const std::string& name, const std::string& version);

  void clean_cache();
  std::size_t cache_size() const;

private:
  PackageCache();
  std::filesystem::path cache_dir_;
};

}  // namespace neamc::pkg
