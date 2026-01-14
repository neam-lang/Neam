//
// NeamC - AST definitions for compiler backend
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/vm/value.hpp"

namespace neamc
{
struct SourceSpan
{
  std::size_t position = 0;
  std::size_t length = 0;
  std::size_t line = 0;
  std::size_t column = 0;
};

struct IdentifierRef
{
  std::string name;
  SourceSpan span;
};

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

struct GetExpr
{
  ExprPtr object;
  std::string name;
};

struct IndexExpr
{
  ExprPtr base;
  ExprPtr index;
};

struct ListExpr
{
  std::vector<ExprPtr> elements;
};

struct MapEntry
{
  ExprPtr key;
  ExprPtr value;
};

struct MapExpr
{
  std::vector<MapEntry> entries;
};

struct TypeExpression
{
  std::string name;
};

struct FileOpenExpr
{
  ExprPtr path;
  std::string mode;
};

struct PathLiteralExpr
{
  std::string path;
};

struct Expression
{
  using Variant =
      std::variant<LiteralExpr, IdentifierExpr, AssignmentExpr, UnaryExpr, BinaryExpr, CallExpr,
                   GetExpr, IndexExpr, ListExpr, MapExpr, FileOpenExpr, PathLiteralExpr>;
  SourceSpan span;
  Variant node;
};

struct ExpressionStmt
{
  ExprPtr expression;
};

struct EmitStmt
{
  ExprPtr value;
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

struct AssertStmt
{
  enum class Kind
  {
    kEq,
    kNe,
    kTrue,
    kFalse,
    kSome,
    kNone,
    kOk,
    kErr,
    kThrows
  };
  Kind kind;
  ExprPtr left;
  ExprPtr right;
  std::unique_ptr<TypeExpression> exception_type;
};

struct TestAttribute
{
  enum class Kind
  {
    kIgnore,
    kShouldPanic,
    kTimeout,
    kAsync
  };
  Kind kind;
  std::optional<std::string> panic_message;
  std::optional<int64_t> timeout_ms;
};

struct TestDecl
{
  std::string name;
  std::vector<TestAttribute> attributes;
  std::unique_ptr<BlockStmt> body;
};

struct TestSuiteDecl
{
  std::string name;
  std::vector<std::unique_ptr<TestDecl>> tests;
  std::vector<std::unique_ptr<TestSuiteDecl>> nested_suites;
  std::unique_ptr<BlockStmt> before_all;
  std::unique_ptr<BlockStmt> before_each;
  std::unique_ptr<BlockStmt> after_each;
  std::unique_ptr<BlockStmt> after_all;
};

struct WithStmt
{
  ExprPtr resource;
  std::string binding_name;
  std::unique_ptr<BlockStmt> body;
};

struct FunctionDecl
{
  std::string name;
  std::vector<std::string> parameters;
  StmtPtr body;  // always a BlockStmt
};

struct SkillParam
{
  std::string name;
  nlohmann::json schema;
};

struct SkillDecl
{
  std::string name;
  std::string description;
  std::vector<SkillParam> params;
  FunctionDecl impl;
};

struct KnowledgeSource
{
  std::string type;
  std::string path;
};

struct KnowledgeDecl
{
  std::string name;
  std::string vector_store;
  std::string embedding_model;
  std::size_t chunk_size{0};
  std::size_t chunk_overlap{0};
  std::vector<KnowledgeSource> sources;
};

struct AgentDecl
{
  std::string name;
  std::string provider;
  std::string model;
  std::optional<std::string> endpoint;
  std::optional<std::string> api_key_env;
  std::optional<double> temperature;
  std::optional<std::string> system;
  std::vector<IdentifierRef> skills;
  std::vector<IdentifierRef> connected_knowledge;
};

struct Statement
{
  using Variant =
      std::variant<ExpressionStmt, EmitStmt, BlockStmt, LetStmt, IfStmt, WhileStmt, ReturnStmt,
                   AssertStmt, WithStmt, TestDecl, TestSuiteDecl, FunctionDecl, SkillDecl,
                   KnowledgeDecl, AgentDecl>;
  SourceSpan span;
  Variant node;
};

struct Program
{
  std::vector<StmtPtr> statements;
  std::string manifest;
};
}  // namespace neamc
