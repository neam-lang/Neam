// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Module visibility rules implementation
//

#include "neamc/module/visibility.hpp"

#include <string>
#include <string_view>

namespace neamc::module
{
namespace
{
std::string_view root_segment(std::string_view module_path)
{
  const auto dot = module_path.find('.');
  if (dot == std::string_view::npos)
  {
    return module_path;
  }
  return module_path.substr(0, dot);
}

std::string_view parent_module(std::string_view module_path)
{
  const auto dot = module_path.rfind('.');
  if (dot == std::string_view::npos)
  {
    return {};
  }
  return module_path.substr(0, dot);
}
}  // namespace

bool is_accessible(std::string_view from_module, std::string_view target_module,
                   VisibilityLevel visibility)
{
  switch (visibility)
  {
    case VisibilityLevel::kPublic:
      return true;
    case VisibilityLevel::kPrivate:
      return from_module == target_module;
    case VisibilityLevel::kCrate:
      return root_segment(from_module) == root_segment(target_module);
    case VisibilityLevel::kSuper:
      return parent_module(target_module) == from_module;
  }
  return false;
}
}  // namespace neamc::module
