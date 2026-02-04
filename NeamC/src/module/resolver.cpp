// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Module path resolver implementation
//

#include "neamc/module/resolver.hpp"

#include <filesystem>

namespace neamc::module
{
namespace fs = std::filesystem;

ModuleResolver::ModuleResolver(std::filesystem::path project_root)
    : project_root_(std::move(project_root))
{
}

void ModuleResolver::set_stdlib_root(std::filesystem::path stdlib_root)
{
  stdlib_root_ = std::move(stdlib_root);
}

std::optional<ModuleLocation> ModuleResolver::resolve(
    const std::vector<std::string>& module_path) const
{
  if (module_path.empty())
  {
    return std::nullopt;
  }

  // Determine base directory: stdlib for "std" prefix, project src otherwise
  fs::path base;
  std::size_t start_index = 0;

  if (module_path[0] == "std" && !stdlib_root_.empty())
  {
    base = stdlib_root_;
    start_index = 1;  // Skip "std" prefix
    if (start_index >= module_path.size())
    {
      return std::nullopt;
    }
  }
  else
  {
    base = project_root_ / "src";
  }

  // Try direct file path: base/segment1/segment2/.../last.neam
  fs::path file_path = base;
  for (std::size_t i = start_index; i < module_path.size(); ++i)
  {
    if (i + 1 == module_path.size())
    {
      file_path /= module_path[i] + ".neam";
    }
    else
    {
      file_path /= module_path[i];
    }
  }

  if (fs::exists(file_path))
  {
    return ModuleLocation{module_path.back(), file_path};
  }

  // Try mod.neam path: base/segment1/segment2/.../last/mod.neam
  fs::path mod_path = base;
  for (std::size_t i = start_index; i < module_path.size(); ++i)
  {
    mod_path /= module_path[i];
  }
  mod_path /= "mod.neam";
  if (fs::exists(mod_path))
  {
    return ModuleLocation{module_path.back(), mod_path};
  }

  return std::nullopt;
}
}  // namespace neamc::module
