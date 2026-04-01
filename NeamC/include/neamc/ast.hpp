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

// ═══════════════════════════════════════════════════════════════
// v0.9.6 Analyst Agent AST Nodes
// ═══════════════════════════════════════════════════════════════

// --- Analyst Enums ---
enum class SQLPlatform {
  SNOWFLAKE, BIGQUERY, REDSHIFT, DATABRICKS,
  POSTGRES, MYSQL, ORACLE, SQLSERVER,
  DUCKDB, TRINO, SPARK
};

enum class OutputFormatType {
  EXCEL, PDF, HTML, CSV, JSON, MARKDOWN, SLACK, TABLE
};

enum class ChartType {
  BAR, LINE, PIE, SCATTER, AREA,
  COMBO, TREEMAP, HEATMAP, HISTOGRAM
};

enum class AnalysisType {
  TREND, OUTLIER, CORRELATION, SEGMENTATION,
  YOY, FORECAST, DRILL_DOWN, PROFILING
};

// --- SQLConnectionDecl ---
struct SQLConnectionDecl {
  Visibility visibility;
  std::string name;
  std::string platform_str;  // "snowflake", "bigquery", etc.
  std::string connection;
  std::string credentials;
  std::string warehouse;
  std::string database;
  std::string schema;
  std::string project;
  std::string dataset;
  std::string catalog;
  std::string cluster;
  int timeout{300};
  int max_rows{10000};
  double cost_limit{0.0};
  std::string queue;
  bool prefer_materialized_views{true};
  bool use_result_cache{true};
  bool partition_pruning{true};
  std::string schema_source;
  std::string semantic_layer;
};

// --- DomainContextDecl ---
struct DomainContextDecl {
  Visibility visibility;
  std::string name;
  std::vector<std::string> models;
  std::vector<std::string> dimensional_models;
  std::vector<std::string> marts;
  std::vector<std::string> schema_sources;
  std::string glossary;
  std::vector<std::string> data_products;
  std::string classification;
  std::string access_policy;
  std::vector<std::string> semantic_layers;
  bool query_history{false};
  bool feedback_loop{false};
};

// --- QueryTemplateDecl ---
struct QueryTemplateDecl {
  Visibility visibility;
  std::string name;
  std::string description;
  std::string category;
  std::string params_json;
  std::string sql;
  std::string default_format;
  std::string chart_json;
  std::string classification;
  bool audit{true};
};

// --- QueryOptimizerDecl ---
struct QueryOptimizerDecl {
  Visibility visibility;
  std::string name;
  std::string cost_model;
  double max_cost_per_query{0.0};
  double max_scan_gb{0.0};
  int max_execution_time{300};
  std::string rules_json;
  bool explain_optimizations{true};
  bool show_cost_comparison{true};
};

// --- ExecutionPolicyDecl ---
struct ExecutionPolicyDecl {
  Visibility visibility;
  std::string name;
  int max_rows{10000};
  double max_cost{0.0};
  int timeout{300};
  bool read_only{true};
  bool apply_masking{true};
  bool apply_row_level_security{true};
  bool audit_all_queries{true};
  bool retry_on_timeout{false};
  bool retry_with_smaller_warehouse{false};
  bool cache_results{true};
  std::string cache_ttl;
  std::string cache_key;
};

// --- OutputFormatDecl ---
struct OutputFormatDecl {
  Visibility visibility;
  std::string name;
  std::string type_str;  // "excel", "pdf", "html", etc.
  std::string excel_json;
  std::string pdf_json;
  std::string html_json;
  std::string csv_json;
  std::string json_config_json;
  std::string slack_json;
};

// --- QueryLibraryDecl ---
struct QueryLibraryDecl {
  Visibility visibility;
  std::string name;
  std::string storage;
  std::string path;
  std::vector<std::string> categories;
  bool tags{false};
  std::string library_visibility;
  bool approval_required{false};
  bool track_usage{false};
  bool track_performance{false};
  bool suggest_similar{false};
  bool auto_optimize{false};
};

// --- AnalysisScheduleDecl ---
struct AnalysisScheduleDecl {
  Visibility visibility;
  std::string name;
  std::string query;
  std::string cron;
  std::string connection;
  std::string format;
  std::string output_path;
  std::string delivery_json;
  bool audit{true};
  std::string budget;
};

// --- AnalystAgentDecl ---
struct AnalystAgentDecl {
  Visibility visibility;
  std::string name;
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::optional<std::string> system_prompt;
  double temperature{0.2};
  std::optional<std::string> endpoint;
  std::optional<std::string> api_key_env;
  std::vector<std::string> connections;
  std::string domain_context;
  std::vector<std::string> models;
  std::vector<std::string> dimensional_models;
  std::vector<std::string> marts;
  std::string glossary;
  std::vector<std::string> semantic_layers;
  std::string governance;
  std::string classification;
  std::string access_policy;
  std::string optimizer;
  std::string execution_policy;
  std::string query_library;
  std::string default_output;
  std::vector<std::string> output_formats;
  std::vector<std::string> skills;
  std::vector<std::string> extern_skills;
  std::vector<std::string> coordinates_with;
  std::vector<std::string> handoffs;
  std::optional<std::string> role;
  std::optional<std::string> purpose;
  std::optional<std::string> autonomy;
  std::optional<std::string> budget;
};

// ═══════════════════════════════════════════════════════════════
// v0.9.7 Data Pipeline Deployment AST Nodes
// ═══════════════════════════════════════════════════════════════

enum class DeployStrategy {
  ROLLING,
  BLUE_GREEN,
  CANARY
};

enum class VersioningScheme {
  SEMVER,
  TIMESTAMP,
  GIT_SHA
};

struct DeployTargetDecl {
  Visibility visibility;
  std::string name;
  std::string environment;
  std::string connection;
  std::string namespace_name;
  std::string region;
  std::string tags_json;
  std::string variables_json;
  bool frozen = false;
  std::string freeze_reason;
};

struct PromotionRuleDecl {
  Visibility visibility;
  std::string name;
  std::string from_env;
  std::string to_env;
  bool require_tests = true;
  bool require_approval = false;
  std::vector<std::string> approvers;
  bool auto_promote = false;
  std::string cooldown;
  std::string gate_checks_json;
};

