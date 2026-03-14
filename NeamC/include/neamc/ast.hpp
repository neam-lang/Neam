//
// NeamC - AST definitions for compiler backend
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
  NotEqual,
  // v0.7.0
  In,
  NotIn
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

// v0.7.0: Index assignment (x[i] = val)
struct IndexAssignExpr
{
  ExprPtr base;
  ExprPtr index;
  ExprPtr value;
};

// v0.7.0: Tuple expression ((a, b, c))
struct TupleExpr
{
  std::vector<ExprPtr> elements;
};

// v0.7.0: F-string expression (f"Hello, {name}!")
struct FStringSegment
{
  bool is_expr{false};
  std::string text;
  ExprPtr expr;
};

struct FStringExpr
{
  std::vector<FStringSegment> segments;
};

// v0.7.0: Slice expression (x[1:3:1])
struct SliceExpr
{
  ExprPtr base;
  ExprPtr start;  // may be null
  ExprPtr end;    // may be null
  ExprPtr step;   // may be null
};

// v0.7.1: Copy-with expression (p with (x: 5))
struct CopyWithExpr
{
  ExprPtr object;
  std::vector<std::pair<std::string, ExprPtr>> overrides;
};

// v0.7.1: Named construction (Point(x: 3, y: 4))
struct NamedConstructExpr
{
  std::string type_name;
  std::vector<std::pair<std::string, ExprPtr>> fields;
};

// v0.7.1: Property assignment (p.x = 5)
struct SetPropertyExpr
{
  ExprPtr object;
  std::string name;
  ExprPtr value;
};

// v0.7.1 Phase 2: Match arm (must precede Expression)
struct MatchArm
{
  std::string pattern_name;  // variant name or "_" for wildcard
  std::vector<std::string> bindings;
  ExprPtr guard;  // optional guard expression
  ExprPtr body;
};

// v0.7.1 Phase 2: Match expression (must precede Expression)
struct MatchExpr
{
  ExprPtr subject;
  std::vector<MatchArm> arms;
};

// v0.8 Phase 8: Spawn expression
struct SpawnExpr
{
  std::string agent_name;
  std::vector<ExprPtr> arguments;
};

// v0.7.0: Destructuring pattern
struct DestructurePattern
{
  enum class Kind { List, Tuple, Map };
  Kind kind;
  std::vector<std::string> names;
  std::string rest_name;  // for ...rest
  bool has_rest{false};
  int rest_position{-1};
};

struct Expression
{
  using Variant =
      std::variant<LiteralExpr, IdentifierExpr, AssignmentExpr, UnaryExpr, BinaryExpr, CallExpr,
                   GetExpr, IndexExpr, ListExpr, MapExpr, FileOpenExpr, PathLiteralExpr, TryExpr,
                   PanicExpr, CatchPanicExpr, ContextExpr, WithContextExpr,
                   IndexAssignExpr, TupleExpr, FStringExpr, SliceExpr,
                   CopyWithExpr, NamedConstructExpr, SetPropertyExpr,
                   MatchExpr, SpawnExpr>;
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
  bool sensitive{false};  // v0.6.9 D10: requires human confirmation
};

// v0.6.7: External skill binding specification
struct SkillBindingSpec
{
  enum class Type
  {
    kMcp,
    kHttp,
    kClaudeBuiltin
  };

  Type type{Type::kHttp};

  // MCP binding fields
  std::string mcp_server;
  std::string mcp_tool;

  // HTTP binding fields
  std::string http_method;
  std::string http_url;
  std::string http_body_template;
  std::vector<std::pair<std::string, std::string>> http_headers;
  std::string http_response_path;
  long http_timeout_ms{30000};

  // Claude built-in fields
  std::string claude_tool_type;
};

struct ExternSkillDecl
{
  Visibility visibility;
  std::string name;
  std::string description;
  std::vector<SkillParam> params;
  SkillBindingSpec binding;
  bool sensitive{false};  // v0.6.9 D10: requires human confirmation
};

// v0.6.7: MCP server declaration
struct McpServerDecl
{
  std::string name;
  std::string command;
  std::string url;
  std::vector<std::string> args;
  std::unordered_map<std::string, std::string> env;
};

