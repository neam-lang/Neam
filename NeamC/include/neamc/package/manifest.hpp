// SPDX-License-Identifier: Apache-2.0
//
// Neam Package Manager - Manifest & Lockfile support
//

#pragma once

#include <string>
#include <vector>

namespace neamc::package
{
struct Dependency
{
  std::string name;
  std::string git;
};

struct PackageManifest
{
  std::string name;
  std::string version;
  std::vector<Dependency> dependencies;

  static PackageManifest parse(const std::string& toml);
};

struct LockEntry
{
  std::string name;
  std::string git;
  std::string revision;
};

struct PackageLock
{
  std::vector<LockEntry> entries;

  std::string serialize() const;
  static PackageLock from_manifest(const PackageManifest& manifest);
};
}  // namespace neamc::package
