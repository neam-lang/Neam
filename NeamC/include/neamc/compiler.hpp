//
// NeamC - Compiler backend translating AST to bytecode
//

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "neamc/ast.hpp"
#include "neamc/vm/bytecode.hpp"

namespace neamc
{
class Compiler
{
public:
  vm::Chunk compile(const Program& program);

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

  vm::Chunk chunk_{};
  std::vector<Local> locals_{};
  int scope_depth_ = 0;
  // v0.7.0: Loop tracking for break/continue
  std::vector<std::size_t> loop_starts_;
  std::vector<std::vector<std::size_t>> break_patches_;
  std::vector<std::size_t> loop_local_counts_;  // locals count before loop scope (for cleanup on break)
};
}  // namespace neamc
