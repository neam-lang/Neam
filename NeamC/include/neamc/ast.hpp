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
inline constexpr char kAstVersion[] = "2.0.0";

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

struct Visibility
{
  enum class Level
  {
    kPrivate,
    kPublic,
    kCrate,
    kSuper
  };

  Level level{Level::kPrivate};
  SourceSpan span{};
};

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

struct TryExpr
{
  ExprPtr expr;
};

struct PanicExpr
{
  ExprPtr message;
};

struct CatchPanicExpr
{
  ExprPtr closure;
};

struct ContextExpr
{
  ExprPtr expr;
  ExprPtr message;
};

struct WithContextExpr
{
  ExprPtr expr;
  std::string key;
  ExprPtr value;
};

struct Expression
{
  using Variant =
      std::variant<LiteralExpr, IdentifierExpr, AssignmentExpr, UnaryExpr, BinaryExpr, CallExpr,
                   GetExpr, IndexExpr, ListExpr, MapExpr, FileOpenExpr, PathLiteralExpr, TryExpr,
                   PanicExpr, CatchPanicExpr, ContextExpr, WithContextExpr>;
  SourceSpan span;
  Variant node;
};

struct ModuleDecl
{
  std::vector<std::string> path;
};

struct ImportDecl
{
  Visibility visibility;
  std::vector<std::string> path;
  std::optional<std::string> alias;
  std::vector<std::string> items;
  bool is_wildcard{false};
  bool is_reexport{false};
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
  Visibility visibility;
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
  Visibility visibility;
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
  Visibility visibility;
  std::string name;
  std::string vector_store;
  std::string embedding_model;
  std::size_t chunk_size{0};
  std::size_t chunk_overlap{0};
  std::vector<KnowledgeSource> sources;
};

struct BudgetDimension
{
  std::string name;
  double value{0.0};
  std::string unit;
};

struct BudgetDecl
{
  Visibility visibility;
  std::string name;
  std::vector<BudgetDimension> dimensions;
};

struct GuardHandler
{
  enum class Type
  {
    kOnObservation,
    kOnAction,
    kOnToolInput,
    kOnToolOutput,
    kOnToolCall,
    kOnResult
  };

  Type type{Type::kOnObservation};
  std::vector<std::string> parameters;
  std::unique_ptr<TypeExpression> return_type;
  std::unique_ptr<BlockStmt> body;
};

struct GuardDecl
{
  Visibility visibility;
  std::string name;
  std::string description;
  std::vector<std::unique_ptr<GuardHandler>> handlers;
};

struct GuardChainDecl
{
  Visibility visibility;
  std::string name;
  std::vector<IdentifierRef> guards;
};

struct CapabilityDecl
{
  Visibility visibility;
  std::string name;
  std::string pattern;
};

struct ToolParam
{
  std::string name;
  std::unique_ptr<TypeExpression> type_expr;
  ExprPtr default_value;
  bool has_default{false};
};

struct BudgetCost
{
  std::string resource;
  double amount{0.0};
};

struct ToolImpl
{
  std::vector<std::string> parameters;
  std::unique_ptr<BlockStmt> body;
};

struct ToolDecl
{
  Visibility visibility;
  std::string name;
  std::string description;
  std::vector<IdentifierRef> capabilities;
  std::vector<ToolParam> params;
  std::unique_ptr<TypeExpression> returns_type;
  std::vector<BudgetCost> budget_costs;
  std::vector<IdentifierRef> guards;
  std::unique_ptr<ToolImpl> impl;
};

struct MemoryConfig
{
  std::string key;
  std::string value;
};

struct MemoryDecl
{
  Visibility visibility;
  std::string name;
  std::string backend;
  std::string retention;
  int max_events{10000};
  int snapshot_interval{100};
};

struct CheckpointStmt
{
  std::string label;
};

struct RewindStmt
{
  ExprPtr target;
};

struct EnvConfig
{
  std::string key;
  std::string value;
  bool is_env_var{false};
  std::string env_var_name;
};

struct EnvDecl
{
  Visibility visibility;
  std::string name;
  std::vector<EnvConfig> configs;
};

struct ConnectorDecl
{
  Visibility visibility;
  std::string name;
  std::string protocol;
  std::string endpoint;
  std::string contract;
  std::string auth;
};

struct WorldModelDecl
{
  Visibility visibility;
  std::string name;
  int tier{0};
  std::string state_schema;
  int update_frequency{1};
};

struct PlanDecl
{
  Visibility visibility;
  std::string name;
  std::string pattern;
  int max_depth{5};
  bool backtrack{true};
  std::string pruning;
};

struct SubagentDecl
{
  Visibility visibility;
  std::string name;
  std::string base_agent;
  double budget_share{0.5};
  bool capability_inherit{true};
  bool isolation{false};
};

struct GrantStmt
{
  IdentifierRef capability;
  IdentifierRef target;
};

struct AgentDecl
{
  Visibility visibility;
  std::string name;
  std::string provider;
  std::string model;
  std::optional<std::string> endpoint;
  std::optional<std::string> api_key_env;
  std::optional<double> temperature;
  std::optional<std::string> system;
  std::vector<IdentifierRef> skills;
  std::vector<IdentifierRef> connected_knowledge;
  std::vector<IdentifierRef> required_capabilities;
  std::vector<IdentifierRef> guardchains;
  std::optional<IdentifierRef> budget;
  std::optional<IdentifierRef> env;
  std::optional<IdentifierRef> memory;
  std::optional<IdentifierRef> world_model;
  std::optional<IdentifierRef> plan;
  std::optional<IdentifierRef> connector;
};

struct ConstDecl
{
  Visibility visibility;
  std::string name;
  std::unique_ptr<TypeExpression> type;
  ExprPtr value;
};

struct TypeAlias
{
  Visibility visibility;
  std::string name;
  std::vector<std::string> type_params;
  std::unique_ptr<TypeExpression> type;
};

struct DocComment
{
  std::string content;
};

struct Statement
{
  using Variant =
      std::variant<ExpressionStmt, EmitStmt, BlockStmt, LetStmt, IfStmt, WhileStmt, ReturnStmt,
                   AssertStmt, WithStmt, TestDecl, TestSuiteDecl, FunctionDecl, SkillDecl,
                   KnowledgeDecl, BudgetDecl, GuardDecl, GuardChainDecl, CapabilityDecl,
                   ToolDecl, MemoryDecl, EnvDecl, ConnectorDecl, WorldModelDecl, PlanDecl,
                   SubagentDecl, AgentDecl, ModuleDecl, ImportDecl, ConstDecl, TypeAlias,
                   DocComment, GrantStmt, CheckpointStmt, RewindStmt>;
  SourceSpan span;
  Variant node;
};

struct Program
{
  std::vector<StmtPtr> statements;
  std::string manifest;
};
}  // namespace neamc
