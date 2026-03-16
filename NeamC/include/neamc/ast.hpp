//
// NeamC - AST definitions for compiler backend
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
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

// ============================================================
// v0.9.2: Migration Agent AST nodes
// ============================================================

// Migration strategy enum
enum class MigrationStrategy
{
  LIFT_AND_SHIFT,
  RE_PLATFORM,
  RE_ARCHITECTURE
};

// Data movement strategy enum
enum class DataMovementStrategy
{
  FULL_DUMP,
  INCREMENTAL,
  PARALLEL_RUN,
  TRICKLE,
  BLUE_GREEN
};

// Cutover strategy enum
enum class CutoverStrategy
{
  BIG_BANG,
  BLUE_GREEN,
  CANARY,
  TRICKLE
};

// CDC configuration
struct CDCConfig
{
  std::string mechanism;
  std::string tool;
  std::string lag_threshold;
  std::string lag_critical;
};

// Wave configuration
struct WaveConfig
{
  std::string mode{"auto"};
  int max_tables_per_wave{50};
  int max_parallel_extractions{4};
  std::vector<std::pair<std::string, std::vector<std::string>>> manual_waves;
  std::unordered_map<std::string, std::vector<std::string>> dependencies;
};

// Movement configuration
struct MovementConfig
{
  DataMovementStrategy strategy{DataMovementStrategy::FULL_DUMP};
  CDCConfig cdc;
  int extraction_threads{4};
  int load_threads{4};
  std::string partition_strategy{"range"};
  std::string staging_format{"parquet"};
  std::string checkpoint_interval{"5GB"};
};

// Schema translation configuration
struct SchemaTranslationConfig
{
  std::string type_mapping{"auto"};
  std::string stored_procedures{"skip"};
  std::string views{"skip"};
  std::string materialized_views{"skip"};
  std::string indexes{"re_evaluate"};
  std::string sequences{"preserve_values"};

  struct PlatformSpecific
  {
    std::string empty_string_handling{"preserve_as_null"};
    bool date_to_timestamp{false};
    std::string clob_threshold{"16MB"};
    std::string number_no_precision{"analyze_and_map"};
  };
  std::optional<PlatformSpecific> oracle_specific;
  std::optional<PlatformSpecific> teradata_specific;
};

// Reconciliation configuration
struct ReconciliationConfig
{
  bool row_counts{true};
  bool column_aggregates{false};
  std::string hash_comparison{"none"};
  std::string statistical_distribution{"none"};
  bool boundary_values{false};
  bool golden_queries{false};
  bool referential_integrity{false};
};

// Tolerance configuration
struct ToleranceConfig
{
  std::string financial_columns{"exact"};
  double floating_point{1e-6};
  std::string timestamp_precision{"1s"};
};

// Validation configuration
struct ValidationConfig
{
  std::string mode{"standard"};
  ReconciliationConfig reconciliation;
  ToleranceConfig tolerances;
  std::vector<std::string> golden_queries;
  bool continuous_enabled{false};
  std::string continuous_interval;
};

// Rollback configuration
struct RollbackConfig
{
  std::string window{"72h"};
  bool auto_trigger{false};
  std::vector<std::string> trigger_conditions;
};

// Cutover configuration
struct CutoverConfig
{
  CutoverStrategy strategy{CutoverStrategy::BIG_BANG};
  RollbackConfig rollback;
};

// Self-heal guardrails
struct SelfHealGuardrails
{
  int max_auto_fix_rows{1000};
  double max_auto_fix_percentage{0.001};
  int max_retries_per_table{3};
  bool require_dry_run{true};
  bool audit_all_remediations{true};
};

// Self-heal configuration (migration-specific)
struct SelfHealMigrationConfig
{
  bool enabled{false};
  bool missing_rows{false};
  bool duplicate_rows{false};
  bool type_conversion_errors{false};
  bool network_failures{false};
  bool checkpoint_resume{false};
  SelfHealGuardrails guardrails;
};

// Assessment configuration
struct AssessmentConfig
{
  bool auto_discover{true};
  bool profile_data{true};
  bool risk_analysis{true};
  std::string report_format{"json"};
};

