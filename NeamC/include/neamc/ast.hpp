// SPDX-License-Identifier: Apache-2.0
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
  Modulo,
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
  std::string content;
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

// ============================================================================
// Agentic Orchestration Types (OpenAI Agents SDK + A2A Protocol)
// ============================================================================

// Handoff target for agent-to-agent control transfer (OpenAI SDK style)
struct HandoffTarget
{
  IdentifierRef agent;
  std::optional<std::string> tool_name_override;
  std::optional<std::string> description;
  std::optional<IdentifierRef> input_filter;
  std::optional<ExprPtr> is_enabled;  // Dynamic enable/disable expression
  std::optional<ExprPtr> on_handoff;  // Callback when handoff occurs
  std::optional<IdentifierRef> input_type;  // Structured input type for handoff
};

// Handoff declaration for grouping handoff targets
struct HandoffDecl
{
  Visibility visibility;
  std::string name;
  std::vector<HandoffTarget> targets;
  std::optional<ExprPtr> on_handoff_callback;
};

// Schema field for A2A Agent Card input/output schemas
struct AgentCardSchema
{
  std::string field_name;
  std::string field_type;  // "string", "number", "boolean", "object", "array"
  bool required{true};
  std::optional<std::string> description;
};

// A2A Agent Card declaration for agent discovery and capabilities
struct AgentCardDecl
{
  Visibility visibility;
  std::string name;
  std::string version;
  std::string description;
  std::vector<std::string> capabilities;
  std::vector<AgentCardSchema> input_schema;
  std::vector<AgentCardSchema> output_schema;
  std::optional<std::string> endpoint_url;   // For A2A discovery
  std::optional<std::string> authentication; // Auth method (bearer, api_key, etc.)
};

// Task status for A2A task lifecycle
enum class TaskStatus
{
  kPending,
  kRunning,
  kCompleted,
  kFailed,
  kCancelled
};

// A2A Task declaration for agent task management
struct TaskDecl
{
  Visibility visibility;
  std::string name;
  std::optional<IdentifierRef> target_agent;
  std::vector<AgentCardSchema> input_schema;
  std::optional<ExprPtr> timeout_ms;
  std::optional<ExprPtr> on_status_change;
};

// Runner declaration for agent loop execution
struct RunnerDecl
{
  Visibility visibility;
  std::string name;
  IdentifierRef entry_agent;
  std::optional<std::size_t> max_turns;
  std::optional<IdentifierRef> tracing;
  std::vector<IdentifierRef> guardrails;        // General guardrails (backward compat)
  std::vector<IdentifierRef> input_guardrails;  // Run before first agent call
  std::vector<IdentifierRef> output_guardrails; // Run before returning final output
  std::optional<ExprPtr> on_turn;               // Callback per turn
  std::optional<ExprPtr> on_complete;           // Callback when loop finishes
};

// ============================================================================
// Extended Agent Declaration
// ============================================================================

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

  // NEW: Handoffs (OpenAI Agents SDK style)
  std::vector<HandoffTarget> handoffs;

  // NEW: Agent Card (A2A Protocol style)
  std::optional<std::unique_ptr<AgentCardDecl>> card;

  // NEW: Structured output type
  std::optional<std::unique_ptr<TypeExpression>> output_type;

  // NEW: Context from AGENTS.md file (Phase 6 - agents.md integration)
  std::optional<std::string> context_from;

  // NEW: MCP server connections (Phase 3)
  std::vector<IdentifierRef> mcp_servers;

  // NEW: Cognitive features (v0.5.0)
  std::optional<std::string> reasoning;
  std::optional<std::string> reflect_json;
  std::optional<std::string> learning_json;
  std::optional<std::vector<std::string>> goals;
  std::optional<std::string> triggers_json;
  std::optional<bool> initiative;
  std::optional<std::string> evolve_json;
  std::optional<std::string> inner_model_json;
  std::optional<std::string> model_path;
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

// Voice Pipeline declaration for STT -> Agent -> TTS orchestration
struct VoicePipelineDecl
{
  Visibility visibility;
  std::string name;
  IdentifierRef agent;
  std::optional<std::string> stt_provider;
  std::optional<std::string> stt_model;
  std::optional<std::string> tts_provider;
  std::optional<std::string> tts_model;
  std::optional<std::string> tts_voice;
  std::optional<std::string> stt_endpoint;
  std::optional<std::string> tts_endpoint;
  std::optional<std::string> tts_format;
  std::optional<std::string> tts_speed;
  std::optional<std::string> tts_instructions;
  std::optional<std::string> stt_language;
  std::optional<std::string> stt_format;
};

struct TryCatchStmt
{
  StmtPtr try_body;
  std::string catch_var;
  StmtPtr catch_body;
};

struct ThrowStmt
{
  ExprPtr value;
};

// Real-time streaming voice agent declaration
struct RealtimeVoiceDecl
{
  Visibility visibility;
  std::string name;
  IdentifierRef agent;
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::optional<std::string> voice;
  std::optional<std::string> vad;
  std::optional<std::string> vad_threshold;
  std::optional<std::string> silence_duration_ms;
  std::optional<std::string> input_format;
  std::optional<std::string> output_format;
  std::optional<std::string> sample_rate;
  std::optional<std::string> speed;
  std::optional<std::string> stt_endpoint;
  std::optional<std::string> tts_endpoint;
  std::optional<std::string> llm_endpoint;
};

struct Statement
{
  using Variant =
      std::variant<ExpressionStmt, EmitStmt, BlockStmt, LetStmt, IfStmt, WhileStmt, ReturnStmt,
                   AssertStmt, WithStmt, TestDecl, TestSuiteDecl, FunctionDecl, SkillDecl,
                   KnowledgeDecl, BudgetDecl, GuardDecl, GuardChainDecl, CapabilityDecl,
                   ToolDecl, MemoryDecl, EnvDecl, ConnectorDecl, WorldModelDecl, PlanDecl,
                   SubagentDecl, AgentDecl, ModuleDecl, ImportDecl, ConstDecl, TypeAlias,
                   DocComment, GrantStmt, CheckpointStmt, RewindStmt,
                   // NEW: Agentic Orchestration declarations
                   HandoffDecl, AgentCardDecl, TaskDecl, RunnerDecl,
                   // Voice pipeline
                   VoicePipelineDecl,
                   // Realtime voice
                   RealtimeVoiceDecl,
                   // Error handling
                   TryCatchStmt, ThrowStmt>;
  SourceSpan span;
  Variant node;
};

struct Program
{
  std::vector<StmtPtr> statements;
  std::string manifest;
};
}  // namespace neamc
