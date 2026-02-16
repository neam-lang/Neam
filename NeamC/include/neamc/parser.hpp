//
// NeamC - Minimal parser for arithmetic expressions and blocks
//

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "neamc/ast.hpp"

namespace neamc
{
enum class TokenType
{
  // Single-character tokens.
  LeftParen,
  RightParen,
  LeftBrace,
  RightBrace,
  Comma,
  Semicolon,
  Colon,
  Hash,
  Minus,
  Plus,
  Slash,
  Star,
  Dot,
  LeftBracket,
  RightBracket,
  Question,
  Dollar,

  // One or two character tokens.
  Bang,
  BangEqual,
  Equal,
  EqualEqual,
  Greater,
  GreaterEqual,
  Less,
  LessEqual,

  // Literals.
  Identifier,
  String,
  Number,
  PathLiteral,

  // Keywords.
  Let,
  If,
  Else,
  While,
  Fun,
  Return,
  Emit,
  Module,
  Import,
  Use,
  Pub,
  Crate,
  Super,
  Const,
  Type,
  Panic,
  CatchPanic,
  Test,
  Suite,
  With,
  As,
  BeforeEach,
  AfterEach,
  BeforeAll,
  AfterAll,
  AssertEq,
  AssertNe,
  AssertTrue,
  AssertFalse,
  AssertSome,
  AssertNone,
  AssertOk,
  AssertErr,
  AssertThrows,
  Ignore,
  Async,
  ShouldPanic,
  Timeout,
  Knowledge,
  Skill,
  Agent,
  Description,
  Params,
  Impl,
  VectorStore,
  EmbeddingModel,
  ChunkSize,
  ChunkOverlap,
  Sources,
  RetrievalStrategy,
  TopK,
  RelevanceThreshold,
  MmrLambda,
  NumHypothetical,
  EnableRelevanceCheck,
  EnableSupportCheck,
  EnableWebFallback,
  EnableQueryDecomposition,
  MaxCorrections,
  MaxIterations,
  EnableReflection,
  SearchDepth,
  IncludeCommunities,
  Provider,
  Model,
  Endpoint,
  ApiKeyEnv,
  Temperature,
  System,
  Skills,
  ConnectedKnowledge,
  Budget,
  Guard,
  GuardChain,
  Capability,
  Grant,
  Tool,
  Memory,
  Checkpoint,
  Rewind,
  Env,
  Connector,
  WorldModel,
  Plan,
  Subagent,
  Requires,
  Guards,
  BudgetCost,
  Capabilities,
  Returns,
  OnObservation,
  OnAction,
  OnToolInput,
  OnToolOutput,
  OnToolCall,
  OnResult,
  To,
  True,
  False,
  Nil,
  DocComment,

  // v0.6.7: External skill adoption
  Extern,
  McpServer,
  Adopt,
  Binding,
  Mcp,
  Http,
  ClaudeBuiltin,
  Method,
  Url,
  Headers,
  ResponsePath,
  Server,
  Command,
  Args,
  BodyTemplate,  // v0.6.8: HTTP body template

  // v0.6.9: Security policy keyword
  Policy,

  // v0.7.0: Data types keywords and operators
  For,
  In,
  Break,
  Continue,
  Not,
  Pipe,         // |>
  DotDotDot,    // ...

  // v0.7.1: OOP — struct, impl blocks, method dispatch
  Struct,
  Fn,
  SelfKw,
  Mut,
  Arrow,        // ->

  // v0.7.1 Phase 2-5: trait, sealed, match, extend, derive, agentic patterns
  Trait,        // "trait"
  Sealed,       // "sealed"
  Match,        // "match"
  FatArrow,     // "=>"
  Underscore,   // "_"
  Extend,       // "extend"
  Derive,       // "derive" (inside #[derive(...)])
  Pipeline,     // "pipeline"
  Dispatch,     // "dispatch"
  Parallel,     // "parallel"
  LoopPattern,  // "loop" (agentic loop pattern)
  WillSet,      // "willSet"
  DidSet,       // "didSet"

  // v0.8: Agent type keywords
  Claw,         // "claw" — prefix for claw agent
  Forge,        // "forge" — prefix for forge agent
  Channel,      // "channel" — channel declaration
  Spawn,        // "spawn" — spawn sub-agent

  Eof
};

struct Token
{
  TokenType type;
  std::string lexeme;
  std::size_t position = 0;
  std::size_t line = 0;
  std::size_t column = 0;
};

class Parser
{
public:
  explicit Parser(std::string source);

  Program parse();

private:
  bool is_at_end() const;
  const Token& peek() const;
  const Token& previous() const;
  const Token& advance();
  bool check(TokenType type) const;
  bool match(TokenType type);
  [[noreturn]] void error(const std::string& message) const;