// v0.6.7: Adopt statement — bulk import tools from MCP server
struct AdoptStmt
{
  std::string server_name;
  std::vector<std::string> tool_names;       // Empty = wildcard (all tools)
  std::optional<std::string> alias_prefix;
};

struct KnowledgeSource
{
  std::string type;
  std::string path;
};

// Retrieval strategy types for advanced RAG
enum class RetrievalStrategy
{
  kBasic,      // Standard vector similarity search
  kMMR,        // Maximal Marginal Relevance (diversity-aware)
  kHybrid,     // Keyword + vector hybrid search
  kHyDE,       // Hypothetical Document Embeddings
  kSelfRAG,    // Self-reflective RAG with relevance checks
  kCRAG,       // Corrective RAG with query decomposition
  kAgentic,    // Agentic RAG with tool-based planning
  kGraphRAG    // Knowledge graph-based retrieval
};

struct RetrievalStrategyOptions
{
  // Common options
  std::size_t top_k{4};
  double relevance_threshold{0.5};

  // MMR options
  double mmr_lambda{0.5};  // Balance between relevance and diversity

  // HyDE options
  std::size_t num_hypothetical{1};

  // Self-RAG options
  bool enable_relevance_check{true};
  bool enable_support_check{true};

  // CRAG options
  bool enable_web_fallback{false};
  bool enable_query_decomposition{true};
  std::size_t max_corrections{2};

  // Agentic options
  std::size_t max_iterations{5};
  bool enable_reflection{true};