// Governance configuration (migration-specific)
struct GovernanceMigrationConfig
{
  bool preserve_classification{false};
  bool pii_detection{false};
  std::string staging_region;
  std::string target_region;
  bool log_all_sql{false};
  bool log_all_data_movement{false};
  std::string audit_retention{"7y"};
};

// Migration agent declaration
struct MigrationAgentDecl
{
  Visibility visibility;
  std::string name;

  // Migration-specific fields
  std::string source;
  std::string target;
  std::optional<std::string> staging;
  MigrationStrategy strategy{MigrationStrategy::RE_PLATFORM};
  WaveConfig waves;
  MovementConfig movement;
  SchemaTranslationConfig schema_translation;
  ValidationConfig validation;
  CutoverConfig cutover;
  SelfHealMigrationConfig self_heal;
  std::optional<AssessmentConfig> assessment;
  std::optional<GovernanceMigrationConfig> governance;

  // Common agent fields
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::optional<std::string> system_prompt;
  std::optional<double> temperature;
  std::optional<std::string> budget;
  std::vector<std::string> skills;

  // Inherited DataAgent/ETLAgent fields
  std::optional<std::string> role;
  std::optional<std::string> purpose;
  std::optional<std::string> autonomy;
  std::optional<PipelineBlock> pipeline;
  std::optional<std::string> agent_md;
};

// ============================================================================
// v0.9.3: DataOps Agent — Platform operations & continuous monitoring
// ============================================================================

// Scheduler type
enum class SchedulerType
{
  AIRFLOW, CONTROLM, CRON, DATABRICKS, SNOWFLAKE_TASKS,
  DBT, GLUE, ADF, INFORMATICA, LUIGI
};

// Log source type
enum class LogSourceType
{
  SNOWFLAKE, ORACLE, POSTGRES, MYSQL, SQLSERVER,
  SPARK, REDSHIFT, BIGQUERY, KAFKA
};

// Platform type
enum class PlatformType
{
  SNOWFLAKE, S3, ADLS, GCS, HDFS,
  REDSHIFT, BIGQUERY, DATABRICKS
};

// DataOps monitoring mode
enum class DataOpsMode
{
  CONTINUOUS,
  SCHEDULED,
  ON_DEMAND
};

// Scheduler connection declaration
struct SchedulerDecl
{
  Visibility visibility;
  std::string name;
  SchedulerType sched_type{SchedulerType::AIRFLOW};
  std::string connection;
  std::string credentials;
  std::string poll_interval{"60s"};
  std::vector<std::string> filters;       // dag_filter, folder_filter, etc.
  std::optional<std::string> timezone;
  std::optional<std::string> datacenter;
  std::optional<std::string> host;
};

// Audit table status value mapping
struct AuditStatusValues
{
  std::vector<std::string> success;
  std::vector<std::string> failure;
  std::vector<std::string> running;
  std::vector<std::string> skipped;
};

// Audit table column mapping
struct AuditColumnMap
{
  std::string job_id;
  std::string timestamp;
  std::string status;
  AuditStatusValues status_values;
  std::optional<std::string> end_time;
  std::optional<std::string> target_table;
  std::optional<std::string> load_type;
  std::optional<std::string> rows_in;
  std::optional<std::string> rows_out;
  std::optional<std::string> rows_inserted;
  std::optional<std::string> rows_updated;
  std::optional<std::string> rows_deleted;
  std::optional<std::string> rows_rejected;
  std::optional<std::string> error;
  std::optional<std::string> duration;
  std::optional<std::string> duration_unit;
  std::optional<std::string> severity;
  std::optional<std::string> rule_name;
};

// Audit table anomaly thresholds
struct AuditAnomalyConfig
{
  double row_count_drop{0.20};
  double row_count_spike{3.0};
  double duration_spike{2.0};
  double failure_rate{0.05};
  int zero_rows_consecutive{3};
};

// Audit table declaration
struct AuditTableDecl
{
  Visibility visibility;
  std::string name;
  std::string source_ref;
  std::string table_name;
  AuditColumnMap column_map;
  std::string poll_interval{"60s"};
  std::optional<std::string> lookback_window;
  std::optional<std::string> retention_analysis;
  AuditAnomalyConfig anomalies;
};