struct RollbackPolicyDecl {
  Visibility visibility;
  std::string name;
  std::string strategy;
  bool keep_data = true;
  bool notify_dataops = true;
  std::string max_rollback_window;
  bool reconciliation = true;
  std::string pre_rollback_json;
  std::string notifications_json;
};

struct ArtifactRegistryDecl {
  Visibility visibility;
  std::string name;
  std::string storage;
  std::string path;
  std::string versioning;
  std::string retention;
  bool sign_artifacts = false;
  std::string checksum;
  bool immutable = true;
};

struct DeployConfigDecl {
  Visibility visibility;
  std::string name;
  std::string target;
  std::string strategy;
  bool approval_gate = false;
  std::string pipeline_ref;
  std::string pre_deploy_json;
  std::string post_deploy_json;
  std::string notifications_json;
  std::string schedule;
  bool auto_rollback = true;
  std::string rollback_policy;
  std::string artifact_registry;
};

// ═══════════════════════════════════════════════════════════════
// v0.9.8 Data Scientist Agent AST Nodes
// ═══════════════════════════════════════════════════════════════

enum class DSProblemType {
  CLASSIFICATION, REGRESSION, CLUSTERING, TIME_SERIES,
  RECOMMENDATION, ANOMALY_DETECTION, NLP, CAUSAL_INFERENCE,
  ASSOCIATION, OPTIMIZATION
};

enum class StatTestType {
  T_TEST_ONE, T_TEST_TWO, T_TEST_PAIRED,
  MANN_WHITNEY_U, KRUSKAL_WALLIS, WILCOXON,
  CHI_SQUARE, FISHER_EXACT,
  ANOVA, MANOVA,
  PEARSON, SPEARMAN, KENDALL,
  SHAPIRO_WILK, ANDERSON_DARLING, KS_TEST,
  LEVENE, BARTLETT,
  BAYESIAN_T_TEST, BAYESIAN_REGRESSION
};

enum class InterpreterRuntime { PYTHON, R };

struct ProblemStatementDecl {
  Visibility visibility;
  std::string name;
  std::string statement;
  std::string business_context_json;
  std::string constraints_json;
  std::string deliverables_json;
};

struct HypothesisTestDecl {
  Visibility visibility;
  std::string name;
  std::string null_hypothesis;
  std::string alternative;
  std::string test_type;
  double significance_level = 0.05;
  double power = 0.80;
  std::string effect_size;
  std::string data_source;
  std::string group_a;
  std::string group_b;
  std::string assumptions_json;
  std::string if_significant_json;
  std::string if_not_significant_json;
};

struct FeatureEngineeringDecl {
  Visibility visibility;
  std::string name;
  std::string source_tables_json;
  std::string strategies_json;
  std::string selection_json;
  std::string output_json;
};

struct MLExperimentDecl {
  Visibility visibility;
  std::string name;
  std::string problem_type;
  std::string target;
  std::string positive_class;
  std::string dataset;
  double train_test_split = 0.8;
  bool stratify = true;
  std::string cross_validation_json;
  std::string algorithms;
  std::string metrics_json;
  std::string interpretability;
  std::string latency_requirement;
  std::string max_training_time;
  std::string budget;
};

struct AutoMLConfigDecl {
  Visibility visibility;
  std::string name;
  std::string algorithms;
  std::string preprocessing_search_json;
  std::string optimization_json;
  int cv_folds = 5;
  std::string primary_metric;
  bool holdout_validation = true;
  std::string selection_criteria_json;
  bool leaderboard = true;
};

struct HyperparameterConfigDecl {
  Visibility visibility;
  std::string name;
  std::string algorithm;
  std::string search_space_json;
  std::string optimizer_json;
  std::string early_stopping_json;
};

struct StackedModelDecl {
  Visibility visibility;
  std::string name;
  std::string base_learners_json;
  std::string meta_learner_json;
  std::string strategy_json;
  std::string compare_against;
  double improvement_threshold = 0.005;
};

struct EvaluationConfigDecl {
  Visibility visibility;
  std::string name;
  std::string classification_json;
  std::string regression_json;
  std::string clustering_json;
  std::string business_json;
  std::string cv_strategy;
  int outer_folds = 5;
  int inner_folds = 3;
  std::string model_comparison_json;
};

struct ModelRegistryDecl {
  Visibility visibility;
  std::string name;
  std::string storage_json;
  std::string tracking_json;
  std::string model_card_json;
  std::string lifecycle_json;
};

struct ExplainabilityConfigDecl {
  Visibility visibility;
  std::string name;
  std::string global_json;
  std::string local_json;
  std::string fairness_json;
};

struct CodeInterpreterDecl {
  Visibility visibility;
  std::string name;
  std::string runtime;
  std::string version;
  std::string venv_manager;
  std::string profiles_json;
  std::string profile_selection;
  std::string sandbox_json;
  std::string auto_test_json;
  std::string data_bridge_json;
};

struct VenvManagerDecl {
  Visibility visibility;
  std::string name;
  std::string lifecycle_json;
  std::string pool_json;
  std::string dependency_resolver_json;
};

struct NLPPipelineDecl {
  Visibility visibility;
  std::string name;
  std::string preprocessing_json;
  std::string tasks_json;
  std::string embedding_model;
  std::string vector_store;
};

struct ChurnAnalysisDecl {
  Visibility visibility;
  std::string name;
  std::string churn_definition_json;
  std::string features_json;
  std::string primary_model;
  std::string calibration;
  std::string threshold_optimization;
  std::string outputs_json;
};

struct CLVModelDecl {
  Visibility visibility;
  std::string name;
  std::string model_type;
  std::string frequency;
  std::string recency;
  std::string monetary;
  std::string T;
  std::string prediction_periods_json;
  double discount_rate = 0.10;
  std::string segments_json;
};

struct PropensityModelDecl {
  Visibility visibility;
  std::string name;
  std::string target_action;
  std::string training_window;
  std::string features_json;
  std::string algorithm;
  std::string calibration_method;
  std::string score_output_json;
  std::string actions_json;
};