  // Graph RAG options
  std::size_t search_depth{2};
  bool include_communities{true};
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
  RetrievalStrategy retrieval_strategy{RetrievalStrategy::kBasic};
  RetrievalStrategyOptions strategy_options;
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

// v0.6.9: Security policy declaration
struct PolicyDecl
{
  Visibility visibility;
  std::string name;
  std::vector<std::string> allow_tools;
  std::vector<std::string> deny_tools;
  std::vector<std::string> confirm_tools;
  bool default_deny = true;
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

// v0.7.0: For-in loop
struct ForInStmt
{
  std::string variable;
  std::string second_variable;  // for (k, v) in map.entries()
  ExprPtr iterable;
  StmtPtr body;
};

// v0.7.1: Struct field definition
struct FieldDef
{
  std::string name;
  std::string type_name;  // optional type annotation
  ExprPtr default_value;  // optional default
  // v0.7.1 Phase 5: Property observers
  StmtPtr will_set_body;  // optional: willSet(newVal) { ... }
  std::string will_set_param;  // param name for willSet (default "newValue")
  StmtPtr did_set_body;   // optional: didSet { ... }
  ExprPtr guard_expr;     // optional: guard expression that must be true
};

// v0.7.1: Struct declaration
struct StructDecl
{
  Visibility visibility;
  std::string name;
  std::vector<std::string> type_params;  // Phase 5: generic type parameters <T, U>
  std::vector<FieldDef> fields;
  bool is_mutable{false};
};

// v0.7.1: Method definition inside impl block
struct MethodDef
{
  std::string name;
  std::vector<std::string> parameters;  // does NOT include "self" — compiler adds it
  StmtPtr body;  // always a BlockStmt
  bool is_static{false};
};

// v0.7.1: Impl block
struct ImplBlock
{
  std::string type_name;
  std::optional<std::string> trait_name;  // None for inherent impl, "Trait" for trait impl
  std::vector<MethodDef> methods;
};

// v0.7.1 Phase 2: Trait method signature (required — no body)
struct TraitMethodSig
{
  std::string name;
  std::vector<std::string> params;
};

// v0.7.1 Phase 2: Trait declaration
struct TraitDecl
{
  Visibility visibility;
  std::string name;
  std::vector<std::string> supertraits;
  std::vector<TraitMethodSig> required_methods;
  std::vector<MethodDef> default_methods;
};

// v0.7.1 Phase 2: Sealed variant definition
struct VariantDef
{
  std::string name;
  std::vector<FieldDef> fields;
};

// v0.7.1 Phase 2: Sealed type declaration
struct SealedDecl
{
  Visibility visibility;
  std::string name;
  std::vector<VariantDef> variants;
};


// v0.7.1 Phase 3: Extend block
struct ExtendBlock
{
  std::string target;
  std::vector<MethodDef> methods;
};

// v0.7.1 Phase 3: Derive annotation (attached to StructDecl)
struct DeriveAnnotation
{
  std::vector<std::string> traits;
};

// v0.7.1 Phase 4: Pipeline declaration
struct PipelineDecl
{
  std::string name;
  std::vector<std::string> step_agents;
};

// v0.7.1 Phase 4: Dispatch declaration
struct DispatchDecl
{
  std::string name;
  std::string router_agent;
  std::vector<std::pair<std::string, std::string>> routes;
  std::optional<std::string> fallback_agent;
};

// v0.7.1 Phase 4: Parallel declaration
struct ParallelDecl
{
  std::string name;
  std::vector<std::string> agents;
  std::string gather_agent;
};

// v0.7.1 Phase 4: Loop pattern declaration
struct LoopPatternDecl
{
  std::string name;
  std::string generator_agent;
  std::string critic_agent;
  int max_iterations{5};
  ExprPtr stop_condition;
};

// v0.7.0: Break statement
struct BreakStmt {};

// v0.7.0: Continue statement
struct ContinueStmt {};

// v0.7.0: Destructuring let
struct DestructureLetStmt
{
  DestructurePattern pattern;
  ExprPtr initializer;
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
  std::optional<IdentifierRef> policy;  // v0.6.9: security policy reference
  std::optional<IdentifierRef> budget;
  std::optional<IdentifierRef> env;
  std::optional<IdentifierRef> memory;
  std::optional<IdentifierRef> world_model;
  std::optional<IdentifierRef> plan;
  std::optional<IdentifierRef> connector;
};

// v0.8: Session configuration for claw agents
struct SessionConfig
{
  std::optional<int> idle_reset_minutes;
  std::optional<int> daily_reset_hour;
  std::optional<int> max_history_turns;
  std::optional<std::string> compaction;  // "auto", "manual", "disabled"
};

// v0.8: Loop configuration for forge agents
struct LoopConfig
{
  std::optional<int> max_iterations;
  std::optional<double> max_cost;
  std::optional<int> max_tokens;
  std::optional<std::string> prompt_file;
  std::optional<std::string> plan_file;
  std::optional<std::string> progress_file;
  std::optional<std::string> learnings_file;
};

// v0.8: Semantic memory configuration for claw agents
struct SemanticMemoryConfig
{
  std::optional<std::string> backend;  // "sqlite", "none"
  std::optional<std::string> search;   // "hybrid", "vector", "keyword", "none"
  bool flush_on_compact{false};
};

// v0.8: Lane configuration for claw agents
struct LaneConfig
{
  std::string name;
  int concurrency{1};
  std::optional<std::string> priority;
};

// v0.8: Claw agent declaration (conversational, multi-channel)
struct ClawAgentDecl
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
  std::vector<IdentifierRef> guardchains;
  std::optional<IdentifierRef> policy;
  std::optional<IdentifierRef> budget;
  std::optional<IdentifierRef> env;
  std::optional<std::string> workspace;
  // Claw-specific fields
  SessionConfig session;
  std::vector<IdentifierRef> channels;
  std::vector<LaneConfig> lanes;
  std::optional<SemanticMemoryConfig> semantic_memory;
};

// v0.8: Forge agent declaration (verification-driven loop)
struct ForgeAgentDecl
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
  std::vector<IdentifierRef> guardchains;
  std::optional<IdentifierRef> policy;
  std::optional<IdentifierRef> budget;
  std::optional<IdentifierRef> env;
  std::optional<std::string> workspace;
  // Forge-specific fields
  LoopConfig loop;
  ExprPtr verify;       // required: fun(ctx) -> VerifyResult
  std::optional<std::string> checkpoint;  // "git", "snapshot", "none"
};

// v0.8 Phase 6: Channel declaration
struct ChannelDecl
{
  std::string name;
  std::unordered_map<std::string, std::string> config;
  SourceSpan span;
};

