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

std::optional<ModuleLocation> ModuleResolver::resolve(
    const std::vector<std::string>& module_path) const
{
  if (module_path.empty())
  {
    return std::nullopt;
  }

  fs::path base = project_root_ / "src";
  fs::path file_path = base;
  for (std::size_t i = 0; i < module_path.size(); ++i)
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

  fs::path mod_path = base;
  for (const auto& segment : module_path)
  {
    mod_path /= segment;
  }
  mod_path /= "mod.neam";
  if (fs::exists(mod_path))
  {
    return ModuleLocation{module_path.back(), mod_path};
  }

  return std::nullopt;
}
}  // namespace neamc::module
