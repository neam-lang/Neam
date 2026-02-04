// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Module path resolver
//

#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace neamc::module
{
struct ModuleLocation
{
  std::string module_name;
  std::filesystem::path path;
};

class ModuleResolver
{
public:
  explicit ModuleResolver(std::filesystem::path project_root);

  std::optional<ModuleLocation> resolve(const std::vector<std::string>& module_path) const;
  const std::filesystem::path& root() const { return project_root_; }

  void set_stdlib_root(std::filesystem::path stdlib_root);

private:
  std::filesystem::path project_root_;
  std::filesystem::path stdlib_root_;
};
}  // namespace neamc::module