// Log source alert configuration
struct LogAlertConfig
{
  bool query_timeout{false};
  double warehouse_credit_spike{0.0};
  int failed_logins{0};
  std::optional<std::string> long_running_queries;
  bool full_table_scans{false};
  int queued_queries{0};
  std::vector<std::string> ora_errors;
  double tablespace_usage{0.0};
  int redo_log_switches{0};
  double dead_tuples_ratio{0.0};
  std::optional<std::string> lock_waits;
  double connection_usage{0.0};
  std::optional<std::string> replication_lag;
  std::optional<std::string> slow_queries;
  bool oom_errors{false};
  std::optional<std::string> shuffle_spill;
  int stage_failures{0};
  bool executor_lost{false};
  double skewed_partitions{0.0};
};

// Log source declaration
struct LogSourceDecl
{
  Visibility visibility;
  std::string name;
  LogSourceType log_type{LogSourceType::SNOWFLAKE};
  std::string connection;
  std::string credentials;
  std::vector<std::string> views;
  std::optional<std::string> log_file;
  std::optional<std::string> log_format;
  std::string poll_interval{"60s"};
  std::optional<std::string> lookback_window;
  LogAlertConfig alerts;
};

// Platform health check configuration
struct PlatformHealthConfig
{
  bool storage_growth{false};
  bool partition_health{false};
  bool file_format_consistency{false};
  int small_files_threshold{0};
  std::optional<std::string> small_files_min_size;
  std::optional<std::string> stale_data;
  bool warehouse_utilization{false};
  std::optional<std::string> query_perf_p50;
  std::optional<std::string> query_perf_p95;
  std::optional<std::string> query_perf_p99;
  bool clustering_health{false};
  std::optional<std::string> mv_staleness;
  bool time_travel_usage{false};
  bool row_count_baseline{false};
  bool schema_drift{false};
  bool consumer_query_patterns{false};
  std::map<std::string, std::string> freshness;
};

// Platform FinOps configuration
struct PlatformFinOpsConfig
{
  double daily_budget{0.0};
  std::map<std::string, double> warehouse_budgets;
  std::optional<std::string> auto_suspend_idle;
  std::optional<std::string> auto_kill_queries;
  double cost_anomaly_threshold{0.0};
};

// Platform declaration
struct PlatformDecl
{
  Visibility visibility;
  std::string name;
  PlatformType plat_type{PlatformType::SNOWFLAKE};
  std::string connection;
  std::string credentials;
  std::optional<std::string> database;
  PlatformHealthConfig health_checks;
  PlatformFinOpsConfig finops;
};

// Severity level for incident policy
struct SeverityLevel
{
  std::string level_name;              // "P1_critical", "P2_high", etc.
  std::vector<std::string> conditions;
  std::string response;
  std::optional<std::string> escalation;
  std::vector<std::string> channels;
};

// Auto-heal guardrails
struct AutoHealGuardrails
{
  double max_cost_per_action{0.0};
  int max_retries_per_hour{0};
  std::vector<std::string> no_actions_during;
  bool require_dry_run{false};
};

// Auto-heal configuration
struct AutoHealConfig
{
  bool enabled{false};
  int max_auto_retries{3};
  std::string retry_backoff{"exponential"};
  std::string retry_initial_wait{"30s"};
  std::vector<std::string> allowed_actions;
  std::vector<std::string> requires_approval;
  AutoHealGuardrails guardrails;
};

// Incident policy declaration
struct IncidentPolicyDecl
{
  Visibility visibility;
  std::string name;
  std::vector<SeverityLevel> severity_levels;
  AutoHealConfig auto_heal;
};

// Correlation scope
struct CorrelationScope
{
  std::vector<std::string> schedulers;
  std::vector<std::string> audit_tables;
  std::vector<std::string> log_sources;
  std::vector<std::string> job_pattern;
  std::vector<std::string> table_pattern;
};

// Correlation SLA
struct CorrelationSLAConfig
{
  std::string deadline;
  std::optional<std::string> timezone;
  bool business_days_only{false};
  std::optional<std::string> escalation;
};