struct RecommendationEngineDecl {
  Visibility visibility;
  std::string name;
  std::string strategy;
  std::string collaborative_json;
  std::string content_based_json;
  std::string blending_json;
  std::string rules_json;
  std::string metrics;
  std::string serving_json;
};

struct ExperimentDesignDecl {
  Visibility visibility;
  std::string name;
  std::string experiment_type;
  std::string control_json;
  std::string treatments_json;
  std::string unit;
  std::string stratify_by;
  std::string power_analysis_json;
  std::string primary_metric_json;
  std::string guardrails_json;
  std::string analysis_json;
};

struct ScenarioAnalysisDecl {
  Visibility visibility;
  std::string name;
  std::string base_model;
  std::string scenarios_json;
  std::string simulation_json;
};

struct DecisionSupportDecl {
  Visibility visibility;
  std::string name;
  std::string deliverables_json;
  std::string confidence_json;
};

struct EDAConfigDecl {
  Visibility visibility;
  std::string name;
  std::string structural_json;
  std::string univariate_json;
  std::string bivariate_json;
  std::string multivariate_json;
  std::string temporal_json;
  std::string performance_json;
  std::string output_json;
};

struct EDATechniqueSelectorDecl {
  Visibility visibility;
  std::string name;
  std::string rules_json;
  std::string output_json;
};

struct SmartConnectorDecl {
  Visibility visibility;
  std::string name;
  std::string discovery_json;
  std::string metadata_cache_json;
};

struct VolumeRouterDecl {
  Visibility visibility;
  std::string name;
  std::string volume_probe_json;
  std::string routing_rules_json;
  std::string auto_escalate_json;
  std::string sampling_strategy_json;
};

struct ComputeConnectorDecl {
  Visibility visibility;
  std::string name;
  std::string engine;
  std::string connection;
  std::string token;
  std::string cluster_config_json;
  std::string idle_timeout;
  bool cost_tracking = true;
};

struct FileConnectorDecl {
  Visibility visibility;
  std::string name;
  std::string base_path;
  bool auto_detect_schema = true;
  bool auto_detect_delimiter = true;
  bool auto_detect_encoding = true;
  std::string supported_formats_json;
  std::string large_file_strategy_json;
};

struct DistributedComputeConfigDecl {
  Visibility visibility;
  std::string name;
  std::string spark_json;
  std::string databricks_json;
  std::string snowflake_json;
  std::string hadoop_json;
  std::string gpu_json;
  std::string selection_logic_json;
};

struct PerformanceConfigDecl {
  Visibility visibility;
  std::string name;
  std::string phase_slas_json;
  std::string total_analysis_sla_json;
  std::string cache_json;
  std::string parallelism_json;
  std::string lazy_eval_json;
  std::string memory_json;
};

struct DataQualityPipelineDecl {
  Visibility visibility;
  std::string name;
  std::string profiling_json;
  std::string scoring_json;
  std::string remediation_json;
  std::string report_json;
  std::string governance_ref;
};

struct SelfCorrectionConfigDecl {
  Visibility visibility;
  std::string name;
  std::string code_errors_json;
  std::string statistical_errors_json;
  std::string model_errors_json;
  std::string reasoning_errors_json;
};

struct SelfAssessmentDecl {
  Visibility visibility;
  std::string name;
  std::string planning_json;
  std::string execution_json;
  std::string interpretation_json;
  std::string communication_json;
  std::string gate_json;
};

struct AdaptiveKnowledgeConfigDecl {
  Visibility visibility;
  std::string name;
  std::string knowledge_sources_json;
  std::string adaptation_json;
  std::string learning_json;
};

struct AnalysisHistoryDecl {
  Visibility visibility;
  std::string name;
  std::string knowledge_base;
  std::string vector_store;
  std::string embedding_model;
  std::string retrieval_strategy;
  std::string record_fields_json;
  std::string retention;
  int max_records = 10000;
};

struct ObservabilityConfigDecl {
  Visibility visibility;
  std::string name;
  std::string feature_monitoring_json;
  std::string prediction_monitoring_json;
  std::string alerts_json;
  std::string auto_remediation_json;
  std::string dataops_ref;
};

struct DataScientistAgentDecl {
  Visibility visibility;
  std::string name;
  std::string provider;
  std::string model;
  std::string system;
  double temperature = 0.2;
  std::string endpoint;
  std::string api_key_env;
  std::string agent_md;
  std::string problem;
  std::string problem_types;
  std::string sub_agents_json;
  std::string forge;
  std::string data_sources_json;
  std::string eda_config;
  std::string feature_config;
  std::string experiment;
  std::string automl;
  std::string ensemble;
  std::vector<std::string> hypotheses;
  std::string evaluation;
  std::string explainability;
  std::string code_interpreter;
  std::string model_registry;
  std::string churn;
  std::string clv;
  std::string propensity;
  std::string recommendation;
  std::string experiment_engine;
  std::string decision_framework;
  std::string volume_router;
  std::string distributed_compute;
  std::string performance;
  std::string data_quality;
  std::string self_correction;
  std::string self_assessment;
  std::string adaptive_knowledge;
  std::string deployment;
  std::vector<std::string> coordinates_with;
  std::vector<std::string> handoffs;
  std::string role;
  std::string purpose;
  std::string autonomy;
  std::string budget;
};

// ═══════════════════════════════════════════════════════════════
// v0.9.8.1 Causal Agent AST Nodes
// ═══════════════════════════════════════════════════════════════

enum class CausalDiscoveryAlgorithm {
  PC, FCI, GES, NOTEARS, DAG_GNN, LINGAM, PCMCI, LLM_ASSISTED
};

enum class CausalIdentificationMethod {
  AUTO, BACKDOOR, FRONTDOOR, INSTRUMENTAL_VARIABLE
};

struct CausalDiscoveryDecl {
  Visibility visibility;
  std::string name;
  std::string llm_discovery_json;
  std::string algorithmic_discovery_json;
  std::string merge_strategy_json;
  std::string validation_json;
};

struct SCMDecl {
  Visibility visibility;
  std::string name;
  std::string variables_json;
  std::string exogenous_json;
  std::string latent_confounders_json;
  std::string dag;
};

