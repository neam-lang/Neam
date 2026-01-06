//
// NeamC - AST definitions for compiler backend
//

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "neamc/vm/value.hpp"

namespace neamc
{
enum class BinaryOp
{
  Add,
  Subtract,
  Multiply,
  Divide,
  Greater,
  GreaterEqual,
  Less,
  LessEqual,
  Equal,
  NotEqual
};

enum class UnaryOp
{
  Negate,
  Not
};

struct Expression;
struct Statement;

using ExprPtr = std::unique_ptr<Expression>;
using StmtPtr = std::unique_ptr<Statement>;

struct LiteralExpr
{
  vm::Value value;
};

struct IdentifierExpr
{
  std::string name;
};

struct AssignmentExpr
{
  std::string name;
  ExprPtr value;
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

struct CallExpr
{
  ExprPtr callee;
  std::vector<ExprPtr> arguments;
};

struct Expression
{
  using Variant =
      std::variant<LiteralExpr, IdentifierExpr, AssignmentExpr, UnaryExpr, BinaryExpr, CallExpr>;
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

struct LetStmt
{
  std::string name;
  ExprPtr initializer;
};

struct IfStmt
{
  ExprPtr condition;
  StmtPtr then_branch;
  StmtPtr else_branch;  // may be null
};

struct WhileStmt
{
  ExprPtr condition;
  StmtPtr body;
};

struct ReturnStmt
{
  ExprPtr value;
};

struct FunctionDecl
{
  std::string name;
  std::vector<std::string> parameters;
  StmtPtr body;  // always a BlockStmt
};

struct Statement
{
  using Variant =
      std::variant<ExpressionStmt, BlockStmt, LetStmt, IfStmt, WhileStmt, ReturnStmt, FunctionDecl>;
  Variant node;
};

struct Program
{
  std::vector<StmtPtr> statements;
  std::string manifest;
};
}  // namespace neamc