// Correlation declaration
struct CorrelationDecl
{
  Visibility visibility;
  std::string name;
  CorrelationScope scope;
  std::string time_window{"30m"};
  std::map<std::string, std::vector<std::string>> dependencies;
  CorrelationSLAConfig sla;
};

// DataOps report configuration
struct DataOpsReportConfig
{
  std::optional<std::string> time;
  std::optional<std::string> day;
  std::optional<std::string> frequency;
  std::optional<std::string> channel;
};

// DataOps agent declaration
struct DataOpsAgentDecl
{
  Visibility visibility;
  std::string name;

  // Common agent fields
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::optional<std::string> endpoint;
  std::optional<std::string> api_key_env;
  std::optional<double> temperature;
  std::optional<std::string> system_prompt;
  std::optional<std::string> budget;

  // DataOps-specific references
  std::vector<std::string> platforms;
  std::vector<std::string> schedulers;
  std::vector<std::string> audit_tables;
  std::vector<std::string> log_sources;
  std::vector<std::string> correlations;

  // Policy and behavior
  std::optional<std::string> incident_policy;
  DataOpsMode mode{DataOpsMode::CONTINUOUS};

  // Reporting
  DataOpsReportConfig daily_digest;
  DataOpsReportConfig weekly_summary;
  DataOpsReportConfig cost_report;

  // Optional agent knowledge
  std::optional<std::string> agent_md;

  // Skills and guards (inherited pattern)
  std::vector<std::string> skills;
  std::vector<std::string> guardchains;
  std::optional<std::string> policy;
};

// ═══════════════════════════════════════════════════════════════
// v0.9.4 Governance Agent AST Nodes
// ═══════════════════════════════════════════════════════════════

// --- Governance Enums ---

enum class CatalogSourceType
{
  SNOWFLAKE, ORACLE, POSTGRES, MYSQL, SQLSERVER,
  REDSHIFT, BIGQUERY, DATABRICKS,
  S3, ADLS, GCS, HDFS,
  COLLIBRA, ATLAS, ALATION, PURVIEW, INFORMATICA, ATLAN
};

enum class SyncMode
{
  PUSH, PULL, BIDIRECTIONAL
};

enum class ConflictResolution
{
  AGENT_WINS, EXTERNAL_WINS, MANUAL
};

enum class SensitivityLevel
{
  PUBLIC,        // L1
  INTERNAL,      // L2
  CONFIDENTIAL,  // L3
  RESTRICTED     // L4
};

enum class AccessModel
{
  RBAC, ABAC, HYBRID_RBAC_ABAC
};

enum class LineageDepth
{
  TABLE, COLUMN, TRANSFORMATION
};

// --- CatalogSource Declaration ---

struct GovCatalogSourceDecl
{
  Visibility visibility;
  std::string name;
  CatalogSourceType source_type{CatalogSourceType::SNOWFLAKE};
  std::string connection;
  std::string credentials;
  std::vector<std::string> databases;
  std::vector<std::string> prefixes;
  std::string scan_interval;
  bool include_views{true};
  bool include_stages{false};
  bool detect_formats{false};
  std::vector<std::string> exclude_patterns;
  SyncMode sync_mode{SyncMode::PULL};
  std::string sync_interval;
  ConflictResolution conflict_resolution{ConflictResolution::EXTERNAL_WINS};
};

// --- Catalog Declaration (v0.9.4 enhanced) ---

struct AutoDocumentConfig
{
  bool enabled{false};
  std::string provider;
  std::string model;
  bool require_review{true};
  std::string review_channel;
};

struct OwnershipConfig
{
  bool auto_assign{false};
  std::string default_domain;
  bool require_owner{false};
};

struct GovCatalogDecl
{
  Visibility visibility;
  std::string name;
  std::vector<std::string> sources;
  AutoDocumentConfig auto_document;
  std::string staleness_threshold;
  bool shadow_dataset_detection{false};
  OwnershipConfig ownership;
};

// --- Glossary Declaration ---

struct SynonymDetectionConfig
{
  bool enabled{false};
  double confidence_threshold{0.85};
  bool cross_database{false};
};