struct InterventionDecl {
  Visibility visibility;
  std::string name;
  std::string scm;
  std::string do_json;
  std::string outcome;
  std::string identification_json;
  std::string estimation_json;
  bool compare_with_naive = true;
};

struct CounterfactualDecl {
  Visibility visibility;
  std::string name;
  std::string scm;
  std::string evidence_json;
  std::string question;
  std::string abduction_json;
  std::string action_json;
  std::string prediction_json;
  std::string attribution_json;
};

struct BayesianModelDecl {
  Visibility visibility;
  std::string name;
  std::string framework;
  std::string version;
  std::string priors_json;
  std::string likelihood_json;
  std::string sampling_json;
  std::string posterior_json;
  std::string comparison_json;
};

struct CausalEstimatorDecl {
  Visibility visibility;
  std::string name;
  std::string scm;
  std::string treatment;
  std::string outcome;
  std::string primary_json;
  std::string secondary_json;
  std::string heterogeneous_json;
  bool compare_estimators = true;
};

struct QuasiExperimentDecl {
  Visibility visibility;
  std::string name;
  std::string method;
  std::string treatment_time;
  std::string treatment_group;
  std::string control_group;
  std::string outcome;
  std::string covariates;
  bool parallel_trends_test = true;
  bool bayesian = true;
  std::string mcmc_json;
  std::string running_variable;
  double cutoff = 0.0;
  std::string bandwidth;
};

struct CausalSensitivityDecl {
  Visibility visibility;
  std::string name;
  std::string estimator;
  std::string rosenbaum_json;
  bool e_value = true;
  std::string refutations_json;
  std::string assumptions_json;
  std::string output_json;
};

struct CausalDataRequirementsDecl {
  Visibility visibility;
  std::string name;
  std::string temporal_json;
  std::string required_confounders;
  std::string instruments_json;
  std::string natural_experiments;
  std::string quality_json;
};

struct CausalAgentDecl {
  Visibility visibility;
  std::string name;
  std::string provider;
  std::string model;
  std::string system;
  double temperature = 0.3;
  std::string endpoint;
  std::string api_key_env;
  std::string agent_md;
  std::string sub_agents_json;
  std::string forge;
  std::string peer_agent;
  std::string discovery;
  std::string scm;
  std::string intervention;
  std::string counterfactual;
  std::string bayesian_model;
  std::string estimator;
  std::string sensitivity;
  std::string data_requirements;
  std::string code_interpreter;
  std::vector<std::string> coordinates_with;
  std::vector<std::string> handoffs;
  std::string role;
  std::string purpose;
  std::string autonomy;
  std::string budget;
};

// ═══════════════════════════════════════════════════════════════
// v0.9.8.2 MLOps Agent AST Nodes
// ═══════════════════════════════════════════════════════════════

enum class MLDeployStrategy { CANARY, SHADOW, BLUE_GREEN, AB_TEST };
enum class DriftType { DATA, CONCEPT, PREDICTION, LABEL, FEATURE, SCHEMA };
enum class RetrainingStrategy { FULL_RETRAIN, INCREMENTAL_UPDATE, FINE_TUNE, ONLINE_LEARNING, WARM_START, ENSEMBLE_MEMBER_REPLACE };

struct DriftMonitorDecl {
  Visibility visibility;
  std::string name;
  std::string model;
  std::string reference_dataset;
  std::string data_drift_json;
  std::string concept_drift_json;
  std::string prediction_drift_json;
  std::string alerts_json;
  std::string root_cause_analysis_json;
};

struct RetrainingPipelineDecl {
  Visibility visibility;
  std::string name;
  std::string triggers_json;
  std::string data_json;
  std::string training_json;
  std::string validation_json;
  std::string deployment_json;
  std::string notifications_json;
};

struct MLDeployStrategyDecl {
  Visibility visibility;
  std::string name;
  std::string strategy;
  std::string config_json;
};

struct ChampionChallengerDecl {
  Visibility visibility;
  std::string name;
  std::string champion_json;
  std::string challenger_json;
  std::string evaluation_json;
  std::string promotion_json;
  std::string rollback_json;
};

struct ServingInfraDecl {
  Visibility visibility;
  std::string name;
  std::string mode;
  std::string platform_json;
  std::string sla_json;
  std::string cost_json;
  std::string health_json;
};

struct TrainingInfraDecl {
  Visibility visibility;
  std::string name;
  std::string compute_tiers_json;
  std::string selection_json;
  std::string cost_tracking_json;
};

struct MLOpsRollbackDecl {
  Visibility visibility;
  std::string name;
  std::string auto_triggers_json;
  std::string strategy_json;
  std::string post_rollback_json;
  std::string recovery_json;
};

struct MonitoringStackDecl {
  Visibility visibility;
  std::string name;
  std::string evidently_json;
  std::string prometheus_json;
  std::string whylabs_json;
};

struct MLflowConfigDecl {
  Visibility visibility;
  std::string name;
  std::string mcp_server;
  std::string tracking_json;
  std::string registry_json;
  std::string lifecycle_json;
};

struct BusinessKPITrackerDecl {
  Visibility visibility;
  std::string name;
  std::string model;
  std::string kpis_json;
  std::string report_frequency;
  std::string compare_with;
};

struct DatasetVersionDecl {
  Visibility visibility;
  std::string name;
  std::string versioning_tool;
  std::string source;
  std::string query;
  std::string hash_method;
  std::string storage;
  std::string lineage_json;
  std::string schema_validation_json;
};

struct FeedbackLoopDecl {
  Visibility visibility;
  std::string name;
  std::string production_metrics_json;
  std::string recommendations_json;
  std::string trigger_ds_agent_json;
};

struct DecisionEngineDecl {
  Visibility visibility;
  std::string name;
  std::string retrain_policy_json;
  std::string rollback_policy;
  std::string scaling_policy_json;
  std::string human_in_the_loop_json;
};

struct EventBusDecl {
  Visibility visibility;
  std::string name;
  std::string emits_json;
  std::string listens_json;
  std::string routing_json;
};

struct DriftRCADecl {
  Visibility visibility;
  std::string name;
  std::string causal_agent;
  std::string investigation_json;
  std::string actions_json;
};