  void tokenize();

  StmtPtr parse_statement();
  StmtPtr parse_declaration();
  StmtPtr parse_return();
  StmtPtr parse_emit();
  StmtPtr parse_function(const Visibility& visibility);
  StmtPtr parse_skill(const Visibility& visibility);
  StmtPtr parse_extern_skill(const Visibility& visibility);
  StmtPtr parse_mcp_server();
  StmtPtr parse_adopt_statement();
  SkillBindingSpec parse_skill_binding();
  StmtPtr parse_knowledge(const Visibility& visibility);
  StmtPtr parse_agent(const Visibility& visibility);
  StmtPtr parse_claw_agent(const Visibility& visibility);
  StmtPtr parse_forge_agent(const Visibility& visibility);
  StmtPtr parse_channel_decl();
  // v0.8 config helpers
  SessionConfig parse_session_config();
  LoopConfig parse_loop_config();
  SemanticMemoryConfig parse_semantic_memory_config();
  std::vector<LaneConfig> parse_lane_configs();
  bool match_identifier(const std::string& name);
  void warning(const std::string& message) const;
  StmtPtr parse_budget(const Visibility& visibility);
  StmtPtr parse_guard(const Visibility& visibility);
  StmtPtr parse_guardchain(const Visibility& visibility);
  StmtPtr parse_policy(const Visibility& visibility);
  StmtPtr parse_capability(const Visibility& visibility);
  StmtPtr parse_tool(const Visibility& visibility);
  StmtPtr parse_memory(const Visibility& visibility);
  StmtPtr parse_env(const Visibility& visibility);
  StmtPtr parse_connector(const Visibility& visibility);
  StmtPtr parse_world_model(const Visibility& visibility);
  StmtPtr parse_plan(const Visibility& visibility);
  StmtPtr parse_subagent(const Visibility& visibility);
  StmtPtr parse_module_decl();
  StmtPtr parse_import_decl(const Visibility& visibility, bool is_reexport);
  StmtPtr parse_const_decl(const Visibility& visibility);
  StmtPtr parse_type_alias(const Visibility& visibility);
  StmtPtr parse_doc_comment(Token first_token);
  StmtPtr parse_test_decl_statement();
  StmtPtr parse_test_suite_statement();
  StmtPtr parse_with_statement();
  StmtPtr parse_assert_statement(TokenType type);
  StmtPtr parse_grant_statement();
  StmtPtr parse_checkpoint_statement();
  StmtPtr parse_rewind_statement();
  StmtPtr parse_let();
  StmtPtr parse_for_in();
  StmtPtr parse_break_stmt();
  StmtPtr parse_continue_stmt();
  StmtPtr parse_struct_decl(const Visibility& visibility);
  StmtPtr parse_impl_block();
  StmtPtr parse_trait_decl(const Visibility& visibility);
  StmtPtr parse_sealed_decl(const Visibility& visibility);
  StmtPtr parse_extend_block();
  StmtPtr parse_pipeline_decl();
  StmtPtr parse_dispatch_decl();
  StmtPtr parse_parallel_decl();
  StmtPtr parse_loop_pattern_decl();
  ExprPtr parse_match_expr();
  StmtPtr parse_block();
  BlockStmt parse_block_node();
  StmtPtr parse_if();
  StmtPtr parse_while();
  TestAttribute parse_test_attribute();
  std::vector<TestAttribute> parse_test_attributes();
  std::unique_ptr<TestDecl> parse_test_decl_node(std::vector<TestAttribute> attributes);
  std::unique_ptr<TestSuiteDecl> parse_test_suite_node();
  SkillParam parse_skill_param();
  ToolParam parse_tool_param();
  BudgetCost parse_budget_cost();
  std::unique_ptr<GuardHandler> parse_guard_handler();
  std::vector<SkillParam> parse_skill_params();
  std::vector<IdentifierRef> parse_identifier_list();
  std::vector<KnowledgeSource> parse_knowledge_sources();
  BudgetDimension parse_budget_dimension();
  Visibility parse_visibility();
  std::vector<std::string> parse_module_path();
  std::vector<std::string> parse_import_items();
  ExprPtr parse_expression();
  ExprPtr parse_assignment();
  ExprPtr parse_pipe();
  ExprPtr parse_equality();
  ExprPtr parse_contains();
  ExprPtr parse_comparison();
  ExprPtr parse_term();
  ExprPtr parse_factor();
  ExprPtr parse_unary();
  ExprPtr parse_call();
  ExprPtr parse_primary();
  ExprPtr parse_fstring(const std::string& raw);
  std::unique_ptr<TypeExpression> parse_type_expression();

  std::string source_;
  std::vector<Token> tokens_{};
  std::size_t current_ = 0;
};
}  // namespace neamc