// v0.9: Schema constraint (e.g. @primary_key, @range(0, 150), @enum(["a","b"]))
struct SchemaConstraint
{
  std::string type;  // "primary_key", "not_null", "unique", "pattern", "length", "range", "positive", "enum", "default", "foreign_key"
  std::vector<std::string> string_args;
  std::vector<double> number_args;
  std::optional<std::string> ref;  // for foreign_key(SchemaName)
};

// v0.9: Schema field declaration (e.g. id: string @primary_key)
struct SchemaFieldDecl
{
  std::string name;
  std::string type_name;  // "string", "int", "float", "bool", "datetime"
  std::vector<SchemaConstraint> constraints;
};

// v0.9: Schema declaration
struct SchemaDecl
{
  Visibility visibility;
  std::string name;
  std::optional<int> version;
  std::vector<SchemaFieldDecl> fields;
};

// v0.9: Source declaration
struct SourceDecl
{
  Visibility visibility;
  std::string name;
  std::string type;       // "postgres", "s3", "http", "kafka"
  std::string connection;
  std::optional<std::string> format;
  std::optional<std::string> refresh;
  std::optional<std::string> schema_ref;
  std::vector<std::string> partition_by;
  std::optional<std::string> classification;
  std::optional<std::string> mode;
};

// v0.9: Sink declaration
struct SinkDecl
{
  Visibility visibility;
  std::string name;
  std::string type;
  std::string connection;
  std::optional<std::string> format;
  std::optional<std::string> write_mode;
  std::optional<int> batch_size;
  std::optional<std::string> schema_ref;
  std::optional<std::string> compute_ref;
};

// v0.9: Quality declaration
struct QualityDecl
{
  Visibility visibility;
  std::string name;
  std::optional<std::string> freshness;
  std::optional<double> completeness;
  std::vector<std::string> uniqueness;
  std::optional<bool> drift_detection;
  std::optional<double> anomaly_threshold;
  std::optional<std::string> on_violation;
};

// v0.9: Compute declaration
struct ComputeDecl
{
  Visibility visibility;
  std::string name;
  std::string engine;  // "spark", "snowflake", "databricks", "bigquery", "local"
  ExprPtr config;      // optional: map expression
};

// v0.9: Governance declaration (complex nested config stored as expression)
struct GovernanceDecl
{
  Visibility visibility;
  std::string name;
  ExprPtr body;  // parsed as config map expression
};

// v0.9: Catalog declaration
struct CatalogDecl
{
  Visibility visibility;
  std::string name;
  std::string engine;  // "unity_catalog", "datahub"
  ExprPtr register_opts;  // optional: map expression
  std::optional<bool> discovery;
};

// v0.9: Pipeline transform operation (e.g. filter(column: "x", op: ">", value: 0))
struct PipelineTransformOp
{
  std::string name;
  std::vector<std::pair<std::string, ExprPtr>> args;  // named args
};

// v0.9: Pipeline block (extract -> transform -> load)
struct PipelineBlock
{
  std::vector<IdentifierRef> extract;
  std::vector<PipelineTransformOp> transforms;
  std::vector<IdentifierRef> load;
};

// v0.9: Compute routing block inside data agent
struct DataAgentComputeBlock
{
  std::optional<IdentifierRef> default_engine;
  std::vector<IdentifierRef> available;
  std::vector<std::pair<std::string, IdentifierRef>> routing;  // stage -> engine
};

// v0.9: Data agent declaration
struct DataAgentDecl
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
  std::vector<IdentifierRef> guardchains;
  std::optional<IdentifierRef> policy;
  std::optional<IdentifierRef> budget;
  std::optional<IdentifierRef> env;
  // Data agent specific
  std::vector<IdentifierRef> sources;
  std::vector<IdentifierRef> sinks;
  std::optional<IdentifierRef> schema_ref;
  std::optional<IdentifierRef> quality_ref;
  std::optional<DataAgentComputeBlock> compute;
  std::optional<IdentifierRef> governance_ref;
  std::optional<IdentifierRef> catalog_ref;
  std::optional<bool> lineage;
  std::optional<std::string> role;
  std::optional<std::string> purpose;
  std::optional<std::string> autonomy;
  std::optional<PipelineBlock> pipeline;
  std::optional<std::string> agent_md;  // v0.9: Agent.MD file path reference
};