struct MLOpsAgentDecl {
  Visibility visibility;
  std::string name;
  std::string provider;
  std::string model;
  std::string system;
  double temperature = 0.2;
  std::string endpoint;
  std::string api_key_env;
  std::string agent_md;
  std::string sub_agents_json;
  std::string drift_monitor;
  std::string retraining_pipeline;
  std::string deployment_strategy;
  std::string champion_challenger;
  std::string serving_infra;
  std::string training_infra;
  std::string rollback_policy;
  std::string monitoring_stack;
  std::string mlflow;
  std::string business_kpi_tracker;
  std::string feedback_loop;
  std::string decision_engine;
  std::string event_bus;
  std::vector<std::string> coordinates_with;
  std::vector<std::string> handoffs;
  std::string role;
  std::string purpose;
  std::string autonomy;
  std::string budget;
};

// ═══════════════════════════════════════════════════════════════
// v0.9.8.3 Data Business Analyst Agent AST Nodes
// ═══════════════════════════════════════════════════════════════

struct RequirementsElicitationDecl { Visibility visibility; std::string name; std::string stakeholders_json; std::string methods_json; std::string output_json; };
struct BRDGeneratorDecl { Visibility visibility; std::string name; std::string project_json; std::string objectives_json; std::string scope_json; std::string benefits_json; std::string constraints_json; std::string assumptions_json; std::string risks_json; std::string output_json; };
struct FunctionalSpecDecl { Visibility visibility; std::string name; std::string data_requirements_json; std::string etl_requirements_json; std::string ml_requirements_json; std::string analytics_requirements_json; };
struct NonfunctionalSpecDecl { Visibility visibility; std::string name; std::string performance_json; std::string reliability_json; std::string security_json; std::string scalability_json; std::string data_quality_json; std::string maintainability_json; };
struct AcceptanceCriteriaGenDecl { Visibility visibility; std::string name; std::string patterns_json; bool auto_generate = true; bool review_required = true; std::string quality_json; };
struct DataRequirementsBADecl { Visibility visibility; std::string name; std::string source_to_target_json; std::string target_schema_json; std::string quality_rules_json; };
struct ImpactAnalysisBADecl { Visibility visibility; std::string name; std::string upstream_json; std::string downstream_json; std::string change_scenarios_json; std::string lineage_json; };
struct TraceabilityMatrixDecl { Visibility visibility; std::string name; std::string entries_json; std::string auto_trace_json; std::string reports_json; };
struct ETLRequirementSpecDecl { Visibility visibility; std::string name; std::string pipeline_json; std::string feature_groups_json; std::string quality_gates_json; std::string upstream_dependencies; std::string downstream_consumers; };
struct MLRequirementSpecDecl { Visibility visibility; std::string name; std::string problem_json; std::string success_criteria_json; std::string feature_requirements_json; std::string serving_json; std::string explainability_json; std::string monitoring_json; };
struct GovernanceRequirementSpecDecl { Visibility visibility; std::string name; std::string data_classification_json; std::string access_requirements_json; std::string compliance_json; std::string quality_sla_json; };
struct AnalyticsRequirementSpecDecl { Visibility visibility; std::string name; std::string reports_json; std::string kpi_definitions_json; };
struct StakeholderAnalysisDecl { Visibility visibility; std::string name; std::string stakeholders_json; std::string raci_matrix_json; };
struct UserStoryGeneratorDecl { Visibility visibility; std::string name; std::string source; std::string epics_json; std::string stories_json; std::string generation_json; };
struct ScopeManagementDecl { Visibility visibility; std::string name; std::string bcar_json; std::string change_management_json; };
struct ChangeImpactAnalyzerDecl { Visibility visibility; std::string name; std::string analysis_json; std::string output_json; };

struct DataBAAgentDecl {
  Visibility visibility;
  std::string name;
  std::string provider;
  std::string model;
  std::string system;
  double temperature = 0.3;
  std::string endpoint;
  std::string api_key_env;
  std::string agent_md;
  std::string downstream_agents_json;
  std::string elicitation;
  std::string brd;
  std::string functional_spec;
  std::string nfr_spec;
  std::string data_requirements;
  std::string impact_analysis;
  std::string traceability;
  std::string etl_spec;
  std::string ml_spec;
  std::string governance_spec;
  std::string analytics_spec;
  std::string stakeholders;
  std::string user_stories;
  std::string scope;
  std::vector<std::string> coordinates_with;
  std::vector<std::string> handoffs;
  std::string role;
  std::string purpose;
  std::string autonomy;
  std::string budget;
};

// ═══════════════════════════════════════════════════════════════
// v0.9.8.4 Data Testing Agent AST Nodes
// ═══════════════════════════════════════════════════════════════

struct TestStrategyDecl { Visibility visibility; std::string name; std::string source_spec; std::string source_nfr; std::string levels_json; std::string test_data_json; std::string gates_json; std::string reporting_json; };
struct TestCaseGeneratorDecl { Visibility visibility; std::string name; std::string source; std::string generation_json; std::string output_json; };
struct TestCaseDecl { Visibility visibility; std::string name; std::string requirement; std::string acceptance_criteria; std::string type; std::string priority; std::string preconditions_json; std::string steps_json; std::string expected_result; std::string automation_json; };
struct ETLTestSuiteDecl { Visibility visibility; std::string name; std::string pipeline; std::string connection; std::string tests_json; };
struct DWTestSuiteDecl { Visibility visibility; std::string name; std::string connection; std::string tests_json; };
struct MLTestSuiteDecl { Visibility visibility; std::string name; std::string model; std::string tests_json; };
struct APITestSuiteDecl { Visibility visibility; std::string name; std::string endpoint; std::string auth_json; std::string tests_json; };
struct PerformanceTestSuiteDecl { Visibility visibility; std::string name; std::string pipeline_performance_json; std::string query_performance_json; std::string api_performance_json; };
struct EdgeCaseTestsDecl { Visibility visibility; std::string name; std::string generation_json; std::string tests_json; };
struct SITSuiteDecl { Visibility visibility; std::string name; std::string scope; std::string tests_json; };
struct UATSuiteDecl { Visibility visibility; std::string name; std::string business_validation_json; std::string data_quality_uat_json; };
struct RegressionSuiteDecl { Visibility visibility; std::string name; std::string baseline_json; std::string checks_json; std::string trigger; };
struct QualityGateDecl { Visibility visibility; std::string name; std::string data_quality_gate_json; std::string model_quality_gate_json; std::string api_quality_gate_json; std::string performance_gate_json; std::string uat_gate_json; std::string deployment_decision_json; };
struct TestReportConfigDecl { Visibility visibility; std::string name; std::string execution_json; std::string report_json; std::string notify_json; };
struct DefectManagementDecl { Visibility visibility; std::string name; std::string on_failure_json; std::string rca_json; std::string tracking_json; };

