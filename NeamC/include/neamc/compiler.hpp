//
// NeamC - Compiler backend translating AST to bytecode
//

#pragma once

#include "neamc/ast.hpp"
#include "neamc/vm/bytecode.hpp"

namespace neamc
{
class Compiler
{
public:
  vm::Chunk compile(const Program& program);

private:
  void emit_statement(const Statement& stmt);
  void emit_expression(const Expression& expr);

  vm::Chunk chunk_{};
};
}  // namespace neamc