// ============================================================
// v0.9.1: NeamETL AST nodes
// ============================================================

// SCD configuration per dimension
struct MartSCDConfig
{
  std::string dimension_name;
  std::string scd_type;                              // "type0"–"type6"
};

// Aggregate table definition within a mart
struct MartAggregateTableDef
{
  std::string name;
  std::string grain;
  std::vector<std::string> group_by;
  std::vector<std::string> measures;
};

// Mart declaration (nested within layers or standalone)
struct MartDecl
{
  Visibility visibility;
  std::string name;
  std::vector<std::string> facts;
  std::vector<std::string> dimensions;
  std::string grain;
  std::vector<std::string> measures;
  std::vector<MartSCDConfig> scd;
  std::vector<std::string> conformed;
  std::vector<MartAggregateTableDef> aggregate_tables;
  std::optional<std::string> materialization;
};

// Semantic metric declaration
struct SemanticMetricDecl
{
  std::string name;
  std::string sql;
  std::string description;
  std::string type;  // "measure"|"ratio"|"cumulative"|"derived"
  std::vector<std::string> time_grains;
  std::vector<std::string> dimensions;
  std::vector<std::string> filters;
  std::optional<std::string> owner;
};

// Semantic entity declaration
struct SemanticEntityDecl
{
  std::string name;
  std::string table;
  std::string key;
  std::optional<std::string> description;
};

// Semantic relationship declaration
struct SemanticRelationshipDecl
{
  std::string from_entity;
  std::string to_entity;
  std::string type;  // "one_to_many"|"many_to_many"
  std::string join_condition;
};

// Semantic time intelligence config
struct SemanticTimeIntelligence
{
  std::optional<std::string> fiscal_year_start;
  std::optional<std::string> week_start;
  std::optional<std::string> default_timezone;
};

// Semantic layer declaration (top-level)
struct SemanticDecl
{
  Visibility visibility;
  std::string name;
  std::vector<SemanticMetricDecl> metrics;
  std::vector<SemanticEntityDecl> entities;
  std::vector<SemanticRelationshipDecl> relationships;
  std::unordered_map<std::string, std::string> synonyms;
  std::optional<SemanticTimeIntelligence> time_intelligence;
};

// Incremental strategy block
struct IncrementalBlock
{
  std::string strategy;  // "timestamp"|"id"|"cdc"|"full_refresh"
  std::optional<std::string> key;
  std::optional<std::string> lookback;
  std::optional<std::string> unique_key;
  std::optional<std::string> on_schema_change;
  std::optional<std::string> full_refresh_schedule;
};

// Self-heal capabilities block
struct SelfHealCapabilities
{
  std::optional<int> auto_retry_max;
  std::optional<std::string> auto_retry_backoff;
  std::optional<double> auto_scale_factor;
  std::optional<std::string> schema_migration_approval;
  std::optional<std::string> data_patching_strategy;
  std::optional<IdentifierRef> fallback_source;
  std::optional<int> circuit_breaker_threshold;
};

// Self-heal learning config
struct SelfHealLearning
{
  bool record_incidents{false};
  std::optional<std::string> incident_store;
  bool improve_from_history{false};
};

// Self-heal notification config
struct SelfHealNotification
{
  std::optional<std::string> channel;
  ExprPtr webhook;  // nullptr if absent
  std::vector<std::string> escalation;
};

// Self-heal block
struct SelfHealBlock
{
  bool enabled{false};
  std::optional<SelfHealCapabilities> capabilities;
  std::optional<SelfHealLearning> learning;
  std::optional<SelfHealNotification> notification;
};

// Auto-model discover config
struct AutoModelDiscover
{
  bool facts{false};
  bool dimensions{false};
  bool relationships{false};
  bool scd_types{false};
  bool conformed_dimensions{false};
  bool degenerate_dimensions{false};
  bool junk_dimensions{false};
  bool bridge_tables{false};
  bool hubs{false};
  bool links{false};
  bool satellites{false};
  bool effectivity{false};
};

