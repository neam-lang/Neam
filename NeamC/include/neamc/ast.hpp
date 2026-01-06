//
// NeamC - Minimal AST definitions for compiler backend
//

#pragma once

#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace neamc
{
enum class BinaryOp
{
  Add,
  Subtract,
  Multiply,
  Divide
};

enum class UnaryOp
{
  Negate
};

struct Expression;
struct Statement;

using ExprPtr = std::unique_ptr<Expression>;
using StmtPtr = std::unique_ptr<Statement>;

struct LiteralExpr
{
  double value;
};

struct UnaryExpr
{
  UnaryOp op;
  ExprPtr operand;
};

struct BinaryExpr
{
  BinaryOp op;
  ExprPtr left;
  ExprPtr right;
};

struct Expression
{
  using Variant = std::variant<LiteralExpr, UnaryExpr, BinaryExpr>;
  Variant node;
};

struct ExpressionStmt
{
  ExprPtr expression;
};

struct BlockStmt
{
  std::vector<StmtPtr> statements;
};

struct AgentDecl
{
  std::string name;
};

struct Statement
{
  using Variant = std::variant<ExpressionStmt, BlockStmt, AgentDecl>;
  Variant node;
};

struct Program
{
  std::vector<StmtPtr> statements;
  std::string manifest;
};
}  // namespace neamc