struct AutoSuggestConfig
{
  bool enabled{false};
  std::string provider;
  std::string model;
  std::vector<std::string> sources;
  bool require_approval{true};
  std::string approval_channel;
};

struct GlossaryDecl
{
  Visibility visibility;
  std::string name;
  std::vector<std::string> domains;
  AutoSuggestConfig auto_suggest;
  SynonymDetectionConfig synonym_detection;
  std::string terms_json;
  std::string external_sync_json;
};

// --- Classification Policy Declaration ---

struct ClassificationLevel
{
  int level{0};
  std::vector<std::string> controls;
  std::string retention_max;
  std::string cross_border;
};

struct DriftDetectionConfig
{
  bool enabled{false};
  std::string scan_interval;
  bool alert_on_new_pii{true};
  bool alert_on_reclassification{true};
};

struct SemanticClassifyConfig
{
  bool column_name_analysis{true};
  bool sample_value_analysis{true};
  bool cross_column_inference{true};
  double confidence_threshold{0.80};
};

struct AutoClassifyConfig
{
  bool enabled{false};
  std::string provider;
  std::string model;
  std::string patterns_json;
  SemanticClassifyConfig semantic;
  DriftDetectionConfig drift_detection;
};

struct TagPropagationConfig
{
  bool lineage_based{false};
  std::string inheritance;
};

struct ClassificationPolicyDecl
{
  Visibility visibility;
  std::string name;
  std::map<std::string, ClassificationLevel> levels;
  AutoClassifyConfig auto_classify;
  TagPropagationConfig propagation;
};

// --- Access Policy Declaration ---

struct RoleDefinition
{
  std::string description;
  std::vector<std::string> permissions;
  std::vector<std::string> databases;
  std::string masking_json;
  std::string row_level_security_json;
  std::string restrictions_json;
};

struct AccessReviewConfig
{
  std::string mode;
  std::string unused_access_threshold;
  bool excessive_access_detection{false};
  std::string service_account_review;
  bool risk_based_prioritization{false};
  std::string review_channel;
  bool auto_revoke_unused{false};
};

struct MaskingPolicy
{
  std::string mask_type;
  std::string pattern;
};

struct AccessPolicyDecl
{
  Visibility visibility;
  std::string name;
  AccessModel model{AccessModel::RBAC};
  std::map<std::string, RoleDefinition> roles;
  std::string attributes_json;
  AccessReviewConfig access_review;
  std::map<std::string, MaskingPolicy> masking_policies;
};

// --- Quality Policy Declaration ---

struct GovProfilingConfig
{
  bool enabled{false};
  std::string scan_interval;
  int sample_size{10000};
  std::string full_scan_interval;
  std::vector<std::string> targets;
};

struct QualityScoringConfig
{
  bool enabled{false};
  double accuracy_weight{0.25};
  double completeness_weight{0.20};
  double consistency_weight{0.15};
  double timeliness_weight{0.15};
  double validity_weight{0.15};
  double uniqueness_weight{0.10};
  double minimum_score{0.80};
  std::string trend_analysis;
  std::string report_channel;
};

struct DataOpsIntegrationConfig
{
  bool create_incident_on_violation{false};
  std::string minimum_severity;
};

struct QualityPolicyDecl
{
  Visibility visibility;
  std::string name;
  GovProfilingConfig profiling;
  std::string rules_json;
  QualityScoringConfig scoring;
  DataOpsIntegrationConfig dataops_integration;
};

// --- Lineage Policy Declaration ---

struct LineageDiscoveryConfig
{
  bool enabled{false};
  std::vector<std::string> sources;
  std::vector<std::string> methods;
  std::string scan_interval;
  LineageDepth depth{LineageDepth::COLUMN};
};

struct ImpactAnalysisConfig
{
  bool enabled{false};
  int downstream_depth{10};
  bool include_reports{true};
  bool include_apis{true};
  bool include_ml_models{false};
  bool notification_on_breaking_change{true};
};

struct LineageTagPropagationConfig
{
  bool enabled{false};
  std::string direction;
  std::string inherit_sensitivity;
  bool inherit_pii_tags{true};
};

