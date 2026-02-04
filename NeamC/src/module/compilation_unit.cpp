// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Module compilation unit implementation
//

#include "neamc/module/compilation_unit.hpp"

namespace neamc::module
{
CompiledModule* ModuleCache::get(const std::string& path)
{
  auto it = modules_.find(path);
  if (it != modules_.end())
  {
    return &it->second;
  }
  return nullptr;
}

CompiledModule& ModuleCache::create(const std::string& path)
{
  auto& module = modules_[path];
  module.source_path = path;
  return module;
}

bool ModuleCache::has(const std::string& path) const
{
  return modules_.count(path) > 0;
}
}  // namespace neamc::module