struct DataTestAgentDecl {
  Visibility visibility;
  std::string name;
  std::string provider;
  std::string model;
  std::string system;
  double temperature = 0.2;
  std::string agent_md;
  std::string sub_agents_json;
  std::string forge;
  std::string test_strategy;
  std::string test_generator;
  std::string etl_tests;
  std::string dw_tests;
  std::string ml_tests;
  std::string api_tests;
  std::string performance_tests;
  std::string edge_tests;
  std::string sit_suite;
  std::string uat_suite;
  std::string regression_suite;
  std::string quality_gate;
  std::string report_config;
  std::string defect_mgmt;
  std::vector<std::string> coordinates_with;
  std::vector<std::string> handoffs;
  std::string role;
  std::string purpose;
  std::string autonomy;
  std::string budget;
};

// ═══════════════════════════════════════════════════════════════
// v0.9.9 Data Intelligent Orchestrator AST Nodes
// ═══════════════════════════════════════════════════════════════

struct AgentRegistryDecl { Visibility visibility; std::string name; std::string agents_json; };
struct AgentContractsDecl { Visibility visibility; std::string name; std::string contracts_json; };
struct RACIMatrixDecl { Visibility visibility; std::string name; std::string tasks_json; };
struct TaskUnderstandingDecl { Visibility visibility; std::string name; std::string intent_classifier_json; std::string detail_extraction_json; };
struct TaskDecomposerDecl { Visibility visibility; std::string name; std::string strategy_json; std::string output_json; };
struct CrewFormationDecl { Visibility visibility; std::string name; std::string strategy_json; };
struct PatternSelectorDecl { Visibility visibility; std::string name; std::string patterns_json; std::string selection_json; };
struct ExecutionManagerDIODecl { Visibility visibility; std::string name; std::string modes_json; std::string resources_json; std::string monitoring_json; };
struct DIOStateMachineDecl { Visibility visibility; std::string name; std::string states_json; std::string transitions_json; std::string persistence_json; };
struct DIOErrorHandlingDecl { Visibility visibility; std::string name; std::string strategies_json; std::string self_healing_json; };
struct ResultSynthesizerDecl { Visibility visibility; std::string name; std::string synthesis_json; std::string delivery_json; };
struct InfrastructureProfileDecl { Visibility visibility; std::string name; std::string data_warehouse_json; std::string data_lake_json; std::string databases_json; std::string streaming_json; std::string data_science_json; std::string governance_json; std::string cicd_json; };
struct RoleFrameworkDecl { Visibility visibility; std::string name; std::string roles_json; std::string dio_role_json; };
struct DelegationProtocolDecl { Visibility visibility; std::string name; std::string delegation_json; };
struct DIOAccountabilityDecl { Visibility visibility; std::string name; std::string escalation_json; };

struct DIOAgentDecl {
  Visibility visibility;
  std::string name;
  std::string provider;
  std::string model;
  std::string system;
  double temperature = 0.2;
  std::string mode;
  std::string task;
  std::string agent_md;
  std::string infrastructure;
  std::string agent_registry;
  std::string raci_matrix;
  std::string pattern_selector;
  std::string crew_formation;
  std::string execution_manager;
  std::string state_machine;
  std::string error_handling;
  std::string result_synthesizer;
  std::string managed_agents_json;
  std::string guardrails_json;
  std::vector<std::string> coordinates_with;
  std::string role;
  std::string purpose;
  std::string autonomy;
  std::string budget;
};

// ═══ v1.0: OWASP Security Declarations ═══
struct GoalIntegrityDecl { std::string name; std::vector<std::string> declared_objectives; std::string verification_json; std::string input_guard_ref; std::string output_guard_ref; bool audit = true; };
struct ToolValidatorDecl { std::string name; std::string schema_enforcement; bool additional_properties = false; std::string rate_limits_json; int max_call_depth = 5; bool detect_cycles = true; std::string budget_per_call_json; };
struct AgentIdentityDecl { std::string name; std::string credential_mode; std::string ttl; std::string rotation; std::string scope_json; bool session_binding = true; bool cross_agent_sharing = false; };
struct SupplyChainPolicyDecl { std::string name; std::string agent_md_signing_json; std::string tool_pinning_json; std::string mcp_verification_json; std::string aibom_json; };
struct CodeSandboxDecl { std::string name; std::string runtime; std::string filesystem_json; std::string network_json; std::string resources_json; std::string pre_execution_review_json; bool log_all_executions = true; };
struct MemoryIntegrityDecl { std::string name; std::string hash_algorithm; bool verify_on_read = true; bool verify_on_write = true; std::string provenance_json; std::string access_guard_json; std::string integrity_scan_json; };
struct MessageSecurityDecl { std::string name; std::string signing_json; std::string encryption_json; std::string authentication_json; bool log_all_messages = true; };
struct CircuitBreakerV10Decl { std::string name; int failure_threshold = 3; int success_threshold = 5; std::string half_open_timeout; std::string isolation_json; };
struct HumanGateDecl { std::string name; std::vector<std::string> approve_before; std::string confidence_escalation_json; std::string workflow_json; bool log_all_decisions = true; };
struct AgentAttestationDecl { std::string name; std::string attest_interval; std::string baseline_json; std::string kill_switch_json; std::string collusion_detection_json; };