struct LineagePolicyDecl
{
  Visibility visibility;
  std::string name;
  LineageDiscoveryConfig auto_discover;
  ImpactAnalysisConfig impact_analysis;
  LineageTagPropagationConfig tag_propagation;
  std::string external_sync_json;
};

// --- Compliance Policy Declaration ---

struct DSARConfig
{
  bool enabled{false};
  std::vector<std::string> search_scope;
  std::string response_format;
  bool anonymization_on_export{true};
};

struct ComplianceMonitoringConfig
{
  std::string scan_interval;
  bool scoring{false};
  bool alert_on_non_compliance{true};
  std::string report_channel;
  std::string audit_report_schedule;
};

struct CompliancePolicyDecl
{
  Visibility visibility;
  std::string name;
  std::vector<std::string> regulations;
  std::string gdpr_json;
  std::string ccpa_json;
  std::string hipaa_json;
  std::string bcbs_239_json;
  ComplianceMonitoringConfig monitoring;
  DSARConfig dsar;
};

// --- Lifecycle Policy Declaration ---

struct RetentionRule
{
  std::string max_retention;
  std::string min_retention;
  std::string action_on_expiry;
  std::string archive_tier;
  bool requires_approval{false};
};

struct GovTieringConfig
{
  bool enabled{false};
  std::string hot_to_warm;
  std::string warm_to_cold;
  std::string cold_to_archive;
  std::string targets_json;
};

struct LegalHoldConfig
{
  bool enabled{false};
  bool hold_overrides_retention{true};
  std::string notification_channel;
};

struct CostOptimizationConfig
{
  std::string unused_table_detection;
  bool redundant_copy_detection{false};
  std::string report_channel;
};

struct LifecyclePolicyDecl
{
  Visibility visibility;
  std::string name;
  std::map<std::string, RetentionRule> retention;
  std::string regulatory_retention_json;
  GovTieringConfig tiering;
  LegalHoldConfig legal_hold;
  CostOptimizationConfig cost_optimization;
};

// --- Data Product Declaration ---

struct DataContractSLA
{
  std::string freshness;
  std::string availability;
  double quality_score{0.0};
};

struct DataContractConfig
{
  std::string schema_version;
  DataContractSLA sla;
  std::string schema_json;
  std::string breaking_change_policy;
  std::vector<std::string> consumers;
};

struct DataProductDecl
{
  Visibility visibility;
  std::string name;
  std::string domain;
  std::string owner;
  std::string description;
  DataContractConfig contract;
  std::string quality_json;
  std::string access_json;
};

// --- Contract Policy Declaration ---

struct ContractPolicyDecl
{
  Visibility visibility;
  std::string name;
  bool schema_validation_on_deploy{true};
  bool schema_validation_on_change{true};
  bool breaking_change_detection{true};
  bool notify_consumers{true};
  std::string freshness_check_interval;
  std::string availability_check_interval;
  std::string quality_check_interval;
  std::string versioning_strategy;
  bool auto_version_on_schema_change{false};
  bool require_changelog{false};
};

// --- Master Data Declaration ---

struct MatchingConfig
{
  std::string strategy;
  std::vector<std::string> fields;
  double confidence_threshold{0.90};
  double manual_review_threshold{0.70};
};

struct StewardshipConfig
{
  std::string review_queue;
  double auto_merge_above{0.95};
  double require_approval_below{0.90};
};

struct MasterDataDecl
{
  Visibility visibility;
  std::string name;
  std::string entity;
  std::string golden_source;
  std::vector<std::string> contributing_sources;
  MatchingConfig matching;
  std::string survivorship_json;
  std::string quality_json;
  StewardshipConfig stewardship;
};

// --- External Tool Declaration ---

struct GovExternalToolDecl
{
  Visibility visibility;
  std::string name;
  std::string tool_type;
  std::string connection;
  std::string credentials;
  std::string capabilities_json;
  std::string sync_interval;
  ConflictResolution conflict_resolution{ConflictResolution::MANUAL};
};

// --- Governance Agent Declaration ---

struct GovernanceReportsConfig
{
  std::string governance_scorecard_json;
  std::string compliance_report_json;
  std::string quality_report_json;
  std::string access_review_report_json;
};

struct GovernanceAgentDecl
{
  Visibility visibility;
  std::string name;

