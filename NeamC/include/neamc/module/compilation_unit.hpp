// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Module compilation unit and cache
//

#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "neamc/module/visibility.hpp"
#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::module
{
enum class SymbolKind
{
  kFunction,
  kConstant,
  kType
};

struct ExportedSymbol
{
  std::string name;
  vm::Value value;
  VisibilityLevel visibility{VisibilityLevel::kPublic};
  SymbolKind kind{SymbolKind::kConstant};
};

struct CompiledModule
{
  std::string module_name;
  std::filesystem::path source_path;
  std::vector<ExportedSymbol> exports;
  vm::Chunk chunk;
  bool is_compiled{false};
  bool is_compiling{false};  // For cycle detection
};

class ModuleCache
{
public:
  CompiledModule* get(const std::string& path);
  CompiledModule& create(const std::string& path);
  bool has(const std::string& path) const;

private:
  std::unordered_map<std::string, CompiledModule> modules_;
};
}  // namespace neamc::module
