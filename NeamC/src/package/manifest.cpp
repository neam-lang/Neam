//
// Neam Package Manager - Manifest & Lockfile implementation
//

#include "neamc/package/manifest.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace neamc::package
{
namespace
{
std::string trim(const std::string& value)
{
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
  {
    ++start;
  }
  std::size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
  {
    --end;
  }
  return value.substr(start, end - start);
}

std::string unquote(const std::string& value)
{
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
  {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

std::string parse_inline_git(const std::string& value)
{
  const auto git_pos = value.find("git");
  if (git_pos == std::string::npos)
  {
    return {};
  }
  const auto quote_start = value.find('"', git_pos);
  if (quote_start == std::string::npos)
  {
    return {};
  }
  const auto quote_end = value.find('"', quote_start + 1);
  if (quote_end == std::string::npos)
  {
    return {};
  }
  return value.substr(quote_start + 1, quote_end - quote_start - 1);
}
}  // namespace

PackageManifest PackageManifest::parse(const std::string& toml)
{
  PackageManifest manifest;
  std::string section;
  std::istringstream input(toml);
  std::string line;
  while (std::getline(input, line))
  {
    const auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#')
    {
      continue;
    }
    if (trimmed.front() == '[' && trimmed.back() == ']')
    {
      section = trimmed.substr(1, trimmed.size() - 2);
      continue;
    }
    const auto eq_pos = trimmed.find('=');
    if (eq_pos == std::string::npos)
    {
      continue;
    }
    const std::string key = trim(trimmed.substr(0, eq_pos));
    const std::string value = trim(trimmed.substr(eq_pos + 1));

    if (section == "package")
    {
      if (key == "name")
      {
        manifest.name = unquote(value);
      }
      else if (key == "version")
      {
        manifest.version = unquote(value);
      }
    }
    else if (section == "dependencies")
    {
      Dependency dep;
      dep.name = key;
      if (!value.empty() && value.front() == '{')
      {
        dep.git = parse_inline_git(value);
      }
      else
      {
        dep.git = unquote(value);
      }
      manifest.dependencies.push_back(std::move(dep));
    }
  }
  return manifest;
}

PackageLock PackageLock::from_manifest(const PackageManifest& manifest)
{
  PackageLock lock;
  for (const auto& dep : manifest.dependencies)
  {
    lock.entries.push_back(LockEntry{dep.name, dep.git, "HEAD"});
  }
  return lock;
}

std::string PackageLock::serialize() const
{
  std::ostringstream out;
  out << "# neam.lock\n";
  for (const auto& entry : entries)
  {
    out << "[[package]]\n";
    out << "name = \"" << entry.name << "\"\n";
    out << "git = \"" << entry.git << "\"\n";
    out << "revision = \"" << entry.revision << "\"\n";
    out << "\n";
  }
  return out.str();
}
}  // namespace neamc::package