  // Common agent fields
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::optional<std::string> endpoint;
  std::optional<std::string> api_key_env;
  std::optional<double> temperature;
  std::optional<std::string> system_prompt;
  std::optional<std::string> budget;

  // Governance pillar references
  std::optional<std::string> catalog;
  std::optional<std::string> glossary;
  std::optional<std::string> classification;
  std::optional<std::string> access_control;
  std::optional<std::string> quality;
  std::optional<std::string> lineage;
  std::optional<std::string> compliance;
  std::optional<std::string> lifecycle;

  // External tools & coordination
  std::vector<std::string> external_tools;
  std::vector<std::string> coordinates_with;

  // Reports
  GovernanceReportsConfig reports;

  // Inherited agent features
  std::vector<std::string> skills;
  std::vector<std::string> guardchains;
  std::optional<std::string> policy;
  std::optional<std::string> agent_md;
};

// ═══════════════════════════════════════════════════════════════
// v0.9.5 Modeling Agent AST Nodes
// ═══════════════════════════════════════════════════════════════

enum class SchemaSourceType
{
  SNOWFLAKE, ORACLE, POSTGRES, MYSQL, SQLSERVER,
  REDSHIFT, BIGQUERY, DATABRICKS,
  S3, ADLS, GCS, HDFS,
  ERWIN, ERSTUDIO, POWERDESIGNER, SPARX_EA,
  DDL, DBT, DATA_LAKE, DELTA_LAKE, ICEBERG, HUDI
};

enum class DimensionalMethodology
{
  STAR, SNOWFLAKE, STARFLAKE, DATA_VAULT
};

enum class ModelingToolType
{
  ERWIN, ERSTUDIO, POWERDESIGNER, SPARX_EA,
  ORACLE_SDDM, DBDIAGRAM, DBT, LIQUIBASE, FLYWAY
};

enum class SyncDirection
{
  READ_ONLY, WRITE_BACK, BIDIRECTIONAL
};

enum class NormalForm
{
  NF1, NF2, NF3, BCNF, NF4, NF5
};

enum class AmendmentType
{
  NON_BREAKING, BREAKING, REQUIRES_REVIEW
};

enum class ERNotation
{
  CHEN, UML, CROWS_FOOT, IDEF1X, IE
};

enum class ModelLevel
{
  CONCEPTUAL, LOGICAL, PHYSICAL
};

// --- SchemaSourceDecl ---
struct SchemaSourceDecl
{
  Visibility visibility;
  std::string name;
  std::string type_str;  // string form of SchemaSourceType
  std::string connection;
  std::string credentials;
  std::vector<std::string> databases;
  std::string scan_interval;
  bool include_views{true};
  bool include_procedures{false};
  bool read_constraints{true};
  bool read_indexes{true};
  bool read_statistics{false};
  std::string path;
  std::string format;
  std::string model_type;
  bool watch{false};
  std::string sync_direction;
  std::string api_connection;
  std::vector<std::string> prefixes;
  int sample_size{1000};
  bool detect_formats{false};
  std::string dialect;
  bool apply_migrations{false};
  std::string project_path;
  std::string manifest_path;
  bool read_sources{true};
  bool read_models{true};
  bool read_tests{false};
  std::vector<std::string> submodel_filter;
  std::string infer_relationships_json;
  std::string schema_evolution_json;
};

// --- ERModelDecl ---
struct ERModelDecl
{
  Visibility visibility;
  std::string name;
  std::string version;
  std::vector<std::string> levels;
  std::string source;
  std::string notation_json;
  std::string domains_json;
  std::string relationship_inference_json;
  std::string sync_json;
};

// --- EntityDecl (v0.9.5 modeling entity, distinct from keyword "entity") ---
struct ModelingEntityDecl
{
  Visibility visibility;
  std::string name;
  std::string domain;
  std::string attributes_json;
  std::string relationships_json;
  std::string glossary_term;
  std::string owner;
  std::string description;
};

