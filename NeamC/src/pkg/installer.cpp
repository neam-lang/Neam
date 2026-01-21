//
// Neam Package Manager - Package Installer Implementation
//

#include "neamc/pkg/installer.hpp"
#include "neamc/pkg/registry.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace neamc::pkg
{

// Semver comparison helpers
namespace semver
{
struct Version
{
  int major = 0;
  int minor = 0;
  int patch = 0;
  std::string prerelease;

  static Version parse(const std::string& str)
  {
    Version v;
    std::regex pattern("(\\d+)\\.(\\d+)\\.(\\d+)(?:-(.+))?");
    std::smatch match;
    if (std::regex_match(str, match, pattern))
    {
      v.major = std::stoi(match[1].str());
      v.minor = std::stoi(match[2].str());
      v.patch = std::stoi(match[3].str());
      if (match.size() > 4)
      {
        v.prerelease = match[4].str();
      }
    }
    return v;
  }

  bool operator<(const Version& other) const
  {
    if (major != other.major)
      return major < other.major;
    if (minor != other.minor)
      return minor < other.minor;
    if (patch != other.patch)
      return patch < other.patch;
    if (prerelease.empty() && !other.prerelease.empty())
      return false;
    if (!prerelease.empty() && other.prerelease.empty())
      return true;
    return prerelease < other.prerelease;
  }

  bool operator==(const Version& other) const
  {
    return major == other.major && minor == other.minor && patch == other.patch &&
           prerelease == other.prerelease;
  }

  bool operator<=(const Version& other) const { return *this < other || *this == other; }
  bool operator>(const Version& other) const { return !(*this <= other); }
  bool operator>=(const Version& other) const { return !(*this < other); }
};

bool satisfies(const std::string& version, const std::string& requirement)
{
  if (requirement.empty() || requirement == "*")
  {
    return true;
  }

  Version v = Version::parse(version);

  // Exact match
  if (requirement[0] != '^' && requirement[0] != '~' && requirement[0] != '>' && requirement[0] != '<')
  {
    return version == requirement;
  }

  // Caret (^): Compatible with version
  if (requirement[0] == '^')
  {
    Version req = Version::parse(requirement.substr(1));
    if (v.major != req.major)
      return false;
    if (v.major == 0)
    {
      if (v.minor != req.minor)
        return false;
      return v >= req;
    }
    return v >= req;
  }

  // Tilde (~): Approximately equivalent
  if (requirement[0] == '~')
  {
    Version req = Version::parse(requirement.substr(1));
    if (v.major != req.major)
      return false;
    if (v.minor != req.minor)
      return false;
    return v >= req;
  }

  // Greater than
  if (requirement.substr(0, 2) == ">=")
  {
    Version req = Version::parse(requirement.substr(2));
    return v >= req;
  }
  if (requirement[0] == '>')
  {
    Version req = Version::parse(requirement.substr(1));
    return v > req;
  }

  // Less than
  if (requirement.substr(0, 2) == "<=")
  {
    Version req = Version::parse(requirement.substr(2));
    return v <= req;
  }
  if (requirement[0] == '<')
  {
    Version req = Version::parse(requirement.substr(1));
    return v < req;
  }

  return false;
}

std::string find_best_match(const std::vector<std::string>& versions, const std::string& requirement)
{
  std::string best;
  for (const auto& ver : versions)
  {
    if (satisfies(ver, requirement))
    {
      if (best.empty() || Version::parse(ver) > Version::parse(best))
      {
        best = ver;
      }
    }
  }
  return best;
}
}  // namespace semver

// TOML parser helpers
namespace toml
{
std::string get_string(const std::string& content, const std::string& section, const std::string& key)
{
  std::string section_pattern = "\\[" + section + "\\]";
  std::regex section_regex(section_pattern);
  std::smatch section_match;

  if (!std::regex_search(content, section_match, section_regex))
  {
    return "";
  }

  std::string after_section = content.substr(section_match.position() + section_match.length());
  std::size_t next_section = after_section.find('[');
  if (next_section != std::string::npos)
  {
    after_section = after_section.substr(0, next_section);
  }

  std::regex key_pattern(key + "\\s*=\\s*\"([^\"]*)\"");
  std::smatch key_match;
  if (std::regex_search(after_section, key_match, key_pattern))
  {
    return key_match[1].str();
  }

  return "";
}

std::map<std::string, std::string> get_dependencies(const std::string& content, const std::string& section)
{
  std::map<std::string, std::string> deps;

  std::string section_pattern = "\\[" + section + "\\]";
  std::regex section_regex(section_pattern);
  std::smatch section_match;

  if (!std::regex_search(content, section_match, section_regex))
  {
    return deps;
  }

  std::string after_section = content.substr(section_match.position() + section_match.length());
  std::size_t next_section = after_section.find('[');
  if (next_section != std::string::npos)
  {
    after_section = after_section.substr(0, next_section);
  }

  // Simple dependency: name = "version"
  std::regex simple_dep("(\\w+)\\s*=\\s*\"([^\"]+)\"");
  auto begin = std::sregex_iterator(after_section.begin(), after_section.end(), simple_dep);
  auto end = std::sregex_iterator();

  for (auto i = begin; i != end; ++i)
  {
    deps[(*i)[1].str()] = (*i)[2].str();
  }

  // Complex dependency: name = { version = "..." }
  std::regex complex_dep("(\\w+)\\s*=\\s*\\{[^}]*version\\s*=\\s*\"([^\"]+)\"[^}]*\\}");
  begin = std::sregex_iterator(after_section.begin(), after_section.end(), complex_dep);
  for (auto i = begin; i != end; ++i)
  {
    deps[(*i)[1].str()] = (*i)[2].str();
  }

  return deps;
}
}  // namespace toml

// Installer implementation

Installer::Installer(const std::filesystem::path& project_root) : project_root_(project_root)
{
  packages_dir_ = project_root_ / ".neam" / "packages";
  std::filesystem::create_directories(packages_dir_);
}

void Installer::set_progress_callback(ProgressCallback callback)
{
  progress_callback_ = callback;
}

void Installer::report_progress(const std::string& package, const std::string& status, double progress)
{
  if (progress_callback_)
  {
    progress_callback_(package, status, progress);
  }
}

bool Installer::install_all()
{
  // Read neam.toml
  std::filesystem::path manifest_path = project_root_ / "neam.toml";
  if (!std::filesystem::exists(manifest_path))
  {
    last_error_ = "No neam.toml found in project root";
    return false;
  }

  std::ifstream manifest_file(manifest_path);
  std::stringstream buffer;
  buffer << manifest_file.rdbuf();
  std::string manifest_content = buffer.str();

  // Get dependencies
  auto deps = toml::get_dependencies(manifest_content, "dependencies");
  auto dev_deps = toml::get_dependencies(manifest_content, "dev-dependencies");

  // Resolve all dependencies
  auto resolved = resolve_dependencies();
  if (resolved.empty() && (!deps.empty() || !dev_deps.empty()))
  {
    return false;
  }

  // Install each resolved dependency
  int total = resolved.size();
  int current = 0;

  for (const auto& dep : resolved)
  {
    report_progress(dep.name, "Installing", static_cast<double>(current) / total);

    if (!download_and_extract(dep))
    {
      return false;
    }

    if (!link_package(dep))
    {
      return false;
    }

    ++current;
  }

  // Generate lock file
  if (!generate_lock_file())
  {
    return false;
  }

  report_progress("", "Complete", 1.0);
  return true;
}

bool Installer::install_package(const std::string& name, const std::string& version)
{
  return install_package_impl(name, version, false);
}

bool Installer::install_dev_package(const std::string& name, const std::string& version)
{
  return install_package_impl(name, version, true);
}

bool Installer::install_package_impl(const std::string& name, const std::string& version, bool dev)
{
  report_progress(name, "Resolving", 0.0);

  // Resolve version
  std::string resolved_version;
  if (!resolve_version(name, version, resolved_version))
  {
    return false;
  }

  // Download and install
  ResolvedDependency dep;
  dep.name = name;
  dep.version = resolved_version;
  dep.source = "registry";
  dep.dev_only = dev;

  report_progress(name, "Downloading", 0.3);
  if (!download_and_extract(dep))
  {
    return false;
  }

  report_progress(name, "Linking", 0.7);
  if (!link_package(dep))
  {
    return false;
  }

  // Add to neam.toml
  std::filesystem::path manifest_path = project_root_ / "neam.toml";
  std::ifstream manifest_file(manifest_path);
  std::stringstream buffer;
  buffer << manifest_file.rdbuf();
  std::string content = buffer.str();
  manifest_file.close();

  std::string section = dev ? "[dev-dependencies]" : "[dependencies]";
  std::size_t section_pos = content.find(section);

  if (section_pos == std::string::npos)
  {
    content += "\n" + section + "\n";
    content += name + " = \"" + resolved_version + "\"\n";
  }
  else
  {
    std::size_t insert_pos = content.find('\n', section_pos) + 1;
    content.insert(insert_pos, name + " = \"" + resolved_version + "\"\n");
  }

  std::ofstream out_file(manifest_path);
  out_file << content;

  // Update lock file
  generate_lock_file();

  report_progress(name, "Complete", 1.0);
  return true;
}

bool Installer::update_all()
{
  // Read current lock file
  std::vector<ResolvedDependency> locked_deps;
  read_lock_file(locked_deps);

  // Check for updates
  RegistryClient registry;

  for (auto& dep : locked_deps)
  {
    auto pkg_info = registry.get_package(dep.name);
    if (!pkg_info)
    {
      continue;
    }

    // Find latest compatible version
    std::vector<std::string> available_versions;
    for (const auto& ver : pkg_info->versions)
    {
      if (!ver.yanked)
      {
        available_versions.push_back(ver.version);
      }
    }

    // Read version requirement from neam.toml
    std::filesystem::path manifest_path = project_root_ / "neam.toml";
    std::ifstream manifest_file(manifest_path);
    std::stringstream buffer;
    buffer << manifest_file.rdbuf();
    std::string manifest_content = buffer.str();

    auto deps = toml::get_dependencies(manifest_content, "dependencies");
    auto dev_deps = toml::get_dependencies(manifest_content, "dev-dependencies");

    std::string version_req = deps.count(dep.name) ? deps[dep.name] : dev_deps[dep.name];

    std::string best_version = semver::find_best_match(available_versions, version_req);
    if (!best_version.empty() && best_version != dep.version)
    {
      report_progress(dep.name, "Updating to " + best_version, 0.5);
      dep.version = best_version;

      if (!download_and_extract(dep))
      {
        return false;
      }
    }
  }

  return generate_lock_file();
}

bool Installer::update_package(const std::string& name)
{
  RegistryClient registry;
  auto pkg_info = registry.get_package(name);

  if (!pkg_info)
  {
    last_error_ = "Package not found: " + name;
    return false;
  }

  // Find latest version
  std::string latest;
  for (const auto& ver : pkg_info->versions)
  {
    if (!ver.yanked)
    {
      if (latest.empty() || semver::Version::parse(ver.version) > semver::Version::parse(latest))
      {
        latest = ver.version;
      }
    }
  }

  if (latest.empty())
  {
    last_error_ = "No available versions for: " + name;
    return false;
  }

  return install_package(name, latest);
}

bool Installer::remove_package(const std::string& name)
{
  // Remove from packages directory
  std::filesystem::path pkg_path = packages_dir_ / name;
  if (std::filesystem::exists(pkg_path))
  {
    std::filesystem::remove_all(pkg_path);
  }

  // Remove from neam.toml
  std::filesystem::path manifest_path = project_root_ / "neam.toml";
  std::ifstream manifest_file(manifest_path);
  std::stringstream buffer;
  buffer << manifest_file.rdbuf();
  std::string content = buffer.str();
  manifest_file.close();

  // Remove dependency line
  std::regex dep_pattern(name + "\\s*=\\s*[^\n]+\n");
  content = std::regex_replace(content, dep_pattern, "");

  std::ofstream out_file(manifest_path);
  out_file << content;

  // Update lock file
  return generate_lock_file();
}

std::vector<ResolvedDependency> Installer::list_installed()
{
  std::vector<ResolvedDependency> installed;

  if (!std::filesystem::exists(packages_dir_))
  {
    return installed;
  }

  for (const auto& entry : std::filesystem::directory_iterator(packages_dir_))
  {
    if (entry.is_directory())
    {
      ResolvedDependency dep;
      dep.name = entry.path().filename().string();

      // Read version from package's neam.toml
      std::filesystem::path pkg_manifest = entry.path() / "neam.toml";
      if (std::filesystem::exists(pkg_manifest))
      {
        std::ifstream file(pkg_manifest);
        std::stringstream buffer;
        buffer << file.rdbuf();
        dep.version = toml::get_string(buffer.str(), "project", "version");
      }

      dep.location = entry.path().string();
      installed.push_back(dep);
    }
  }

  return installed;
}

std::vector<std::pair<std::string, std::string>> Installer::list_outdated()
{
  std::vector<std::pair<std::string, std::string>> outdated;
  auto installed = list_installed();

  RegistryClient registry;

  for (const auto& dep : installed)
  {
    auto pkg_info = registry.get_package(dep.name);
    if (!pkg_info)
    {
      continue;
    }

    // Find latest version
    std::string latest;
    for (const auto& ver : pkg_info->versions)
    {
      if (!ver.yanked)
      {
        if (latest.empty() || semver::Version::parse(ver.version) > semver::Version::parse(latest))
        {
          latest = ver.version;
        }
      }
    }

    if (!latest.empty() && semver::Version::parse(latest) > semver::Version::parse(dep.version))
    {
      outdated.push_back({dep.name, latest});
    }
  }

  return outdated;
}

bool Installer::generate_lock_file()
{
  auto resolved = resolve_dependencies();

  std::filesystem::path lock_path = project_root_ / "neam.lock";
  std::ofstream lock_file(lock_path);

  if (!lock_file.is_open())
  {
    last_error_ = "Failed to create lock file";
    return false;
  }

  lock_file << "# neam.lock - Auto-generated, do not edit manually\n\n";

  for (const auto& dep : resolved)
  {
    lock_file << "[[package]]\n";
    lock_file << "name = \"" << dep.name << "\"\n";
    lock_file << "version = \"" << dep.version << "\"\n";
    lock_file << "source = \"" << dep.source << "\"\n";

    if (!dep.location.empty() && dep.source != "registry")
    {
      lock_file << "location = \"" << dep.location << "\"\n";
    }

    if (dep.dev_only)
    {
      lock_file << "dev-only = true\n";
    }

    lock_file << "\n";
  }

  return true;
}

bool Installer::verify_lock_file()
{
  std::filesystem::path lock_path = project_root_ / "neam.lock";
  if (!std::filesystem::exists(lock_path))
  {
    last_error_ = "Lock file not found";
    return false;
  }

  std::vector<ResolvedDependency> locked_deps;
  if (!read_lock_file(locked_deps))
  {
    return false;
  }

  // Verify each locked dependency is installed
  for (const auto& dep : locked_deps)
  {
    std::filesystem::path pkg_path = packages_dir_ / dep.name;
    if (!std::filesystem::exists(pkg_path))
    {
      last_error_ = "Missing package: " + dep.name;
      return false;
    }

    // Verify version matches
    std::filesystem::path pkg_manifest = pkg_path / "neam.toml";
    if (std::filesystem::exists(pkg_manifest))
    {
      std::ifstream file(pkg_manifest);
      std::stringstream buffer;
      buffer << file.rdbuf();
      std::string installed_version = toml::get_string(buffer.str(), "project", "version");

      if (installed_version != dep.version)
      {
        last_error_ = "Version mismatch for " + dep.name + ": expected " + dep.version + ", found " +
                      installed_version;
        return false;
      }
    }
  }

  return true;
}

std::vector<ResolvedDependency> Installer::resolve_dependencies()
{
  std::vector<ResolvedDependency> resolved;

  // Read neam.toml
  std::filesystem::path manifest_path = project_root_ / "neam.toml";
  if (!std::filesystem::exists(manifest_path))
  {
    return resolved;
  }

  std::ifstream manifest_file(manifest_path);
  std::stringstream buffer;
  buffer << manifest_file.rdbuf();
  std::string manifest_content = buffer.str();

  auto deps = toml::get_dependencies(manifest_content, "dependencies");
  auto dev_deps = toml::get_dependencies(manifest_content, "dev-dependencies");

  // Resolve regular dependencies
  for (const auto& [name, version_req] : deps)
  {
    std::string resolved_version;
    if (resolve_version(name, version_req, resolved_version))
    {
      ResolvedDependency dep;
      dep.name = name;
      dep.version = resolved_version;
      dep.source = "registry";
      dep.dev_only = false;
      resolved.push_back(dep);
    }
  }

  // Resolve dev dependencies
  for (const auto& [name, version_req] : dev_deps)
  {
    std::string resolved_version;
    if (resolve_version(name, version_req, resolved_version))
    {
      ResolvedDependency dep;
      dep.name = name;
      dep.version = resolved_version;
      dep.source = "registry";
      dep.dev_only = true;
      resolved.push_back(dep);
    }
  }

  return resolved;
}

bool Installer::resolve_version(const std::string& name, const std::string& version_req,
                                std::string& resolved_version)
{
  // If exact version specified
  if (!version_req.empty() && version_req[0] != '^' && version_req[0] != '~' && version_req[0] != '>' &&
      version_req[0] != '<')
  {
    resolved_version = version_req;
    return true;
  }

  // Query registry for available versions
  RegistryClient registry;
  auto pkg_info = registry.get_package(name);

  if (!pkg_info)
  {
    last_error_ = "Package not found: " + name;
    return false;
  }

  // Collect available versions
  std::vector<std::string> available_versions;
  for (const auto& ver : pkg_info->versions)
  {
    if (!ver.yanked)
    {
      available_versions.push_back(ver.version);
    }
  }

  // Find best matching version
  resolved_version = semver::find_best_match(available_versions, version_req);

  if (resolved_version.empty())
  {
    last_error_ = "No version satisfies requirement: " + name + " " + version_req;
    return false;
  }

  return true;
}

bool Installer::download_and_extract(const ResolvedDependency& dep)
{
  // Check global cache first
  PackageCache& cache = PackageCache::instance();
  if (cache.has_package(dep.name, dep.version))
  {
    // Copy from cache
    std::filesystem::path cache_path = cache.package_path(dep.name, dep.version);
    std::filesystem::path dest_path = packages_dir_ / dep.name;
    std::filesystem::create_directories(dest_path);
    std::filesystem::copy(cache_path, dest_path, std::filesystem::copy_options::recursive |
                                                     std::filesystem::copy_options::overwrite_existing);
    return true;
  }

  // Download from registry
  RegistryClient registry;

  std::filesystem::path temp_dir = std::filesystem::temp_directory_path();
  std::filesystem::path archive_path = temp_dir / (dep.name + "-" + dep.version + ".neampkg");

  if (!registry.download_package(dep.name, dep.version, archive_path.string()))
  {
    last_error_ = registry.last_error();
    return false;
  }

  // Extract archive (simplified - just copy for now)
  std::filesystem::path dest_path = packages_dir_ / dep.name;
  std::filesystem::create_directories(dest_path);

  // In a real implementation, you would extract a tar.gz or zip archive
  // For now, we'll assume the package is a directory
  std::filesystem::copy(archive_path, dest_path, std::filesystem::copy_options::recursive);

  // Add to cache
  cache.add_package(dep.name, dep.version, dest_path);

  // Clean up temp file
  std::filesystem::remove(archive_path);

  return true;
}

bool Installer::link_package(const ResolvedDependency& dep)
{
  // Create symlink or copy to project packages directory
  std::filesystem::path src_path = packages_dir_ / dep.name;
  std::filesystem::path link_path = project_root_ / ".neam" / "packages" / dep.name;

  if (src_path == link_path)
  {
    return true;  // Already in place
  }

  std::filesystem::create_directories(link_path.parent_path());

  if (std::filesystem::exists(link_path))
  {
    std::filesystem::remove_all(link_path);
  }

  // Create symlink if possible, otherwise copy
  std::error_code ec;
  std::filesystem::create_symlink(src_path, link_path, ec);

  if (ec)
  {
    // Fall back to copy
    std::filesystem::copy(src_path, link_path, std::filesystem::copy_options::recursive);
  }

  return true;
}

bool Installer::verify_checksum(const std::filesystem::path& file, const std::string& expected)
{
  // Simplified checksum verification
  // In a real implementation, compute SHA256 and compare
  if (expected.empty())
  {
    return true;
  }

  // TODO: Implement actual SHA256 checksum verification
  return true;
}

bool Installer::read_lock_file(std::vector<ResolvedDependency>& deps)
{
  std::filesystem::path lock_path = project_root_ / "neam.lock";
  if (!std::filesystem::exists(lock_path))
  {
    return true;
  }

  std::ifstream lock_file(lock_path);
  std::stringstream buffer;
  buffer << lock_file.rdbuf();
  std::string content = buffer.str();

  // Parse lock file
  std::regex package_pattern("\\[\\[package\\]\\]([^\\[]+)");
  auto begin = std::sregex_iterator(content.begin(), content.end(), package_pattern);
  auto end = std::sregex_iterator();

  for (auto i = begin; i != end; ++i)
  {
    std::string pkg_content = (*i)[1].str();

    ResolvedDependency dep;

    std::regex name_pattern("name\\s*=\\s*\"([^\"]+)\"");
    std::smatch name_match;
    if (std::regex_search(pkg_content, name_match, name_pattern))
    {
      dep.name = name_match[1].str();
    }

    std::regex version_pattern("version\\s*=\\s*\"([^\"]+)\"");
    std::smatch version_match;
    if (std::regex_search(pkg_content, version_match, version_pattern))
    {
      dep.version = version_match[1].str();
    }

    std::regex source_pattern("source\\s*=\\s*\"([^\"]+)\"");
    std::smatch source_match;
    if (std::regex_search(pkg_content, source_match, source_pattern))
    {
      dep.source = source_match[1].str();
    }

    std::regex dev_pattern("dev-only\\s*=\\s*true");
    dep.dev_only = std::regex_search(pkg_content, dev_pattern);

    if (!dep.name.empty())
    {
      deps.push_back(dep);
    }
  }

  return true;
}

bool Installer::write_lock_file(const std::vector<ResolvedDependency>& deps)
{
  std::filesystem::path lock_path = project_root_ / "neam.lock";
  std::ofstream lock_file(lock_path);

  if (!lock_file.is_open())
  {
    last_error_ = "Failed to write lock file";
    return false;
  }

  lock_file << "# neam.lock - Auto-generated, do not edit manually\n\n";

  for (const auto& dep : deps)
  {
    lock_file << "[[package]]\n";
    lock_file << "name = \"" << dep.name << "\"\n";
    lock_file << "version = \"" << dep.version << "\"\n";
    lock_file << "source = \"" << dep.source << "\"\n";
    if (dep.dev_only)
    {
      lock_file << "dev-only = true\n";
    }
    lock_file << "\n";
  }

  return true;
}

// PackageCache implementation

PackageCache& PackageCache::instance()
{
  static PackageCache instance;
  return instance;
}

PackageCache::PackageCache()
{
  const char* home = std::getenv("HOME");
  if (home)
  {
    cache_dir_ = std::filesystem::path(home) / ".neam" / "cache" / "packages";
  }
  else
  {
    cache_dir_ = std::filesystem::temp_directory_path() / ".neam" / "cache" / "packages";
  }
  std::filesystem::create_directories(cache_dir_);
}

std::filesystem::path PackageCache::cache_dir() const
{
  return cache_dir_;
}

std::filesystem::path PackageCache::package_path(const std::string& name, const std::string& version) const
{
  return cache_dir_ / name / version;
}

bool PackageCache::has_package(const std::string& name, const std::string& version) const
{
  return std::filesystem::exists(package_path(name, version));
}

bool PackageCache::add_package(const std::string& name, const std::string& version,
                               const std::filesystem::path& source)
{
  std::filesystem::path dest = package_path(name, version);
  std::filesystem::create_directories(dest.parent_path());

  std::error_code ec;
  std::filesystem::copy(source, dest,
                        std::filesystem::copy_options::recursive | std::filesystem::copy_options::overwrite_existing,
                        ec);

  return !ec;
}

bool PackageCache::remove_package(const std::string& name, const std::string& version)
{
  std::filesystem::path pkg_path = package_path(name, version);
  if (std::filesystem::exists(pkg_path))
  {
    std::filesystem::remove_all(pkg_path);
    return true;
  }
  return false;
}

void PackageCache::clean_cache()
{
  if (std::filesystem::exists(cache_dir_))
  {
    std::filesystem::remove_all(cache_dir_);
    std::filesystem::create_directories(cache_dir_);
  }
}

std::size_t PackageCache::cache_size() const
{
  std::size_t total = 0;

  if (!std::filesystem::exists(cache_dir_))
  {
    return 0;
  }

  for (const auto& entry : std::filesystem::recursive_directory_iterator(cache_dir_))
  {
    if (entry.is_regular_file())
    {
      total += entry.file_size();
    }
  }

  return total;
}

}  // namespace neamc::pkg