// Auto-model generate config
struct AutoModelGenerate
{
  std::optional<std::string> surrogate_keys;
  bool date_dimension{false};
  bool audit_columns{false};
  bool hash_diff{false};
  std::optional<std::string> hash_keys;
  bool load_date_column{false};
  bool record_source_column{false};
  bool hash_diff_for_satellites{false};
};

// Auto-model block
struct AutoModelBlock
{
  bool enabled{false};
  std::optional<std::string> methodology;
  std::optional<AutoModelDiscover> discover;
  std::optional<AutoModelGenerate> generate;
  std::optional<std::string> approval;
};

// Layer staging/integration config
struct LayerConfig
{
  std::optional<std::string> prefix;
  std::vector<std::string> operations;
  std::optional<std::string> materialization;
  std::optional<std::string> naming;
};

// Layers block
struct LayersBlock
{
  std::optional<LayerConfig> staging;
  std::optional<LayerConfig> integration;
  std::vector<MartDecl> marts;
};

// ETL Agent declaration (v0.9.1)
struct ETLAgentDecl
{
  Visibility visibility;
  std::string name;

  // Shared agent fields
  std::string provider;
  std::string model;
  std::optional<std::string> endpoint;
  std::optional<std::string> api_key_env;
  std::optional<double> temperature;
  std::optional<std::string> system;
  std::vector<IdentifierRef> skills;
  std::vector<IdentifierRef> connected_knowledge;
  std::vector<IdentifierRef> guardchains;
  std::optional<IdentifierRef> policy;
  std::optional<IdentifierRef> budget;
  std::optional<IdentifierRef> env;

  // Data agent fields (inherited)
  std::vector<IdentifierRef> sources;
  std::vector<IdentifierRef> sinks;
  std::vector<IdentifierRef> schemas;
  std::optional<PipelineBlock> pipeline;
  std::optional<IdentifierRef> quality;
  std::optional<DataAgentComputeBlock> compute;
  std::optional<IdentifierRef> governance;
  std::optional<std::string> role;
  std::optional<std::string> purpose;
  std::optional<std::string> jurisdiction;
  std::optional<IdentifierRef> catalog;
  std::optional<bool> lineage;
  std::optional<std::string> autonomy;
  std::vector<std::string> approval_required;
  std::vector<IdentifierRef> handoffs;

  // ETL agent-specific fields
  IdentifierRef warehouse;
  std::optional<std::string> model_type;
  std::optional<LayersBlock> layers;
  std::optional<IdentifierRef> semantic;
  std::optional<IncrementalBlock> incremental;
  std::optional<bool> self_heal_flag;
  std::optional<std::string> on_failure;
  std::optional<SelfHealBlock> self_heal_block;
  std::optional<AutoModelBlock> auto_model;
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
                   PolicyDecl,
                   ToolDecl, MemoryDecl, EnvDecl, ConnectorDecl, WorldModelDecl, PlanDecl,
                   SubagentDecl, AgentDecl, ModuleDecl, ImportDecl, ConstDecl, TypeAlias,
                   DocComment, GrantStmt, CheckpointStmt, RewindStmt,
                   ExternSkillDecl, McpServerDecl, AdoptStmt,
                   ForInStmt, BreakStmt, ContinueStmt, DestructureLetStmt,
                   StructDecl, ImplBlock,
                   TraitDecl, SealedDecl, ExtendBlock,
                   PipelineDecl, DispatchDecl, ParallelDecl, LoopPatternDecl,
                   ClawAgentDecl, ForgeAgentDecl, ChannelDecl,
                   SchemaDecl, SourceDecl, SinkDecl, QualityDecl,
                   ComputeDecl, GovernanceDecl, CatalogDecl, DataAgentDecl,
                   ETLAgentDecl, MartDecl, SemanticDecl>;
  SourceSpan span;
  Variant node;
};

struct Program
{
  std::vector<StmtPtr> statements;
  std::string manifest;
};
}  // namespace neamc