// --- DimensionalModelDecl ---
struct DimensionalModelDecl
{
  Visibility visibility;
  std::string name;
  std::string methodology;  // "star", "snowflake", "starflake", "data_vault"
  std::string source;
  std::string facts_json;
  std::string dimensions_json;
  std::vector<std::string> conformed;
  std::string target_platform;
  std::string target_schema;
  std::string output_json;
};

// --- DataMartDecl_v095 ---
struct DataMartDecl_v095
{
  Visibility visibility;
  std::string name;
  std::string dimensional_model;
  std::string purpose;
  std::string owner;
  std::vector<std::string> facts;
  std::vector<std::string> dimensions;
  std::string additional_dimensions_json;
  std::string aggregate_tables_json;
  std::string materialization_json;
  std::string row_level_security_json;
  std::string column_masking_json;
  std::string quality_json;
};

// --- NormalizationAnalysisDecl ---
struct NormalizationAnalysisDecl
{
  Visibility visibility;
  std::string name;
  std::string scope_json;
  std::string target_nf;  // "1NF", "2NF", "3NF", "BCNF", "4NF", "5NF"
  std::string fd_discovery_json;
  std::string report_json;
  std::string governance;
  std::string on_violation;
};

// --- AmendmentConfigDecl ---
struct AmendmentConfigDecl
{
  Visibility visibility;
  std::string name;
  std::string monitor_json;
  std::string change_types_json;
  std::string impact_scope_json;
  std::string approval_json;
  std::string document_json;
};

// --- AmendmentDecl ---
struct AmendmentDecl
{
  Visibility visibility;
  std::string name;
  std::string model;
  std::string type_str;  // "non_breaking", "breaking", "requires_review"
  std::string description;
  std::string changes_json;
  bool auto_analyze{false};
  bool require_approval{true};
};

// --- DataProfileDecl ---
struct DataProfileDecl
{
  Visibility visibility;
  std::string name;
  std::string sources_json;
  std::string profiling_json;
  std::string output_json;
};

// --- ModelingToolDecl ---
struct ModelingToolDecl
{
  Visibility visibility;
  std::string name;
  std::string type_str;  // "erwin", "erstudio", etc.
  std::string path;
  std::string api_url;
  std::string credentials;
  std::string repository;
  std::vector<std::string> submodels;
  std::string sync_json;
  std::string mapping_json;
  std::string on_conflict_json;
};

// --- ModelingAgentDecl ---
struct ModelingAgentDecl
{
  Visibility visibility;
  std::string name;
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::optional<std::string> endpoint;
  std::optional<std::string> api_key_env;
  std::optional<std::string> budget;
  std::vector<std::string> sources;
  std::optional<std::string> catalog;
  std::optional<std::string> governance;
  std::vector<std::string> modeling_tools;
  std::string capabilities_json;
  std::vector<std::string> coordinates_with;
  bool enrich_from_governance{false};
  std::optional<std::string> role;
  std::optional<std::string> purpose;
  std::optional<std::string> jurisdiction;
  std::optional<std::string> autonomy;
  std::vector<std::string> approval_required;
  std::vector<std::string> handoffs;
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
                   ETLAgentDecl, MartDecl, SemanticDecl,
                   MigrationAgentDecl,
                   SchedulerDecl, AuditTableDecl, LogSourceDecl, PlatformDecl,
                   IncidentPolicyDecl, CorrelationDecl, DataOpsAgentDecl,
                   GovCatalogSourceDecl, GovCatalogDecl, GlossaryDecl,
                   ClassificationPolicyDecl, AccessPolicyDecl, QualityPolicyDecl,
                   LineagePolicyDecl, CompliancePolicyDecl, LifecyclePolicyDecl,
                   DataProductDecl, ContractPolicyDecl, MasterDataDecl,
                   GovExternalToolDecl, GovernanceAgentDecl,
                   SchemaSourceDecl, ERModelDecl, ModelingEntityDecl,
                   DimensionalModelDecl, DataMartDecl_v095,
                   NormalizationAnalysisDecl, AmendmentConfigDecl,
                   AmendmentDecl, DataProfileDecl, ModelingToolDecl,
                   ModelingAgentDecl>;
  SourceSpan span;
  Variant node;
};

struct Program
{
  std::vector<StmtPtr> statements;
  std::string manifest;
};
}  // namespace neamc
