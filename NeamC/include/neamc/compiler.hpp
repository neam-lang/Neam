// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Compiler backend translating AST to bytecode
//

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "neamc/ast.hpp"
#include "neamc/module/compilation_unit.hpp"
#include "neamc/module/graph.hpp"
#include "neamc/module/resolver.hpp"
#include "neamc/vm/bytecode.hpp"

namespace neamc
{
class Compiler
{
public:
  vm::Chunk compile(const Program& program);

  void set_module_context(module::ModuleResolver* resolver,
                          module::ModuleCache* module_cache,
                          module::ModuleGraph* dep_graph,
                          const std::string& current_module);

private:
  struct Local
  {
    std::string name;
    int depth = 0;
  };

  vm::Value compile_function(const FunctionDecl& decl);
  vm::Value compile_block_function(const std::string& name,
                                   const std::vector<std::string>& parameters,
                                   const BlockStmt& body);
  void begin_scope();
  void end_scope();
  int resolve_local(const std::string& name) const;

  void emit_statement(const Statement& stmt);
  void emit_expression(const Expression& expr);
  void emit_block(const BlockStmt& block);

  // Module system
  void compile_import(const ImportDecl& node);
  void compile_module_file(const std::string& module_name,
                           const std::filesystem::path& path);

  vm::Chunk chunk_{};
  std::vector<Local> locals_{};
  int scope_depth_ = 0;

  // Module context (optional; null if not in module-aware mode)
  module::ModuleResolver* resolver_{nullptr};
  module::ModuleCache* module_cache_{nullptr};
  module::ModuleGraph* dep_graph_{nullptr};
  std::string current_module_;
};
}  // namespace neamc