// v1.0: MCP Security
struct MCPAllowlistDecl { std::string name; std::string servers_json; bool block_unlisted = true; bool alert_on_new = true; };
struct ToolPinningDecl { std::string name; std::string method; bool pin_descriptions = true; bool block_on_change = true; };
struct ContextGuardDecl { std::string name; bool compartmentalize = true; std::string cross_task_sharing; std::string max_context_age; bool purge_on_completion = true; };

// v1.0: AIBOM
struct AIBOMConfigDecl { std::string name; std::string format; std::string version; std::string components_json; std::string provenance_json; bool auto_generate = true; std::string trigger; std::string output_path; std::string eu_ai_act_json; };

// v1.0: Evaluation
struct GymEvaluatorDecl { std::string name; std::string mode; std::string agent_path; std::string dataset_path; std::string graders_json; std::string thresholds_json; std::string reproducibility_json; std::string metrics_json; std::string output_path; };

// v1.0: Cloud Stack
struct GatewayDecl { std::string name; std::string auth_json; std::string rate_limit_json; std::string routes_json; std::string observability_json; };
struct ModelRouterDecl { std::string name; std::string strategy; std::string routes_json; std::string fallback_chain_json; std::string budget_json; };
struct MarketplaceV10Decl { std::string name; std::string package_format_json; std::string publish_requires_json; std::string install_policy_json; };

// v1.0: Special Agents
struct SecuritySentinelAgentDecl { Visibility visibility; std::string name; std::string provider; std::string model; double temperature = 0.2; std::string budget; std::string monitors_json; std::string actions_json; std::string reporting_json; };
struct ProtocolBridgeAgentDecl { Visibility visibility; std::string name; std::string provider; std::string model; double temperature = 0.2; std::string budget; std::string protocols_json; std::string firewall_json; };
struct CostGuardianAgentDecl { Visibility visibility; std::string name; std::string provider; std::string model; double temperature = 0.2; std::string budget; std::string tracking_json; std::string optimization_json; std::string alerts_json; };

// ═══ v1.1: NeamOS Foundation ═══

// Knowledge Card (4 sub-types: concept, policy, decision, skill)
struct KnowledgeCardDecl {
    std::string name;
    std::string card_type;                 // "concept"|"policy"|"decision"|"skill"
    std::string fields_json;               // type-specific fields as JSON
    std::string owner_agent_ref;           // agent reference (cross-validated)
    std::string version;                   // semver
    std::string ttl;                       // duration ("180d", "24h") or empty
    std::string domain;                    // dot-path ("telecom.retention")
    std::string provenance_json;           // {created_by, verified_by, source, last_reviewed}
    SourceSpan span;
};

// Context Assembly (Minimum Viable Context)
struct ContextAssemblyDecl {
    std::string name;
    std::string target_agent_ref;          // agent reference (cross-validated)
    std::string phase;                     // DIO phase or empty for all
    std::vector<std::string> card_refs;    // knowledge_card references (cross-validated)
    int max_context_tokens = 4000;
    std::string assembly_strategy;         // "relevance_ranked"|"chronological"|"priority"
    bool include_metadata = false;
    std::string fallback;                  // "truncate"|"prioritize"|"error"
    SourceSpan span;
};

// Agent Persona (voice, personality, localization)
struct AgentPersonaDecl {
    std::string name;
    std::string target_agent_ref;          // agent reference (cross-validated)
    std::string display_name;
    std::string avatar_path;               // file path (existence checked at compile)
    std::string voice_json;                // {engine, voice_id}
    std::string personality_json;          // {trait: float} all 0.0-1.0
    std::string catchphrase;
    std::string locales_json;              // {lang: {voice_id, catchphrases}}
    std::string animation_set;             // directory path
    SourceSpan span;
};

// Locale Configuration (i18n)
struct LocaleConfigDecl {
    std::string name;
    std::vector<std::string> supported;    // ISO 639-1 codes
    std::vector<std::string> rtl;          // RTL subset
    std::string fallback;                  // must be in supported
    std::string string_table;              // path template "./locales/{lang}.json"
    SourceSpan span;
};

// Governance Rule (declarative policy with typed conditions)
struct GovernanceRuleDecl {
    std::string name;
    std::string trigger;                   // "data_write"|"data_read"|"tool_call"|"agent_spawn"|"config_change"|"budget_exceed"
    std::string condition_json;            // type-checked condition expression as JSON AST
    std::string action_json;               // {enforce_ttl, require_consent, notify, audit, block}
    std::string regulatory_basis;
    std::string effective_date;
    std::string review_date;
    SourceSpan span;
};

// Agent Adapter (external agent interface)
struct AgentAdapterDecl {
    std::string name;
    std::string adapter_type;              // "local_coding_agent"|"gateway_agent"|"cloud_agent"|"custom"
    std::string binary;                    // for local agents
    std::string protocol;                  // "stdin_stdout"|"rpc"|"sse"|"websocket"
    std::string endpoint;                  // for gateway/cloud agents
    std::string capabilities_json;         // ["code_gen", "review", ...]
    std::string communication;             // "stdin_stdout"|"rpc"|"sse"|"websocket"
    bool skill_injection = false;
    std::string cost_tracking;             // "token_based"|"api_based"|"time_based"|"none"
    bool device_key_pairing = false;
    SourceSpan span;
};

// Blueprint (portable ecosystem package)
struct BlueprintDecl {
    std::string name;
    std::string version;                   // semver
    std::string description;
    std::string author;
    std::vector<std::string> agent_refs;   // agent references (cross-validated)
    std::vector<std::string> skill_refs;   // skill references
    std::vector<std::string> card_refs;    // knowledge_card references
    std::string dio_spec_ref;
    std::string parameters_json;           // {param: {type, default, range}}
    SourceSpan span;
};

// KnowledgeWeaver Agent
struct KnowledgeWeaverAgentDecl {
    Visibility visibility;
    std::string name;
    std::string provider;
    std::string model;
    double temperature = 0.2;
    std::string budget;
    std::string fabric_json;               // {card_store, embedding_model, search, certification}
    std::string monitors_json;             // {card_freshness, coverage_gaps, conflict_detection}
    SourceSpan span;
};

