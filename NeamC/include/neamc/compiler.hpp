//
// NeamC - Compiler backend translating AST to bytecode
//

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
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

  // v0.8 Phase 1: Agent type tracking for trait validation
  enum class AgentKind { Stateless, Claw, Forge, Data, ETL };
  std::unordered_map<std::string, AgentKind> agent_types_;

  // v0.9: Data agent definition tracking
  std::unordered_map<std::string, bool> schema_defs_;
  std::unordered_map<std::string, bool> source_defs_;
  std::unordered_map<std::string, bool> sink_defs_;
  std::unordered_map<std::string, bool> compute_defs_;
  std::unordered_map<std::string, bool> governance_defs_;
  std::unordered_map<std::string, bool> catalog_defs_;
  std::unordered_map<std::string, bool> quality_defs_;

  // v0.9.1: ETLAgent tracking
  std::unordered_set<std::string> semantic_defs_;
  std::unordered_set<std::string> mart_defs_;

  struct ETLAgentDef {
    int source_count;
    bool has_warehouse;
    int mart_count;
  };
  std::unordered_map<std::string, ETLAgentDef> etl_agent_defs_;

  bool is_neamclaw_trait(const std::string& name) const;
  void validate_neamclaw_trait_compat(const std::string& trait, const std::string& type);
  void compiler_warning(const std::string& message) const;

  vm::Chunk chunk_{};
  std::vector<Local> locals_{};
  int scope_depth_ = 0;
  // v0.7.0: Loop tracking for break/continue
  std::vector<std::size_t> loop_starts_;
  std::vector<std::vector<std::size_t>> break_patches_;
  std::vector<std::size_t> loop_local_counts_;  // locals count before loop scope (for cleanup on break)
};
}  // namespace neamc