// AdaptAgent
struct AdaptAgentDecl {
    Visibility visibility;
    std::string name;
    std::string provider;
    std::string model;
    double temperature = 0.2;
    std::string budget;
    std::string monitors_json;             // {model_releases, protocol_updates, security_advisories}
    std::string proposals_json;            // {channel, requires_approval}
    SourceSpan span;
};

// Storyteller Agent (Dreamland)
struct StorytellerAgentDecl {
    Visibility visibility;
    std::string name;
    std::string provider;
    std::string model;
    double temperature = 0.7;
    std::string budget;
    std::string sub_agents_json;           // {scene_artist, voice_caster, mood_composer}
    std::string safety_json;               // {content_filter, age_groups, image_scan, parent_preview}
    SourceSpan span;
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
                   ModelingAgentDecl,
                   SQLConnectionDecl, DomainContextDecl, QueryTemplateDecl,
                   QueryOptimizerDecl, ExecutionPolicyDecl, OutputFormatDecl,
                   QueryLibraryDecl, AnalysisScheduleDecl, AnalystAgentDecl,
                   DeployTargetDecl, PromotionRuleDecl, RollbackPolicyDecl,
                   ArtifactRegistryDecl, DeployConfigDecl,
                   ProblemStatementDecl, HypothesisTestDecl, FeatureEngineeringDecl,
                   MLExperimentDecl, AutoMLConfigDecl, HyperparameterConfigDecl,
                   StackedModelDecl, EvaluationConfigDecl, ModelRegistryDecl,
                   ExplainabilityConfigDecl, CodeInterpreterDecl, VenvManagerDecl,
                   NLPPipelineDecl, ChurnAnalysisDecl, CLVModelDecl, PropensityModelDecl,
                   RecommendationEngineDecl, ExperimentDesignDecl, ScenarioAnalysisDecl,
                   DecisionSupportDecl, EDAConfigDecl, EDATechniqueSelectorDecl,
                   SmartConnectorDecl, VolumeRouterDecl, ComputeConnectorDecl,
                   FileConnectorDecl, DistributedComputeConfigDecl, PerformanceConfigDecl,
                   DataQualityPipelineDecl, SelfCorrectionConfigDecl, SelfAssessmentDecl,
                   AdaptiveKnowledgeConfigDecl, AnalysisHistoryDecl, ObservabilityConfigDecl,
                   DataScientistAgentDecl,
                   CausalDiscoveryDecl, SCMDecl, InterventionDecl, CounterfactualDecl,
                   BayesianModelDecl, CausalEstimatorDecl, QuasiExperimentDecl,
                   CausalSensitivityDecl, CausalDataRequirementsDecl, CausalAgentDecl,
                   DriftMonitorDecl, RetrainingPipelineDecl, MLDeployStrategyDecl,
                   ChampionChallengerDecl, ServingInfraDecl, TrainingInfraDecl,
                   MLOpsRollbackDecl, MonitoringStackDecl, MLflowConfigDecl,
                   BusinessKPITrackerDecl, DatasetVersionDecl, FeedbackLoopDecl,
                   DecisionEngineDecl, EventBusDecl, DriftRCADecl, MLOpsAgentDecl,
                   RequirementsElicitationDecl, BRDGeneratorDecl,
                   FunctionalSpecDecl, NonfunctionalSpecDecl,
                   AcceptanceCriteriaGenDecl, DataRequirementsBADecl,
                   ImpactAnalysisBADecl, TraceabilityMatrixDecl,
                   ETLRequirementSpecDecl, MLRequirementSpecDecl,
                   GovernanceRequirementSpecDecl, AnalyticsRequirementSpecDecl,
                   StakeholderAnalysisDecl, UserStoryGeneratorDecl,
                   ScopeManagementDecl, ChangeImpactAnalyzerDecl, DataBAAgentDecl,
                   TestStrategyDecl, TestCaseGeneratorDecl, TestCaseDecl,
                   ETLTestSuiteDecl, DWTestSuiteDecl, MLTestSuiteDecl,
                   APITestSuiteDecl, PerformanceTestSuiteDecl, EdgeCaseTestsDecl,
                   SITSuiteDecl, UATSuiteDecl, RegressionSuiteDecl,
                   QualityGateDecl, TestReportConfigDecl, DefectManagementDecl,
                   DataTestAgentDecl,
                   AgentRegistryDecl, AgentContractsDecl, RACIMatrixDecl,
                   TaskUnderstandingDecl, TaskDecomposerDecl, CrewFormationDecl,
                   PatternSelectorDecl, ExecutionManagerDIODecl, DIOStateMachineDecl,
                   DIOErrorHandlingDecl, ResultSynthesizerDecl, InfrastructureProfileDecl,
                   RoleFrameworkDecl, DelegationProtocolDecl, DIOAccountabilityDecl,
                   DIOAgentDecl,
                   // v1.0: OWASP Security
                   GoalIntegrityDecl, ToolValidatorDecl, AgentIdentityDecl,
                   SupplyChainPolicyDecl, CodeSandboxDecl, MemoryIntegrityDecl,
                   MessageSecurityDecl, CircuitBreakerV10Decl, HumanGateDecl,
                   AgentAttestationDecl,
                   // v1.0: MCP Security
                   MCPAllowlistDecl, ToolPinningDecl, ContextGuardDecl,
                   // v1.0: AIBOM + Evaluation
                   AIBOMConfigDecl, GymEvaluatorDecl,
                   // v1.0: Cloud Stack
                   GatewayDecl, ModelRouterDecl, MarketplaceV10Decl,
                   // v1.0: Special Agents
                   SecuritySentinelAgentDecl, ProtocolBridgeAgentDecl,
                   CostGuardianAgentDecl,
                   // v1.1: NeamOS Foundation
                   KnowledgeCardDecl, ContextAssemblyDecl,
                   AgentPersonaDecl, LocaleConfigDecl,
                   GovernanceRuleDecl, AgentAdapterDecl,
                   BlueprintDecl,
                   KnowledgeWeaverAgentDecl, AdaptAgentDecl,
                   StorytellerAgentDecl>;
  SourceSpan span;
  Variant node;
};

struct Program
{
  std::vector<StmtPtr> statements;
  std::string manifest;
};
}  // namespace neamc
