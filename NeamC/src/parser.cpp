// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Parser implementation (tokenized, visitor-friendly)
//

#include "neamc/parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>

#include "neamc/vm/object.hpp"
namespace neamc
{
namespace
{
SourceSpan span_from_token(const Token& token)
{
  return SourceSpan{token.position, token.lexeme.size(), token.line, token.column};
}

SourceSpan merge_span(const SourceSpan& start, const SourceSpan& end)
{
  SourceSpan merged = start;
  const std::size_t end_pos = end.position + end.length;
  if (end_pos > start.position)
  {
    merged.length = end_pos - start.position;
  }
  return merged;
}

std::optional<std::string> extract_string_key(const Expression& expr)
{
  if (const auto* literal = std::get_if<LiteralExpr>(&expr.node))
  {
    if (literal->value.is_string())
    {
      auto* str = vm::as_string(literal->value);
      return std::string(str->chars, str->length);
    }
  }
  if (const auto* ident = std::get_if<IdentifierExpr>(&expr.node))
  {
    return ident->name;
  }
  return std::nullopt;
}

ExprPtr make_literal(vm::Value value, SourceSpan span)
{
  auto expr = std::make_unique<Expression>();
  expr->span = span;
  expr->node = LiteralExpr{std::move(value)};
  return expr;
}

ExprPtr make_identifier(std::string name, SourceSpan span)
{
  auto expr = std::make_unique<Expression>();
  expr->span = span;
  expr->node = IdentifierExpr{std::move(name)};
  return expr;
}

ExprPtr make_assignment(std::string name, ExprPtr value, SourceSpan span)
{
  auto expr = std::make_unique<Expression>();
  expr->span = span;
  expr->node = AssignmentExpr{std::move(name), std::move(value)};
  return expr;
}

ExprPtr make_unary(UnaryOp op, ExprPtr operand, SourceSpan span)
{
  auto expr = std::make_unique<Expression>();
  expr->span = span;
  expr->node = UnaryExpr{op, std::move(operand)};
  return expr;
}

ExprPtr make_binary(BinaryOp op, ExprPtr left, ExprPtr right, SourceSpan span)
{
  auto expr = std::make_unique<Expression>();
  expr->span = span;
  expr->node = BinaryExpr{op, std::move(left), std::move(right)};
  return expr;
}

ExprPtr make_call(ExprPtr callee, std::vector<ExprPtr> arguments, SourceSpan span)
{
  auto expr = std::make_unique<Expression>();
  expr->span = span;
  expr->node = CallExpr{std::move(callee), std::move(arguments)};
  return expr;
}

ExprPtr make_get(ExprPtr object, std::string name, SourceSpan span)
{
  auto expr = std::make_unique<Expression>();
  expr->span = span;
  expr->node = GetExpr{std::move(object), std::move(name)};
  return expr;
}

ExprPtr make_index(ExprPtr base, ExprPtr index, SourceSpan span)
{
  auto expr = std::make_unique<Expression>();
  expr->span = span;
  expr->node = IndexExpr{std::move(base), std::move(index)};
  return expr;
}

ExprPtr make_list(std::vector<ExprPtr> elements, SourceSpan span)
{
  auto expr = std::make_unique<Expression>();
  expr->span = span;
  expr->node = ListExpr{std::move(elements)};
  return expr;
}

ExprPtr make_map(std::vector<MapEntry> entries, SourceSpan span)
{
  auto expr = std::make_unique<Expression>();
  expr->span = span;
  expr->node = MapExpr{std::move(entries)};
  return expr;
}

ExprPtr make_try_expr(ExprPtr expr, SourceSpan span)
{
  auto node = std::make_unique<Expression>();
  node->span = span;
  node->node = TryExpr{std::move(expr)};
  return node;
}

ExprPtr make_panic_expr(ExprPtr message, SourceSpan span)
{
  auto node = std::make_unique<Expression>();
  node->span = span;
  node->node = PanicExpr{std::move(message)};
  return node;
}

ExprPtr make_catch_panic_expr(ExprPtr closure, SourceSpan span)
{
  auto node = std::make_unique<Expression>();
  node->span = span;
  node->node = CatchPanicExpr{std::move(closure)};
  return node;
}

ExprPtr make_context_expr(ExprPtr expr, ExprPtr message, SourceSpan span)
{
  auto node = std::make_unique<Expression>();
  node->span = span;
  node->node = ContextExpr{std::move(expr), std::move(message)};
  return node;
}

ExprPtr make_with_context_expr(ExprPtr expr, std::string key, ExprPtr value, SourceSpan span)
{
  auto node = std::make_unique<Expression>();
  node->span = span;
  node->node = WithContextExpr{std::move(expr), std::move(key), std::move(value)};
  return node;
}

StmtPtr make_expression_stmt(ExprPtr expression, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ExpressionStmt{std::move(expression)};
  return stmt;
}

StmtPtr make_emit_stmt(ExprPtr value, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = EmitStmt{std::move(value)};
  return stmt;
}

StmtPtr make_block_stmt(std::vector<StmtPtr> statements, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = BlockStmt{std::move(statements)};
  return stmt;
}

StmtPtr make_let_stmt(std::string name, ExprPtr initializer, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = LetStmt{std::move(name), std::move(initializer)};
  return stmt;
}

StmtPtr make_if_stmt(ExprPtr condition, StmtPtr then_branch, StmtPtr else_branch, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = IfStmt{std::move(condition), std::move(then_branch), std::move(else_branch)};
  return stmt;
}

StmtPtr make_while_stmt(ExprPtr condition, StmtPtr body, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = WhileStmt{std::move(condition), std::move(body)};
  return stmt;
}

StmtPtr make_return_stmt(ExprPtr value, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ReturnStmt{std::move(value)};
  return stmt;
}

StmtPtr make_assert_stmt(AssertStmt assert_stmt, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(assert_stmt);
  return stmt;
}

StmtPtr make_with_stmt(ExprPtr resource, std::string name, std::unique_ptr<BlockStmt> body,
                       SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = WithStmt{std::move(resource), std::move(name), std::move(body)};
  return stmt;
}

StmtPtr make_test_decl_stmt(TestDecl decl, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr make_test_suite_stmt(TestSuiteDecl decl, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr make_function_decl(Visibility visibility, std::string name, std::vector<std::string> params,
                           StmtPtr body, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = FunctionDecl{std::move(visibility), std::move(name), std::move(params),
                            std::move(body)};
  return stmt;
}

StmtPtr make_skill_decl(Visibility visibility, std::string name, std::string description,
                        std::vector<SkillParam> params, FunctionDecl impl, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = SkillDecl{std::move(visibility), std::move(name), std::move(description),
                         std::move(params), std::move(impl)};
  return stmt;
}

StmtPtr make_agent_decl(Visibility visibility, std::string name, std::string provider,
                        std::string model,
                        std::optional<std::string> endpoint,
                        std::optional<std::string> api_key_env,
                        std::optional<double> temperature,
                        std::optional<std::string> system,
                        std::vector<IdentifierRef> skills,
                        std::vector<IdentifierRef> connected_knowledge,
                        std::vector<IdentifierRef> required_capabilities,
                        std::vector<IdentifierRef> guardchains,
                        std::optional<IdentifierRef> budget,
                        std::optional<IdentifierRef> env,
                        std::optional<IdentifierRef> memory,
                        std::optional<IdentifierRef> world_model,
                        std::optional<IdentifierRef> plan,
                        std::optional<IdentifierRef> connector,
                        std::vector<HandoffTarget> handoffs,
                        std::optional<std::unique_ptr<AgentCardDecl>> card,
                        std::optional<std::unique_ptr<TypeExpression>> output_type,
                        std::optional<std::string> context_from,
                        std::vector<IdentifierRef> mcp_servers,
                        // Cognitive fields (v0.5.0)
                        std::optional<std::string> reasoning,
                        std::optional<std::string> reflect_json,
                        std::optional<std::string> learning_json,
                        std::optional<std::vector<std::string>> goals,
                        std::optional<std::string> triggers_json,
                        std::optional<bool> initiative,
                        std::optional<std::string> evolve_json,
                        std::optional<std::string> model_path,
                        SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  AgentDecl decl;
  decl.visibility = std::move(visibility);
  decl.name = std::move(name);
  decl.provider = std::move(provider);
  decl.model = std::move(model);
  decl.endpoint = std::move(endpoint);
  decl.api_key_env = std::move(api_key_env);
  decl.temperature = std::move(temperature);
  decl.system = std::move(system);
  decl.skills = std::move(skills);
  decl.connected_knowledge = std::move(connected_knowledge);
  decl.required_capabilities = std::move(required_capabilities);
  decl.guardchains = std::move(guardchains);
  decl.budget = std::move(budget);
  decl.env = std::move(env);
  decl.memory = std::move(memory);
  decl.world_model = std::move(world_model);
  decl.plan = std::move(plan);
  decl.connector = std::move(connector);
  decl.handoffs = std::move(handoffs);
  decl.card = std::move(card);
  decl.output_type = std::move(output_type);
  decl.context_from = std::move(context_from);
  decl.mcp_servers = std::move(mcp_servers);
  decl.reasoning = std::move(reasoning);
  decl.reflect_json = std::move(reflect_json);
  decl.learning_json = std::move(learning_json);
  decl.goals = std::move(goals);
  decl.triggers_json = std::move(triggers_json);
  decl.initiative = std::move(initiative);
  decl.evolve_json = std::move(evolve_json);
  decl.model_path = std::move(model_path);
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr make_knowledge_decl(Visibility visibility, std::string name, std::string vector_store,
                            std::string embedding_model,
                            std::size_t chunk_size, std::size_t chunk_overlap,
                            std::vector<KnowledgeSource> sources,
                            RetrievalStrategy retrieval_strategy,
                            RetrievalStrategyOptions strategy_options,
                            SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = KnowledgeDecl{std::move(visibility), std::move(name), std::move(vector_store),
                             std::move(embedding_model), chunk_size, chunk_overlap,
                             std::move(sources), retrieval_strategy, std::move(strategy_options)};
  return stmt;
}

StmtPtr make_budget_decl(Visibility visibility, std::string name,
                         std::vector<BudgetDimension> dimensions, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = BudgetDecl{std::move(visibility), std::move(name), std::move(dimensions)};
  return stmt;
}

StmtPtr make_guard_decl(Visibility visibility, std::string name, std::string description,
                        std::vector<std::unique_ptr<GuardHandler>> handlers, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = GuardDecl{std::move(visibility), std::move(name), std::move(description),
                         std::move(handlers)};
  return stmt;
}

StmtPtr make_guardchain_decl(Visibility visibility, std::string name,
                             std::vector<IdentifierRef> guards, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = GuardChainDecl{std::move(visibility), std::move(name), std::move(guards)};
  return stmt;
}

StmtPtr make_capability_decl(Visibility visibility, std::string name, std::string pattern,
                             SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = CapabilityDecl{std::move(visibility), std::move(name), std::move(pattern)};
  return stmt;
}

StmtPtr make_tool_decl(Visibility visibility, std::string name, std::string description,
                       std::vector<IdentifierRef> capabilities, std::vector<ToolParam> params,
                       std::unique_ptr<TypeExpression> returns_type,
                       std::vector<BudgetCost> budget_costs,
                       std::vector<IdentifierRef> guards, std::unique_ptr<ToolImpl> impl,
                       SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ToolDecl{std::move(visibility), std::move(name), std::move(description),
                        std::move(capabilities), std::move(params), std::move(returns_type),
                        std::move(budget_costs), std::move(guards), std::move(impl)};
  return stmt;
}

StmtPtr make_memory_decl(Visibility visibility, std::string name, std::string backend,
                         std::string retention, int max_events, int snapshot_interval,
                         SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = MemoryDecl{std::move(visibility), std::move(name), std::move(backend),
                          std::move(retention), max_events, snapshot_interval};
  return stmt;
}

StmtPtr make_env_decl(Visibility visibility, std::string name, std::vector<EnvConfig> configs,
                      SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = EnvDecl{std::move(visibility), std::move(name), std::move(configs)};
  return stmt;
}

StmtPtr make_connector_decl(Visibility visibility, std::string name, std::string protocol,
                            std::string endpoint, std::string contract, std::string auth,
                            SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node =
      ConnectorDecl{std::move(visibility), std::move(name), std::move(protocol),
                    std::move(endpoint), std::move(contract), std::move(auth)};
  return stmt;
}

StmtPtr make_world_model_decl(Visibility visibility, std::string name, int tier,
                              std::string state_schema, int update_frequency, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = WorldModelDecl{std::move(visibility), std::move(name), tier,
                              std::move(state_schema), update_frequency};
  return stmt;
}

StmtPtr make_plan_decl(Visibility visibility, std::string name, std::string pattern,
                       int max_depth, bool backtrack, std::string pruning, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = PlanDecl{std::move(visibility), std::move(name), std::move(pattern), max_depth,
                        backtrack, std::move(pruning)};
  return stmt;
}

StmtPtr make_subagent_decl(Visibility visibility, std::string name, std::string base_agent,
                           double budget_share, bool capability_inherit, bool isolation,
                           SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = SubagentDecl{std::move(visibility), std::move(name), std::move(base_agent),
                            budget_share, capability_inherit, isolation};
  return stmt;
}

StmtPtr make_grant_stmt(IdentifierRef capability, IdentifierRef target, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = GrantStmt{std::move(capability), std::move(target)};
  return stmt;
}

StmtPtr make_checkpoint_stmt(std::string label, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = CheckpointStmt{std::move(label)};
  return stmt;
}

StmtPtr make_rewind_stmt(ExprPtr target, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = RewindStmt{std::move(target)};
  return stmt;
}

StmtPtr make_try_catch_stmt(StmtPtr try_body, std::string catch_var, StmtPtr catch_body,
                            SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = TryCatchStmt{std::move(try_body), std::move(catch_var), std::move(catch_body)};
  return stmt;
}

StmtPtr make_throw_stmt(ExprPtr value, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ThrowStmt{std::move(value)};
  return stmt;
}

StmtPtr make_module_decl(std::vector<std::string> path, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ModuleDecl{std::move(path)};
  return stmt;
}

StmtPtr make_import_decl(Visibility visibility, std::vector<std::string> path,
                         std::optional<std::string> alias, std::vector<std::string> items,
                         bool is_wildcard, bool is_reexport, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ImportDecl{std::move(visibility), std::move(path), std::move(alias),
                          std::move(items), is_wildcard, is_reexport};
  return stmt;
}

StmtPtr make_const_decl(Visibility visibility, std::string name,
                        std::unique_ptr<TypeExpression> type, ExprPtr value, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node =
      ConstDecl{std::move(visibility), std::move(name), std::move(type), std::move(value)};
  return stmt;
}

StmtPtr make_type_alias(Visibility visibility, std::string name, std::vector<std::string> params,
                        std::unique_ptr<TypeExpression> type, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node =
      TypeAlias{std::move(visibility), std::move(name), std::move(params), std::move(type)};
  return stmt;
}

StmtPtr make_doc_comment(std::string content, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = DocComment{std::move(content)};
  return stmt;
}
}  // namespace

Parser::Parser(std::string source) : source_(std::move(source)) { tokenize(); }

bool Parser::is_at_end() const
{
  return peek().type == TokenType::Eof;
}

const Token& Parser::peek() const
{
  return tokens_[current_];
}

const Token& Parser::previous() const
{
  return tokens_[current_ - 1];
}

const Token& Parser::advance()
{
  if (!is_at_end())
  {
    ++current_;
  }
  return previous();
}

bool Parser::check(TokenType type) const
{
  if (is_at_end())
  {
    return false;
  }
  return peek().type == type;
}

bool Parser::match(TokenType type)
{
  if (!check(type))
  {
    return false;
  }
  advance();
  return true;
}

[[noreturn]] void Parser::error(const std::string& message) const
{
  throw std::runtime_error("Parse error: " + message);
}

void Parser::tokenize()
{
  static const std::unordered_map<std::string, TokenType> kKeywords = {
      {"let", TokenType::Let},
      {"if", TokenType::If},
      {"else", TokenType::Else},
      {"while", TokenType::While},
      {"fun", TokenType::Fun},
      {"return", TokenType::Return},
      {"emit", TokenType::Emit},
      {"module", TokenType::Module},
      {"import", TokenType::Import},
      {"use", TokenType::Use},
      {"pub", TokenType::Pub},
      {"crate", TokenType::Crate},
      {"super", TokenType::Super},
      {"const", TokenType::Const},
      {"type", TokenType::Type},
      {"panic", TokenType::Panic},
      {"catch_panic", TokenType::CatchPanic},
      {"test", TokenType::Test},
      {"suite", TokenType::Suite},
      {"with", TokenType::With},
      {"as", TokenType::As},
      {"before_each", TokenType::BeforeEach},
      {"after_each", TokenType::AfterEach},
      {"before_all", TokenType::BeforeAll},
      {"after_all", TokenType::AfterAll},
      {"assert_eq", TokenType::AssertEq},
      {"assert_ne", TokenType::AssertNe},
      {"assert_true", TokenType::AssertTrue},
      {"assert_false", TokenType::AssertFalse},
      {"assert_some", TokenType::AssertSome},
      {"assert_none", TokenType::AssertNone},
      {"assert_ok", TokenType::AssertOk},
      {"assert_err", TokenType::AssertErr},
      {"assert_throws", TokenType::AssertThrows},
      {"ignore", TokenType::Ignore},
      {"async", TokenType::Async},
      {"should_panic", TokenType::ShouldPanic},
      {"timeout", TokenType::Timeout},
      {"true", TokenType::True},
      {"false", TokenType::False},
      {"nil", TokenType::Nil},
      {"skill", TokenType::Skill},
      {"agent", TokenType::Agent},
      {"knowledge", TokenType::Knowledge},
      {"description", TokenType::Description},
      {"params", TokenType::Params},
      {"impl", TokenType::Impl},
      {"vector_store", TokenType::VectorStore},
      {"embedding_model", TokenType::EmbeddingModel},
      {"chunk_size", TokenType::ChunkSize},
      {"chunk_overlap", TokenType::ChunkOverlap},
      {"sources", TokenType::Sources},
      {"retrieval_strategy", TokenType::RetrievalStrategy},
      {"top_k", TokenType::TopK},
      {"relevance_threshold", TokenType::RelevanceThreshold},
      {"mmr_lambda", TokenType::MmrLambda},
      {"num_hypothetical", TokenType::NumHypothetical},
      {"enable_relevance_check", TokenType::EnableRelevanceCheck},
      {"enable_support_check", TokenType::EnableSupportCheck},
      {"enable_web_fallback", TokenType::EnableWebFallback},
      {"enable_query_decomposition", TokenType::EnableQueryDecomposition},
      {"max_corrections", TokenType::MaxCorrections},
      {"max_iterations", TokenType::MaxIterations},
      {"enable_reflection", TokenType::EnableReflection},
      {"search_depth", TokenType::SearchDepth},
      {"include_communities", TokenType::IncludeCommunities},
      {"provider", TokenType::Provider},
      {"model", TokenType::Model},
      {"endpoint", TokenType::Endpoint},
      {"api_key_env", TokenType::ApiKeyEnv},
      {"temperature", TokenType::Temperature},
      {"system", TokenType::System},
      {"skills", TokenType::Skills},
      {"connected_knowledge", TokenType::ConnectedKnowledge},
      {"budget", TokenType::Budget},
      {"guard", TokenType::Guard},
      {"guardchain", TokenType::GuardChain},
      {"capability", TokenType::Capability},
      {"grant", TokenType::Grant},
      {"tool", TokenType::Tool},
      {"memory", TokenType::Memory},
      {"checkpoint", TokenType::Checkpoint},
      {"rewind", TokenType::Rewind},
      {"env", TokenType::Env},
      {"connector", TokenType::Connector},
      {"world_model", TokenType::WorldModel},
      {"plan", TokenType::Plan},
      {"subagent", TokenType::Subagent},
      {"requires", TokenType::Requires},
      {"guards", TokenType::Guards},
      {"budget_cost", TokenType::BudgetCost},
      {"capabilities", TokenType::Capabilities},
      {"returns", TokenType::Returns},
      {"on_observation", TokenType::OnObservation},
      {"on_action", TokenType::OnAction},
      {"on_tool_input", TokenType::OnToolInput},
      {"on_tool_output", TokenType::OnToolOutput},
      {"on_tool_call", TokenType::OnToolCall},
      {"on_result", TokenType::OnResult},
      {"to", TokenType::To},
      // NEW: Agentic Orchestration keywords
      {"handoff", TokenType::Handoff},
      {"handoff_to", TokenType::HandoffTo},
      {"handoffs", TokenType::Handoffs},
      {"on_handoff", TokenType::OnHandoff},
      {"input_filter", TokenType::InputFilter},
      {"is_enabled", TokenType::IsEnabled},
      {"tool_name", TokenType::ToolName},
      {"input_type", TokenType::InputType},
      {"card", TokenType::Card},
      {"version", TokenType::Version},
      {"input_schema", TokenType::InputSchema},
      {"output_schema", TokenType::OutputSchema},
      {"auth_method", TokenType::AuthMethod},
      {"task", TokenType::Task},
      {"status", TokenType::Status},
      {"on_status_change", TokenType::OnStatusChange},
      {"runner", TokenType::Runner},
      {"max_turns", TokenType::MaxTurns},
      {"tracing", TokenType::Tracing},
      {"entry_agent", TokenType::EntryAgent},
      {"on_turn", TokenType::OnTurn},
      {"on_complete", TokenType::OnComplete},
      {"guardrails", TokenType::Guardrails},
      {"input_guardrails", TokenType::InputGuardrails},
      {"output_guardrails", TokenType::OutputGuardrails},
      {"a2a_endpoint", TokenType::A2AEndpoint},
      {"a2a_auth", TokenType::A2AAuth},
      {"output_type", TokenType::OutputType},
      {"context_from", TokenType::ContextFrom},
      {"mcp_servers", TokenType::McpServers},
      // Voice pipeline keywords
      {"voice", TokenType::Voice},
      {"stt_provider", TokenType::SttProvider},
      {"stt_model", TokenType::SttModel},
      {"tts_provider", TokenType::TtsProvider},
      {"tts_model", TokenType::TtsModel},
      {"tts_voice", TokenType::TtsVoice},
      {"stt_endpoint", TokenType::SttEndpoint},
      {"tts_endpoint", TokenType::TtsEndpoint},
      {"tts_format", TokenType::TtsFormat},
      {"tts_speed", TokenType::TtsSpeed},
      {"tts_instructions", TokenType::TtsInstructions},
      {"stt_language", TokenType::SttLanguage},
      {"stt_format", TokenType::SttFormat},
      // Realtime voice keywords
      {"realtime_voice", TokenType::RealtimeVoice},
      {"rt_provider", TokenType::RtProvider},
      {"rt_model", TokenType::RtModel},
      {"rt_voice", TokenType::RtVoice},
      {"rt_vad", TokenType::RtVad},
      {"vad_threshold", TokenType::RtVadThreshold},
      {"silence_duration_ms", TokenType::RtSilenceDurationMs},
      {"input_format", TokenType::RtInputFormat},
      {"output_format", TokenType::RtOutputFormat},
      {"sample_rate", TokenType::RtSampleRate},
      {"rt_speed", TokenType::RtSpeed},
      {"rt_stt_endpoint", TokenType::RtSttEndpoint},
      {"rt_tts_endpoint", TokenType::RtTtsEndpoint},
      {"rt_llm_endpoint", TokenType::RtLlmEndpoint},
      // Error handling keywords
      {"try", TokenType::Try},
      {"catch", TokenType::Catch},
      {"throw", TokenType::Throw},
      // Cognitive keywords (v0.5.0)
      {"reasoning", TokenType::Reasoning},
      {"reflect", TokenType::Reflect},
      {"learning", TokenType::Learning},
      {"goals", TokenType::Goals},
      {"triggers", TokenType::Triggers},
      {"initiative", TokenType::Initiative},
      {"evolve", TokenType::Evolve},
      {"inner_model", TokenType::InnerModel},
      {"embedded_config", TokenType::EmbeddedConfig},
      {"model_path", TokenType::ModelPath},
      {"core_identity", TokenType::CoreIdentity},
      {"review_interval", TokenType::ReviewInterval},
      {"review_after", TokenType::ReviewAfter},
      {"min_confidence", TokenType::MinConfidence},
      {"on_low_quality", TokenType::OnLowQuality},
      {"max_revisions", TokenType::MaxRevisions},
      {"feedback_signal", TokenType::FeedbackSignal},
      {"max_adaptations", TokenType::MaxAdaptations},
      {"rollback_on_decline", TokenType::RollbackOnDecline},
      {"max_daily_calls", TokenType::MaxDailyCalls},
      {"max_daily_cost", TokenType::MaxDailyCost},
      {"max_daily_tokens", TokenType::MaxDailyTokens},
      {"on_schedule", TokenType::OnSchedule},
      {"allow_rollback", TokenType::AllowRollback},
      {"mutable", TokenType::MutableFields}};

  tokens_.clear();
  for (std::size_t i = 0; i < source_.size();)
  {
    const char c = source_[i];
    const auto add = [&](TokenType type, std::size_t len = 1)
    { tokens_.push_back(Token{type, source_.substr(i, len), i}), i += len; };

    switch (c)
    {
      case ' ':
      case '\r':
      case '\t':
      case '\n':
        ++i;
        continue;
      case '(':
        add(TokenType::LeftParen);
        continue;
      case ')':
        add(TokenType::RightParen);
        continue;
      case '{':
        add(TokenType::LeftBrace);
        continue;
      case '}':
        add(TokenType::RightBrace);
        continue;
      case ',':
        add(TokenType::Comma);
        continue;
      case ';':
        add(TokenType::Semicolon);
        continue;
      case ':':
        add(TokenType::Colon);
        continue;
      case '#':
        add(TokenType::Hash);
        continue;
      case '.':
        add(TokenType::Dot);
        continue;
      case '[':
        add(TokenType::LeftBracket);
        continue;
      case ']':
        add(TokenType::RightBracket);
        continue;
      case '?':
        add(TokenType::Question);
        continue;
      case '$':
        add(TokenType::Dollar);
        continue;
      case '+':
        add(TokenType::Plus);
        continue;
      case '-':
        add(TokenType::Minus);
        continue;
      case '%':
        add(TokenType::Percent);
        continue;
      case '*':
        add(TokenType::Star);
        continue;
      case '/':
        if (i + 2 < source_.size() && source_[i + 1] == '/' && source_[i + 2] == '/')
        {
          const std::size_t start = i + 3;
          i += 3;
          while (i < source_.size() && source_[i] != '\n')
          {
            ++i;
          }
          const auto value = source_.substr(start, i - start);
          tokens_.push_back(Token{TokenType::DocComment, value, start});
          continue;
        }
        if (i + 1 < source_.size() && source_[i + 1] == '/')
        {
          while (i < source_.size() && source_[i] != '\n')
          {
            ++i;
          }
          continue;
        }
        add(TokenType::Slash);
        continue;
      case '!':
        if (i + 1 < source_.size() && source_[i + 1] == '=')
        {
          add(TokenType::BangEqual, 2);
        }
        else
        {
          add(TokenType::Bang);
        }
        continue;
      case '=':
        if (i + 1 < source_.size() && source_[i + 1] == '=')
        {
          add(TokenType::EqualEqual, 2);
        }
        else
        {
          add(TokenType::Equal);
        }
        continue;
      case '<':
        if (i + 1 < source_.size() && source_[i + 1] == '=')
        {
          add(TokenType::LessEqual, 2);
        }
        else
        {
          add(TokenType::Less);
        }
        continue;
      case '>':
        if (i + 1 < source_.size() && source_[i + 1] == '=')
        {
          add(TokenType::GreaterEqual, 2);
        }
        else
        {
          add(TokenType::Greater);
        }
        continue;
      case '"':
      {
        std::size_t start = i + 1;
        ++i;
        while (i < source_.size() && source_[i] != '"')
        {
          ++i;
        }
        if (i >= source_.size())
        {
          error("Unterminated string literal");
        }
        const auto value = source_.substr(start, i - start);
        tokens_.push_back(Token{TokenType::String, value, start});
        ++i;  // consume closing quote
        continue;
      }
      default:
        break;
    }

    if (std::isdigit(static_cast<unsigned char>(c)))
    {
      std::size_t start = i;
      while (i < source_.size() && std::isdigit(static_cast<unsigned char>(source_[i])))
      {
        ++i;
      }
      if (i < source_.size() && source_[i] == '.')
      {
        ++i;
        while (i < source_.size() && std::isdigit(static_cast<unsigned char>(source_[i])))
        {
          ++i;
        }
      }
      tokens_.push_back(Token{TokenType::Number, source_.substr(start, i - start), start});
      continue;
    }

    if (c == 'p' && i + 1 < source_.size() && source_[i + 1] == '"')
    {
      std::size_t start = i + 2;
      i += 2;
      while (i < source_.size() && source_[i] != '"')
      {
        ++i;
      }
      if (i >= source_.size())
      {
        error("Unterminated path literal");
      }
      const auto value = source_.substr(start, i - start);
      tokens_.push_back(Token{TokenType::PathLiteral, value, start});
      ++i;
      continue;
    }

    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_')
    {
      std::size_t start = i;
      ++i;
      while (i < source_.size() && (std::isalnum(static_cast<unsigned char>(source_[i])) || source_[i] == '_'))
      {
        ++i;
      }
      const auto text = source_.substr(start, i - start);
      auto it = kKeywords.find(text);
      if (it != kKeywords.end())
      {
        tokens_.push_back(Token{it->second, text, start});
      }
      else
      {
        tokens_.push_back(Token{TokenType::Identifier, text, start});
      }
      continue;
    }

    error(std::string("Unexpected character '") + c + "'");
  }

  tokens_.push_back(Token{TokenType::Eof, "", source_.size()});

  std::vector<std::size_t> line_starts;
  line_starts.reserve(64);
  line_starts.push_back(0);
  for (std::size_t i = 0; i < source_.size(); ++i)
  {
    if (source_[i] == '\n')
    {
      line_starts.push_back(i + 1);
    }
  }
  for (auto& token : tokens_)
  {
    auto it = std::upper_bound(line_starts.begin(), line_starts.end(), token.position);
    if (it == line_starts.begin())
    {
      token.line = 0;
      token.column = token.position;
      continue;
    }
    const std::size_t line_index = static_cast<std::size_t>(it - line_starts.begin() - 1);
    token.line = line_index;
    token.column = token.position - line_starts[line_index];
  }
}

StmtPtr Parser::parse_declaration()
{
  if (match(TokenType::DocComment))
  {
    return parse_doc_comment(previous());
  }
  if (match(TokenType::Module))
  {
    return parse_module_decl();
  }
  if (match(TokenType::Import))
  {
    return parse_import_decl(Visibility{}, false);
  }

  Visibility visibility;
  bool has_visibility = false;
  if (check(TokenType::Pub))
  {
    visibility = parse_visibility();
    has_visibility = true;
  }

  if (has_visibility && match(TokenType::Use))
  {
    return parse_import_decl(visibility, true);
  }
  if (match(TokenType::Const))
  {
    return parse_const_decl(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Type))
  {
    return parse_type_alias(has_visibility ? visibility : Visibility{});
  }
  if (check(TokenType::Hash) || check(TokenType::Test))
  {
    return parse_test_decl_statement();
  }
  if (match(TokenType::Suite))
  {
    return parse_test_suite_statement();
  }
  if (match(TokenType::Skill))
  {
    return parse_skill(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Budget))
  {
    return parse_budget(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Guard))
  {
    return parse_guard(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::GuardChain))
  {
    return parse_guardchain(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Capability))
  {
    return parse_capability(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Tool))
  {
    return parse_tool(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Memory))
  {
    return parse_memory(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Env))
  {
    return parse_env(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Connector))
  {
    return parse_connector(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::WorldModel))
  {
    return parse_world_model(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Plan))
  {
    return parse_plan(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Subagent))
  {
    return parse_subagent(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Knowledge))
  {
    return parse_knowledge(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Agent))
  {
    return parse_agent(has_visibility ? visibility : Visibility{});
  }
  // NEW: Agentic Orchestration declarations
  if (match(TokenType::Handoff))
  {
    return parse_handoff(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Task))
  {
    return parse_task(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Runner))
  {
    return parse_runner(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Voice))
  {
    return parse_voice_pipeline(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::RealtimeVoice))
  {
    return parse_realtime_voice(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Card))
  {
    return parse_agent_card(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Fun))
  {
    return parse_function(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Let))
  {
    return parse_let();
  }
  if (has_visibility)
  {
    error("Visibility modifier must be applied to a declaration");
  }
  return parse_statement();
}

StmtPtr Parser::parse_statement()
{
  if (match(TokenType::If))
  {
    return parse_if();
  }
  if (match(TokenType::While))
  {
    return parse_while();
  }
  if (match(TokenType::Return))
  {
    return parse_return();
  }
  if (match(TokenType::Emit))
  {
    return parse_emit();
  }
  if (match(TokenType::Grant))
  {
    return parse_grant_statement();
  }
  if (match(TokenType::Checkpoint))
  {
    return parse_checkpoint_statement();
  }
  if (match(TokenType::Rewind))
  {
    return parse_rewind_statement();
  }
  if (match(TokenType::With))
  {
    return parse_with_statement();
  }
  if (match(TokenType::AssertEq) || match(TokenType::AssertNe) || match(TokenType::AssertTrue) ||
      match(TokenType::AssertFalse) || match(TokenType::AssertSome) || match(TokenType::AssertNone) ||
      match(TokenType::AssertOk) || match(TokenType::AssertErr) || match(TokenType::AssertThrows))
  {
    return parse_assert_statement(previous().type);
  }
  if (match(TokenType::Try))
  {
    return parse_try_catch();
  }
  if (match(TokenType::Throw))
  {
    return parse_throw();
  }
  if (match(TokenType::LeftBrace))
  {
    return parse_block();
  }

  auto expr = parse_expression();
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after expression");
  }
  auto span = expr->span;  // Save span before moving
  return make_expression_stmt(std::move(expr), span);
}

StmtPtr Parser::parse_return()
{
  SourceSpan span = span_from_token(previous());
  ExprPtr value;
  if (!check(TokenType::Semicolon))
  {
    value = parse_expression();
    span = merge_span(span, value->span);
  }
  else
  {
    value = make_literal(vm::Value::Nil(), span);
  }
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after return value");
  }
  return make_return_stmt(std::move(value), span);
}

StmtPtr Parser::parse_emit()
{
  SourceSpan span = span_from_token(previous());
  auto value = parse_expression();
  span = merge_span(span, value->span);
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after emit value");
  }
  return make_emit_stmt(std::move(value), span);
}

StmtPtr Parser::parse_module_decl()
{
  SourceSpan span = span_from_token(previous());
  auto path = parse_module_path();
  span = merge_span(span, span_from_token(previous()));
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after module declaration");
  }
  return make_module_decl(std::move(path), span);
}

StmtPtr Parser::parse_import_decl(const Visibility& visibility, bool is_reexport)
{
  SourceSpan span = span_from_token(previous());
  auto path = parse_module_path();
  std::vector<std::string> items;
  bool is_wildcard = false;

  if (match(TokenType::Dot))
  {
    if (match(TokenType::Star))
    {
      is_wildcard = true;
    }
    else if (match(TokenType::LeftBrace))
    {
      items = parse_import_items();
      if (!match(TokenType::RightBrace))
      {
        error("Expected '}' after import list");
      }
    }
    else
    {
      error("Expected '*' or '{' after '.' in import");
    }
  }

  std::optional<std::string> alias;
  if (match(TokenType::As))
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected identifier after 'as'");
    }
    alias = previous().lexeme;
  }
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after import");
  }
  return make_import_decl(visibility, std::move(path), std::move(alias), std::move(items),
                          is_wildcard, is_reexport, span);
}

StmtPtr Parser::parse_const_decl(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected constant name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::Colon))
  {
    error("Expected ':' after constant name");
  }
  auto type = parse_type_expression();
  if (!match(TokenType::Equal))
  {
    error("Expected '=' after constant type");
  }
  auto value = parse_expression();
  span = merge_span(span, value->span);
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after constant declaration");
  }
  return make_const_decl(visibility, name, std::move(type), std::move(value), span);
}

StmtPtr Parser::parse_type_alias(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected type alias name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::Equal))
  {
    error("Expected '=' after type alias name");
  }
  auto type = parse_type_expression();
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after type alias");
  }
  return make_type_alias(visibility, name, {}, std::move(type), span);
}

StmtPtr Parser::parse_doc_comment(Token first_token)
{
  SourceSpan span = span_from_token(first_token);
  std::string content = first_token.lexeme;
  while (match(TokenType::DocComment))
  {
    content.append("\n");
    content.append(previous().lexeme);
    span = merge_span(span, span_from_token(previous()));
  }
  return make_doc_comment(std::move(content), span);
}

StmtPtr Parser::parse_function(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected function name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftParen))
  {
    error("Expected '(' after function name");
  }
  std::vector<std::string> params;
  if (!check(TokenType::RightParen))
  {
    do
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected parameter name");
      }
      params.push_back(previous().lexeme);
    } while (match(TokenType::Comma));
  }
  if (!match(TokenType::RightParen))
  {
    error("Expected ')' after parameters");
  }
  if (!match(TokenType::LeftBrace))
  {
    error("Expected function body");
  }
  auto body = parse_block();
  return make_function_decl(visibility, name, std::move(params), std::move(body), span);
}

StmtPtr Parser::parse_skill(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected skill name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after skill name");
  }

  std::optional<std::string> description;
  std::vector<SkillParam> params;
  std::optional<FunctionDecl> impl;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Description))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after description");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for description");
      }
      description = previous().lexeme;
      continue;
    }
    if (match(TokenType::Params))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after params");
      }
      params = parse_skill_params();
      continue;
    }
    if (match(TokenType::Impl))
    {
      if (!match(TokenType::LeftParen))
      {
        error("Expected '(' after impl");
      }
      std::vector<std::string> impl_params;
      if (!check(TokenType::RightParen))
      {
        do
        {
          if (!match(TokenType::Identifier))
          {
            error("Expected parameter name in impl");
          }
          impl_params.push_back(previous().lexeme);
        } while (match(TokenType::Comma));
      }
      if (!match(TokenType::RightParen))
      {
        error("Expected ')' after impl parameters");
      }
      if (!match(TokenType::LeftBrace))
      {
        error("Expected impl body");
      }
      auto body = parse_block();
      impl =
          FunctionDecl{visibility, name + ".impl", std::move(impl_params), std::move(body)};
      continue;
    }

    error("Unexpected token in skill block");
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated skill block");
  }
  if (!description.has_value())
  {
    error("Skill missing description");
  }
  if (!impl.has_value())
  {
    error("Skill missing impl");
  }
  return make_skill_decl(visibility, name, *description, std::move(params), std::move(*impl), span);
}

StmtPtr Parser::parse_agent(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected agent name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after agent name");
  }

  std::optional<std::string> provider;
  std::optional<std::string> model;
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
  // NEW: Agentic orchestration fields
  std::vector<HandoffTarget> handoffs;
  std::optional<std::unique_ptr<AgentCardDecl>> card;
  std::optional<std::unique_ptr<TypeExpression>> output_type;
  // NEW: agents.md context integration (Phase 6)
  std::optional<std::string> context_from;
  // NEW: MCP servers (Phase 3)
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

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Provider))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after provider");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for provider");
      }
      provider = previous().lexeme;
      continue;
    }
    if (match(TokenType::Model))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after model");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for model");
      }
      model = previous().lexeme;
      continue;
    }
    if (match(TokenType::Endpoint))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after endpoint");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for endpoint");
      }
      endpoint = previous().lexeme;
      continue;
    }
    if (match(TokenType::ApiKeyEnv))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after api_key_env");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for api_key_env");
      }
      api_key_env = previous().lexeme;
      continue;
    }
    if (match(TokenType::Temperature))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after temperature");
      }
      if (!match(TokenType::Number))
      {
        error("Expected number literal for temperature");
      }
      temperature = std::strtod(previous().lexeme.c_str(), nullptr);
      continue;
    }
    if (match(TokenType::System))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after system");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for system");
      }
      system = previous().lexeme;
      continue;
    }
    if (match(TokenType::Skills))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after skills");
      }
      skills = parse_identifier_list();
      continue;
    }
    if (match(TokenType::ConnectedKnowledge))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after connected_knowledge");
      }
      connected_knowledge = parse_identifier_list();
      continue;
    }
    if (match(TokenType::Requires))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after requires");
      }
      required_capabilities = parse_identifier_list();
      continue;
    }
    if (match(TokenType::Guards))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after guards");
      }
      guardchains = parse_identifier_list();
      continue;
    }
    if (match(TokenType::Budget))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after budget");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected budget identifier");
      }
      budget = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    if (match(TokenType::Env))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after env");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected env identifier");
      }
      env = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    if (match(TokenType::Memory))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after memory");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected memory identifier");
      }
      memory = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    if (match(TokenType::WorldModel))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after world_model");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected world_model identifier");
      }
      world_model = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    if (match(TokenType::Plan))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after plan");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected plan identifier");
      }
      plan = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    if (match(TokenType::Connector))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after connector");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected connector identifier");
      }
      connector = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    // NEW: Parse handoffs (OpenAI Agents SDK style)
    if (match(TokenType::Handoffs))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after handoffs");
      }
      handoffs = parse_handoff_list();
      continue;
    }
    // NEW: Parse inline agent card (A2A Protocol style)
    if (match(TokenType::Card))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after card");
      }
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' for agent card");
      }
      auto card_decl = std::make_unique<AgentCardDecl>();
      card_decl->name = name;  // Use agent name as card name
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match(TokenType::Version))
        {
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after version");
          }
          if (!match(TokenType::String))
          {
            error("Expected string literal for version");
          }
          card_decl->version = previous().lexeme;
        }
        else if (match(TokenType::Description))
        {
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after description");
          }
          if (!match(TokenType::String))
          {
            error("Expected string literal for description");
          }
          card_decl->description = previous().lexeme;
        }
        else if (match(TokenType::Capabilities))
        {
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after capabilities");
          }
          if (!match(TokenType::LeftBracket))
          {
            error("Expected '[' for capabilities list");
          }
          while (!check(TokenType::RightBracket) && !is_at_end())
          {
            if (!match(TokenType::String))
            {
              error("Expected string literal in capabilities");
            }
            card_decl->capabilities.push_back(previous().lexeme);
            if (!match(TokenType::Comma))
            {
              break;
            }
          }
          if (!match(TokenType::RightBracket))
          {
            error("Expected ']' after capabilities");
          }
        }
        else if (match(TokenType::InputSchema))
        {
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after input_schema");
          }
          card_decl->input_schema = parse_card_schema();
        }
        else if (match(TokenType::OutputSchema))
        {
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after output_schema");
          }
          card_decl->output_schema = parse_card_schema();
        }
        else
        {
          error("Unknown card config key in agent");
        }
      }
      if (!match(TokenType::RightBrace))
      {
        error("Expected '}' after agent card");
      }
      card = std::move(card_decl);
      continue;
    }
    // NEW: Parse structured output type
    if (match(TokenType::OutputType))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after output_type");
      }
      output_type = parse_type_expression();
      continue;
    }
    // NEW: Parse mcp_servers (Phase 3)
    if (match(TokenType::McpServers))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after mcp_servers");
      }
      mcp_servers = parse_identifier_list();
      continue;
    }
    // NEW: Parse context_from (agents.md integration - Phase 6)
    if (match(TokenType::ContextFrom))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after context_from");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for context_from path");
      }
      context_from = previous().lexeme;
      continue;
    }
    // NEW: Parse reasoning (v0.5.0) — identifier value
    if (match(TokenType::Reasoning))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after reasoning");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected reasoning mode identifier (chain_of_thought, plan_and_execute, tree_of_thought, self_consistency)");
      }
      reasoning = previous().lexeme;
      continue;
    }
    // NEW: Parse reflect block (v0.5.0) — brace-delimited, encoded to JSON
    if (match(TokenType::Reflect))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after reflect");
      }
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' after reflect:");
      }
      nlohmann::json rj = nlohmann::json::object();
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match(TokenType::Identifier) && previous().lexeme == "after")
        {
          if (!match(TokenType::Colon)) error("Expected ':' after 'after'");
          if (!match(TokenType::Identifier)) error("Expected identifier for after");
          rj["after"] = previous().lexeme;
        }
        else if (match(TokenType::Identifier) && previous().lexeme == "evaluate")
        {
          if (!match(TokenType::Colon)) error("Expected ':' after 'evaluate'");
          if (!match(TokenType::LeftBracket)) error("Expected '[' after evaluate:");
          std::vector<std::string> dims;
          while (!check(TokenType::RightBracket) && !is_at_end())
          {
            if (match(TokenType::Identifier) || match(TokenType::String))
            {
              dims.push_back(previous().lexeme);
            }
            if (!match(TokenType::Comma)) break;
          }
          if (!match(TokenType::RightBracket)) error("Expected ']'");
          rj["evaluate"] = dims;
        }
        else if (match(TokenType::MinConfidence))
        {
          if (!match(TokenType::Colon)) error("Expected ':' after min_confidence");
          if (!match(TokenType::Number)) error("Expected number for min_confidence");
          rj["min_confidence"] = std::strtod(previous().lexeme.c_str(), nullptr);
        }
        else if (match(TokenType::OnLowQuality))
        {
          if (!match(TokenType::Colon)) error("Expected ':' after on_low_quality");
          if (!match(TokenType::LeftBrace)) error("Expected '{' after on_low_quality:");
          nlohmann::json olq = nlohmann::json::object();
          while (!check(TokenType::RightBrace) && !is_at_end())
          {
            if (match(TokenType::Identifier) && previous().lexeme == "strategy")
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::String)) error("Expected string for strategy");
              olq["strategy"] = previous().lexeme;
            }
            else if (match(TokenType::MaxRevisions))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::Number)) error("Expected number for max_revisions");
              olq["max_revisions"] = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
            }
            else if (match(TokenType::Identifier) && previous().lexeme == "escalate_to")
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected value for escalate_to");
              olq["escalate_to"] = previous().lexeme;
            }
            else
            {
              error("Unexpected token in on_low_quality block");
            }
          }
          if (!match(TokenType::RightBrace)) error("Expected '}' after on_low_quality");
          rj["on_low_quality"] = olq;
        }
        else
        {
          error("Unexpected token in reflect block");
        }
      }
      if (!match(TokenType::RightBrace)) error("Expected '}' after reflect block");
      reflect_json = rj.dump();
      continue;
    }
    // NEW: Parse learning block (v0.5.0)
    if (match(TokenType::Learning))
    {
      if (!match(TokenType::Colon)) error("Expected ':' after learning");
      if (!match(TokenType::LeftBrace)) error("Expected '{' after learning:");
      nlohmann::json lj = nlohmann::json::object();
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match(TokenType::Identifier) && previous().lexeme == "strategy")
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string for strategy");
          lj["strategy"] = previous().lexeme;
        }
        else if (match(TokenType::ReviewInterval))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::Number)) error("Expected number for review_interval");
          lj["review_interval"] = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
        }
        else if (match(TokenType::MaxAdaptations))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::Number)) error("Expected number for max_adaptations");
          lj["max_adaptations"] = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
        }
        else if (match(TokenType::RollbackOnDecline))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (match(TokenType::True)) lj["rollback_on_decline"] = true;
          else if (match(TokenType::False)) lj["rollback_on_decline"] = false;
          else error("Expected true/false for rollback_on_decline");
        }
        else
        {
          error("Unexpected token in learning block");
        }
      }
      if (!match(TokenType::RightBrace)) error("Expected '}' after learning block");
      learning_json = lj.dump();
      continue;
    }
    // NEW: Parse goals (v0.5.0) — string list
    if (match(TokenType::Goals))
    {
      if (!match(TokenType::Colon)) error("Expected ':' after goals");
      if (!match(TokenType::LeftBracket)) error("Expected '[' after goals:");
      std::vector<std::string> goal_list;
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::String)) error("Expected string literal in goals");
        goal_list.push_back(previous().lexeme);
        if (!match(TokenType::Comma)) break;
      }
      if (!match(TokenType::RightBracket)) error("Expected ']' after goals");
      goals = std::move(goal_list);
      continue;
    }
    // NEW: Parse triggers block (v0.5.0)
    if (match(TokenType::Triggers))
    {
      if (!match(TokenType::Colon)) error("Expected ':' after triggers");
      if (!match(TokenType::LeftBrace)) error("Expected '{' after triggers:");
      nlohmann::json tj = nlohmann::json::object();
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match(TokenType::OnSchedule))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string for on_schedule");
          tj["on_schedule"] = previous().lexeme;
        }
        else if (match(TokenType::Identifier))
        {
          std::string key = previous().lexeme;
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string value");
          tj[key] = previous().lexeme;
        }
        else
        {
          error("Unexpected token in triggers block");
        }
      }
      if (!match(TokenType::RightBrace)) error("Expected '}' after triggers block");
      triggers_json = tj.dump();
      continue;
    }
    // NEW: Parse initiative (v0.5.0)
    if (match(TokenType::Initiative))
    {
      if (!match(TokenType::Colon)) error("Expected ':' after initiative");
      if (match(TokenType::True)) initiative = true;
      else if (match(TokenType::False)) initiative = false;
      else error("Expected true/false for initiative");
      continue;
    }
    // NEW: Parse evolve block (v0.5.0)
    if (match(TokenType::Evolve))
    {
      if (!match(TokenType::Colon)) error("Expected ':' after evolve");
      if (!match(TokenType::LeftBrace)) error("Expected '{' after evolve:");
      nlohmann::json ej = nlohmann::json::object();
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match(TokenType::MutableFields))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::LeftBracket)) error("Expected '[' after mutable:");
          std::vector<std::string> fields;
          while (!check(TokenType::RightBracket) && !is_at_end())
          {
            if (match(TokenType::Identifier) || match(TokenType::String))
            {
              fields.push_back(previous().lexeme);
            }
            if (!match(TokenType::Comma)) break;
          }
          if (!match(TokenType::RightBracket)) error("Expected ']'");
          ej["mutable"] = fields;
        }
        else if (match(TokenType::ReviewAfter))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::Number)) error("Expected number for review_after");
          ej["review_after"] = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
        }
        else if (match(TokenType::CoreIdentity))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string for core_identity");
          ej["core_identity"] = previous().lexeme;
        }
        else if (match(TokenType::AllowRollback))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (match(TokenType::True)) ej["allow_rollback"] = true;
          else if (match(TokenType::False)) ej["allow_rollback"] = false;
          else error("Expected true/false for allow_rollback");
        }
        else
        {
          error("Unexpected token in evolve block");
        }
      }
      if (!match(TokenType::RightBrace)) error("Expected '}' after evolve block");
      evolve_json = ej.dump();
      continue;
    }
    // NEW: Parse model_path (v0.5.0)
    if (match(TokenType::ModelPath))
    {
      if (!match(TokenType::Colon)) error("Expected ':' after model_path");
      if (!match(TokenType::String)) error("Expected string for model_path");
      model_path = previous().lexeme;
      continue;
    }
    // NEW: Parse budget config inside agent (v0.5.0 — for daily limits)
    if (match(TokenType::MaxDailyCalls))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Number)) error("Expected number");
      // Store in triggers_json if needed — or skip (handled via goals JSON)
      continue;
    }
    if (match(TokenType::MaxDailyCost))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Number)) error("Expected number");
      continue;
    }
    if (match(TokenType::MaxDailyTokens))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Number)) error("Expected number");
      continue;
    }
    error("Unexpected token in agent block");
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated agent block");
  }
  if (!provider.has_value())
  {
    error("Agent missing provider");
  }
  if (!model.has_value())
  {
    error("Agent missing model");
  }
  return make_agent_decl(visibility, name, *provider, *model, std::move(endpoint),
                         std::move(api_key_env), std::move(temperature), std::move(system),
                         std::move(skills), std::move(connected_knowledge),
                         std::move(required_capabilities), std::move(guardchains),
                         std::move(budget), std::move(env), std::move(memory),
                         std::move(world_model), std::move(plan), std::move(connector),
                         std::move(handoffs), std::move(card), std::move(output_type),
                         std::move(context_from), std::move(mcp_servers),
                         std::move(reasoning), std::move(reflect_json),
                         std::move(learning_json), std::move(goals),
                         std::move(triggers_json), std::move(initiative),
                         std::move(evolve_json), std::move(model_path), span);
}

StmtPtr Parser::parse_budget(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected budget name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after budget name");
  }

  std::vector<BudgetDimension> dimensions;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    dimensions.push_back(parse_budget_dimension());
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated budget block");
  }
  return make_budget_decl(visibility, name, std::move(dimensions), span);
}

StmtPtr Parser::parse_guard(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected guard name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after guard name");
  }

  std::string description;
  std::vector<std::unique_ptr<GuardHandler>> handlers;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Description))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after description");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for description");
      }
      description = previous().lexeme;
      continue;
    }
    if (match(TokenType::OnObservation) || match(TokenType::OnAction) ||
        match(TokenType::OnToolInput) || match(TokenType::OnToolOutput) ||
        match(TokenType::OnToolCall) || match(TokenType::OnResult))
    {
      handlers.push_back(parse_guard_handler());
      continue;
    }
    error("Unexpected token in guard block");
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated guard block");
  }
  return make_guard_decl(visibility, name, std::move(description), std::move(handlers), span);
}

StmtPtr Parser::parse_guardchain(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected guardchain name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::Equal))
  {
    error("Expected '=' after guardchain name");
  }
  auto guards = parse_identifier_list();
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after guardchain declaration");
  }
  return make_guardchain_decl(visibility, name, std::move(guards), span);
}

StmtPtr Parser::parse_capability(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected capability name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::Equal))
  {
    error("Expected '=' after capability name");
  }
  if (!match(TokenType::Identifier) || previous().lexeme != "cap")
  {
    error("Expected cap(...) expression");
  }
  if (!match(TokenType::LeftParen))
  {
    error("Expected '(' after cap");
  }
  if (!match(TokenType::String))
  {
    error("Expected string literal for capability pattern");
  }
  const auto pattern = previous().lexeme;
  if (!match(TokenType::RightParen))
  {
    error("Expected ')' after capability pattern");
  }
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after capability declaration");
  }
  return make_capability_decl(visibility, name, pattern, span);
}

StmtPtr Parser::parse_tool(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected tool name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after tool name");
  }

  std::string description;
  std::vector<IdentifierRef> capabilities;
  std::vector<ToolParam> params;
  std::unique_ptr<TypeExpression> returns_type;
  std::vector<BudgetCost> budget_costs;
  std::vector<IdentifierRef> guards;
  std::unique_ptr<ToolImpl> impl;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Description))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after description");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for description");
      }
      description = previous().lexeme;
      continue;
    }
    if (match(TokenType::Capabilities))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after capabilities");
      }
      capabilities = parse_identifier_list();
      continue;
    }
    if (match(TokenType::Params))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after params");
      }
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' to start tool params");
      }
      if (!check(TokenType::RightBrace))
      {
        do
        {
          params.push_back(parse_tool_param());
        } while (match(TokenType::Comma));
      }
      if (!match(TokenType::RightBrace))
      {
        error("Expected '}' after tool params");
      }
      continue;
    }
    if (match(TokenType::Returns))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after returns");
      }
      returns_type = parse_type_expression();
      continue;
    }
    if (match(TokenType::BudgetCost))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after budget_cost");
      }
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' after budget_cost");
      }
      if (!check(TokenType::RightBrace))
      {
        do
        {
          budget_costs.push_back(parse_budget_cost());
        } while (match(TokenType::Comma));
      }
      if (!match(TokenType::RightBrace))
      {
        error("Expected '}' after budget_cost");
      }
      continue;
    }
    if (match(TokenType::Guards))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after guards");
      }
      guards = parse_identifier_list();
      continue;
    }
    if (match(TokenType::Impl))
    {
      if (!match(TokenType::LeftParen))
      {
        error("Expected '(' after impl");
      }
      auto impl_node = std::make_unique<ToolImpl>();
      if (!check(TokenType::RightParen))
      {
        do
        {
          if (!match(TokenType::Identifier))
          {
            error("Expected identifier in tool impl params");
          }
          impl_node->parameters.push_back(previous().lexeme);
        } while (match(TokenType::Comma));
      }
      if (!match(TokenType::RightParen))
      {
        error("Expected ')' after impl params");
      }
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' after impl params");
      }
      impl_node->body = std::make_unique<BlockStmt>(parse_block_node());
      impl = std::move(impl_node);
      continue;
    }
    error("Unexpected token in tool block");
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated tool block");
  }
  if (!impl)
  {
    error("Tool missing impl");
  }
  return make_tool_decl(visibility, name, std::move(description), std::move(capabilities),
                        std::move(params), std::move(returns_type), std::move(budget_costs),
                        std::move(guards), std::move(impl), span);
}

StmtPtr Parser::parse_memory(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected memory name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after memory name");
  }

  std::string backend;
  std::string retention;
  int max_events = 10000;
  int snapshot_interval = 100;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected memory config key");
    }
    const auto key = previous().lexeme;
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after memory config key");
    }
    if (key == "backend" || key == "retention")
    {
      if (!match(TokenType::String))
      {
        error("Expected string literal for memory config");
      }
      if (key == "backend")
      {
        backend = previous().lexeme;
      }
      else
      {
        retention = previous().lexeme;
      }
    }
    else if (key == "max_events" || key == "snapshot_interval")
    {
      if (!match(TokenType::Number))
      {
        error("Expected number literal for memory config");
      }
      const int value = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
      if (key == "max_events")
      {
        max_events = value;
      }
      else
      {
        snapshot_interval = value;
      }
    }
    else
    {
      error("Unknown memory config key");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated memory block");
  }
  return make_memory_decl(visibility, name, std::move(backend), std::move(retention), max_events,
                          snapshot_interval, span);
}

StmtPtr Parser::parse_env(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected env name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after env name");
  }

  std::vector<EnvConfig> configs;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected env config key");
    }
    const auto key = previous().lexeme;
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after env config key");
    }

    EnvConfig config;
    config.key = key;
    if (match(TokenType::String))
    {
      config.value = previous().lexeme;
    }
    else if (match(TokenType::True) || match(TokenType::False))
    {
      config.value = previous().type == TokenType::True ? "true" : "false";
    }
    else if (match(TokenType::Env))
    {
      if (!match(TokenType::LeftParen))
      {
        error("Expected '(' after env");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for env var");
      }
      config.is_env_var = true;
      config.env_var_name = previous().lexeme;
      if (!match(TokenType::RightParen))
      {
        error("Expected ')' after env var");
      }
    }
    else
    {
      error("Expected env config value");
    }
    configs.push_back(std::move(config));
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated env block");
  }
  return make_env_decl(visibility, name, std::move(configs), span);
}

StmtPtr Parser::parse_connector(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected connector name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after connector name");
  }

  std::string protocol;
  std::string endpoint;
  std::string contract;
  std::string auth;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected connector config key");
    }
    const auto key = previous().lexeme;
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after connector config key");
    }
    if (!match(TokenType::String))
    {
      error("Expected string literal for connector config");
    }
    const auto value = previous().lexeme;
    if (key == "protocol")
    {
      protocol = value;
    }
    else if (key == "endpoint")
    {
      endpoint = value;
    }
    else if (key == "contract")
    {
      contract = value;
    }
    else if (key == "auth")
    {
      auth = value;
    }
    else
    {
      error("Unknown connector config key");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated connector block");
  }
  return make_connector_decl(visibility, name, std::move(protocol), std::move(endpoint),
                             std::move(contract), std::move(auth), span);
}

StmtPtr Parser::parse_world_model(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected world_model name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after world_model name");
  }

  int tier = 0;
  std::string state_schema;
  int update_frequency = 1;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected world_model config key");
    }
    const auto key = previous().lexeme;
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after world_model config key");
    }
    if (key == "tier" || key == "update_frequency")
    {
      if (!match(TokenType::Number))
      {
        error("Expected number literal for world_model config");
      }
      const int value = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
      if (key == "tier")
      {
        tier = value;
      }
      else
      {
        update_frequency = value;
      }
    }
    else if (key == "state_schema")
    {
      if (!match(TokenType::String))
      {
        error("Expected string literal for world_model config");
      }
      state_schema = previous().lexeme;
    }
    else
    {
      error("Unknown world_model config key");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated world_model block");
  }
  return make_world_model_decl(visibility, name, tier, std::move(state_schema), update_frequency,
                               span);
}

StmtPtr Parser::parse_plan(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected plan name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after plan name");
  }

  std::string pattern;
  int max_depth = 5;
  bool backtrack = true;
  std::string pruning;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected plan config key");
    }
    const auto key = previous().lexeme;
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after plan config key");
    }
    if (key == "pattern" || key == "pruning")
    {
      if (!match(TokenType::String))
      {
        error("Expected string literal for plan config");
      }
      if (key == "pattern")
      {
        pattern = previous().lexeme;
      }
      else
      {
        pruning = previous().lexeme;
      }
    }
    else if (key == "max_depth")
    {
      if (!match(TokenType::Number))
      {
        error("Expected number literal for plan config");
      }
      max_depth = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
    }
    else if (key == "backtrack")
    {
      if (match(TokenType::True) || match(TokenType::False))
      {
        backtrack = previous().type == TokenType::True;
      }
      else
      {
        error("Expected boolean literal for plan config");
      }
    }
    else
    {
      error("Unknown plan config key");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated plan block");
  }
  return make_plan_decl(visibility, name, std::move(pattern), max_depth, backtrack,
                        std::move(pruning), span);
}

StmtPtr Parser::parse_subagent(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected subagent name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after subagent name");
  }

  std::string base_agent;
  double budget_share = 0.5;
  bool capability_inherit = true;
  bool isolation = false;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected subagent config key");
    }
    const auto key = previous().lexeme;
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after subagent config key");
    }
    if (key == "base_agent")
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected identifier for base_agent");
      }
      base_agent = previous().lexeme;
    }
    else if (key == "budget_share")
    {
      if (!match(TokenType::Number))
      {
        error("Expected number literal for budget_share");
      }
      budget_share = std::strtod(previous().lexeme.c_str(), nullptr);
    }
    else if (key == "capability_inherit" || key == "isolation")
    {
      if (match(TokenType::True) || match(TokenType::False))
      {
        const bool value = previous().type == TokenType::True;
        if (key == "capability_inherit")
        {
          capability_inherit = value;
        }
        else
        {
          isolation = value;
        }
      }
      else
      {
        error("Expected boolean literal for subagent config");
      }
    }
    else
    {
      error("Unknown subagent config key");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated subagent block");
  }
  return make_subagent_decl(visibility, name, std::move(base_agent), budget_share,
                            capability_inherit, isolation, span);
}

StmtPtr Parser::parse_test_decl_statement()
{
  auto attributes = parse_test_attributes();
  if (!match(TokenType::Test))
  {
    error("Expected 'test' after attributes");
  }
  SourceSpan span = span_from_token(previous());
  auto decl = parse_test_decl_node(std::move(attributes));
  return make_test_decl_stmt(std::move(*decl), span);
}

StmtPtr Parser::parse_test_suite_statement()
{
  SourceSpan span = span_from_token(previous());
  auto suite = parse_test_suite_node();
  return make_test_suite_stmt(std::move(*suite), span);
}

StmtPtr Parser::parse_with_statement()
{
  SourceSpan span = span_from_token(previous());
  auto resource = parse_expression();
  if (!match(TokenType::As))
  {
    error("Expected 'as' in with statement");
  }
  if (!match(TokenType::Identifier))
  {
    error("Expected identifier after 'as'");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after with binding");
  }
  auto body = std::make_unique<BlockStmt>(parse_block_node());
  return make_with_stmt(std::move(resource), name, std::move(body), span);
}

StmtPtr Parser::parse_assert_statement(TokenType type)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::LeftParen))
  {
    error("Expected '(' after assert");
  }

  AssertStmt assert_stmt;
  assert_stmt.right = nullptr;
  assert_stmt.exception_type = nullptr;
  switch (type)
  {
    case TokenType::AssertEq:
      assert_stmt.kind = AssertStmt::Kind::kEq;
      break;
    case TokenType::AssertNe:
      assert_stmt.kind = AssertStmt::Kind::kNe;
      break;
    case TokenType::AssertTrue:
      assert_stmt.kind = AssertStmt::Kind::kTrue;
      break;
    case TokenType::AssertFalse:
      assert_stmt.kind = AssertStmt::Kind::kFalse;
      break;
    case TokenType::AssertSome:
      assert_stmt.kind = AssertStmt::Kind::kSome;
      break;
    case TokenType::AssertNone:
      assert_stmt.kind = AssertStmt::Kind::kNone;
      break;
    case TokenType::AssertOk:
      assert_stmt.kind = AssertStmt::Kind::kOk;
      break;
    case TokenType::AssertErr:
      assert_stmt.kind = AssertStmt::Kind::kErr;
      break;
    case TokenType::AssertThrows:
      assert_stmt.kind = AssertStmt::Kind::kThrows;
      break;
    default:
      error("Unsupported assert statement");
  }

  assert_stmt.left = parse_expression();

  if (assert_stmt.kind == AssertStmt::Kind::kEq || assert_stmt.kind == AssertStmt::Kind::kNe)
  {
    if (!match(TokenType::Comma))
    {
      error("Expected ',' in assert statement");
    }
    assert_stmt.right = parse_expression();
  }
  else if (assert_stmt.kind == AssertStmt::Kind::kThrows)
  {
    if (match(TokenType::Comma))
    {
      assert_stmt.exception_type = parse_type_expression();
    }
  }

  if (!match(TokenType::RightParen))
  {
    error("Expected ')' after assert");
  }
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after assert");
  }
  return make_assert_stmt(std::move(assert_stmt), span);
}

StmtPtr Parser::parse_grant_statement()
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected capability identifier after grant");
  }
  IdentifierRef capability{previous().lexeme, span_from_token(previous())};
  if (!match(TokenType::To))
  {
    error("Expected 'to' after grant capability");
  }
  if (!match(TokenType::Identifier))
  {
    error("Expected target identifier after grant");
  }
  IdentifierRef target{previous().lexeme, span_from_token(previous())};
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after grant statement");
  }
  return make_grant_stmt(std::move(capability), std::move(target), span);
}

StmtPtr Parser::parse_checkpoint_statement()
{
  SourceSpan span = span_from_token(previous());
  std::string label;
  if (match(TokenType::String))
  {
    label = previous().lexeme;
  }
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after checkpoint statement");
  }
  return make_checkpoint_stmt(std::move(label), span);
}

StmtPtr Parser::parse_rewind_statement()
{
  SourceSpan span = span_from_token(previous());
  ExprPtr target;
  if (match(TokenType::To))
  {
    target = parse_expression();
  }
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after rewind statement");
  }
  return make_rewind_stmt(std::move(target), span);
}

StmtPtr Parser::parse_voice_pipeline(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected voice pipeline name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after voice pipeline name");
  }

  std::optional<IdentifierRef> agent_opt;
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

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Agent))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after agent");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected identifier for agent");
      }
      agent_opt = IdentifierRef{previous().lexeme, span_from_token(previous())};
    }
    else if (match(TokenType::SttProvider))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after stt_provider");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for stt_provider");
      }
      stt_provider = previous().lexeme;
    }
    else if (match(TokenType::SttModel))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after stt_model");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for stt_model");
      }
      stt_model = previous().lexeme;
    }
    else if (match(TokenType::TtsProvider))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after tts_provider");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for tts_provider");
      }
      tts_provider = previous().lexeme;
    }
    else if (match(TokenType::TtsModel))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after tts_model");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for tts_model");
      }
      tts_model = previous().lexeme;
    }
    else if (match(TokenType::TtsVoice))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after tts_voice");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for tts_voice");
      }
      tts_voice = previous().lexeme;
    }
    else if (match(TokenType::SttEndpoint))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after stt_endpoint");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for stt_endpoint");
      }
      stt_endpoint = previous().lexeme;
    }
    else if (match(TokenType::TtsEndpoint))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after tts_endpoint");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for tts_endpoint");
      }
      tts_endpoint = previous().lexeme;
    }
    else if (match(TokenType::TtsFormat))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after tts_format");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for tts_format");
      }
      tts_format = previous().lexeme;
    }
    else if (match(TokenType::TtsSpeed))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after tts_speed");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for tts_speed");
      }
      tts_speed = previous().lexeme;
    }
    else if (match(TokenType::TtsInstructions))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after tts_instructions");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for tts_instructions");
      }
      tts_instructions = previous().lexeme;
    }
    else if (match(TokenType::SttLanguage))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after stt_language");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for stt_language");
      }
      stt_language = previous().lexeme;
    }
    else if (match(TokenType::SttFormat))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after stt_format");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for stt_format");
      }
      stt_format = previous().lexeme;
    }
    else
    {
      error("Unknown voice pipeline config key");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated voice pipeline block");
  }

  if (!agent_opt)
  {
    error("Voice pipeline missing agent");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = VoicePipelineDecl{visibility, name, *agent_opt,
                                  stt_provider, stt_model,
                                  tts_provider, tts_model, tts_voice,
                                  stt_endpoint, tts_endpoint,
                                  tts_format, tts_speed, tts_instructions,
                                  stt_language, stt_format};
  return stmt;
}

StmtPtr Parser::parse_realtime_voice(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected realtime voice agent name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after realtime_voice name");
  }

  std::optional<IdentifierRef> agent_opt;
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

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Agent))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after agent");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected identifier for agent");
      }
      agent_opt = IdentifierRef{previous().lexeme, span_from_token(previous())};
    }
    else if (match(TokenType::RtProvider))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after rt_provider");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for rt_provider");
      }
      provider = previous().lexeme;
    }
    else if (match(TokenType::RtModel))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after rt_model");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for rt_model");
      }
      model = previous().lexeme;
    }
    else if (match(TokenType::RtVoice))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after rt_voice");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for rt_voice");
      }
      voice = previous().lexeme;
    }
    else if (match(TokenType::RtVad))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after rt_vad");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for rt_vad");
      }
      vad = previous().lexeme;
    }
    else if (match(TokenType::RtVadThreshold))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after vad_threshold");
      }
      if (!match(TokenType::String) && !match(TokenType::Number))
      {
        error("Expected value for vad_threshold");
      }
      vad_threshold = previous().lexeme;
    }
    else if (match(TokenType::RtSilenceDurationMs))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after silence_duration_ms");
      }
      if (!match(TokenType::String) && !match(TokenType::Number))
      {
        error("Expected value for silence_duration_ms");
      }
      silence_duration_ms = previous().lexeme;
    }
    else if (match(TokenType::RtInputFormat))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after input_format");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for input_format");
      }
      input_format = previous().lexeme;
    }
    else if (match(TokenType::RtOutputFormat))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after output_format");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for output_format");
      }
      output_format = previous().lexeme;
    }
    else if (match(TokenType::RtSampleRate))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after sample_rate");
      }
      if (!match(TokenType::String) && !match(TokenType::Number))
      {
        error("Expected value for sample_rate");
      }
      sample_rate = previous().lexeme;
    }
    else if (match(TokenType::RtSpeed))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after rt_speed");
      }
      if (!match(TokenType::String) && !match(TokenType::Number))
      {
        error("Expected value for rt_speed");
      }
      speed = previous().lexeme;
    }
    else if (match(TokenType::RtSttEndpoint))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after rt_stt_endpoint");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for rt_stt_endpoint");
      }
      stt_endpoint = previous().lexeme;
    }
    else if (match(TokenType::RtTtsEndpoint))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after rt_tts_endpoint");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for rt_tts_endpoint");
      }
      tts_endpoint = previous().lexeme;
    }
    else if (match(TokenType::RtLlmEndpoint))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after rt_llm_endpoint");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for rt_llm_endpoint");
      }
      llm_endpoint = previous().lexeme;
    }
    else
    {
      error("Unknown realtime_voice config key");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated realtime_voice block");
  }

  if (!agent_opt)
  {
    error("realtime_voice missing agent");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = RealtimeVoiceDecl{
      visibility, name, *agent_opt,
      provider, model, voice, vad,
      vad_threshold, silence_duration_ms,
      input_format, output_format,
      sample_rate, speed,
      stt_endpoint, tts_endpoint, llm_endpoint};
  return stmt;
}

StmtPtr Parser::parse_try_catch()
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after 'try'");
  }
  auto try_body = parse_block();

  if (!match(TokenType::Catch))
  {
    error("Expected 'catch' after try block");
  }
  if (!match(TokenType::LeftParen))
  {
    error("Expected '(' after 'catch'");
  }
  if (!match(TokenType::Identifier))
  {
    error("Expected identifier in catch clause");
  }
  std::string catch_var = previous().lexeme;
  if (!match(TokenType::RightParen))
  {
    error("Expected ')' after catch variable");
  }
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after catch clause");
  }
  auto catch_body = parse_block();

  return make_try_catch_stmt(std::move(try_body), std::move(catch_var), std::move(catch_body), span);
}

StmtPtr Parser::parse_throw()
{
  SourceSpan span = span_from_token(previous());
  auto value = parse_expression();
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after throw expression");
  }
  return make_throw_stmt(std::move(value), span);
}

TestAttribute Parser::parse_test_attribute()
{
  if (!match(TokenType::LeftBracket))
  {
    error("Expected '[' after '#'");
  }
  TestAttribute attribute;
  if (match(TokenType::Ignore))
  {
    attribute.kind = TestAttribute::Kind::kIgnore;
  }
  else if (match(TokenType::Async))
  {
    attribute.kind = TestAttribute::Kind::kAsync;
  }
  else if (match(TokenType::ShouldPanic))
  {
    attribute.kind = TestAttribute::Kind::kShouldPanic;
    if (match(TokenType::LeftParen))
    {
      if (!match(TokenType::String))
      {
        error("Expected panic message string");
      }
      attribute.panic_message = previous().lexeme;
      if (!match(TokenType::RightParen))
      {
        error("Expected ')' after panic message");
      }
    }
  }
  else if (match(TokenType::Timeout))
  {
    attribute.kind = TestAttribute::Kind::kTimeout;
    if (!match(TokenType::LeftParen))
    {
      error("Expected '(' after timeout");
    }
    if (!match(TokenType::Number))
    {
      error("Expected timeout milliseconds");
    }
    attribute.timeout_ms = static_cast<int64_t>(std::strtod(previous().lexeme.c_str(), nullptr));
    if (!match(TokenType::RightParen))
    {
      error("Expected ')' after timeout");
    }
  }
  else
  {
    error("Unknown test attribute");
  }

  if (!match(TokenType::RightBracket))
  {
    error("Expected ']' after test attribute");
  }
  return attribute;
}

std::vector<TestAttribute> Parser::parse_test_attributes()
{
  std::vector<TestAttribute> attributes;
  while (match(TokenType::Hash))
  {
    attributes.push_back(parse_test_attribute());
  }
  return attributes;
}

std::unique_ptr<TestDecl> Parser::parse_test_decl_node(std::vector<TestAttribute> attributes)
{
  if (!match(TokenType::String))
  {
    error("Expected string literal for test name");
  }
  auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' to start test body");
  }
  auto body = std::make_unique<BlockStmt>(parse_block_node());
  auto decl = std::make_unique<TestDecl>();
  decl->name = std::move(name);
  decl->attributes = std::move(attributes);
  decl->body = std::move(body);
  return decl;
}

std::unique_ptr<TestSuiteDecl> Parser::parse_test_suite_node()
{
  if (!match(TokenType::String))
  {
    error("Expected string literal for suite name");
  }
  auto suite = std::make_unique<TestSuiteDecl>();
  suite->name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' to start suite");
  }
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::BeforeAll))
    {
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' after before_all");
      }
      suite->before_all = std::make_unique<BlockStmt>(parse_block_node());
      continue;
    }
    if (match(TokenType::AfterAll))
    {
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' after after_all");
      }
      suite->after_all = std::make_unique<BlockStmt>(parse_block_node());
      continue;
    }
    if (match(TokenType::BeforeEach))
    {
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' after before_each");
      }
      suite->before_each = std::make_unique<BlockStmt>(parse_block_node());
      continue;
    }
    if (match(TokenType::AfterEach))
    {
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' after after_each");
      }
      suite->after_each = std::make_unique<BlockStmt>(parse_block_node());
      continue;
    }

    if (check(TokenType::Hash) || check(TokenType::Test))
    {
      auto attributes = parse_test_attributes();
      if (!match(TokenType::Test))
      {
        error("Expected 'test' in suite");
      }
      suite->tests.push_back(parse_test_decl_node(std::move(attributes)));
      continue;
    }

    if (match(TokenType::Suite))
    {
      suite->nested_suites.push_back(parse_test_suite_node());
      continue;
    }

    error("Unexpected token in test suite");
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated test suite");
  }
  return suite;
}

RetrievalStrategy parse_strategy_name(const std::string& name)
{
  if (name == "basic" || name == "similarity") return RetrievalStrategy::kBasic;
  if (name == "mmr") return RetrievalStrategy::kMMR;
  if (name == "hybrid") return RetrievalStrategy::kHybrid;
  if (name == "hyde") return RetrievalStrategy::kHyDE;
  if (name == "self_rag" || name == "self-rag") return RetrievalStrategy::kSelfRAG;
  if (name == "crag" || name == "corrective") return RetrievalStrategy::kCRAG;
  if (name == "agentic") return RetrievalStrategy::kAgentic;
  if (name == "graph" || name == "graph_rag") return RetrievalStrategy::kGraphRAG;
  return RetrievalStrategy::kBasic;
}

StmtPtr Parser::parse_knowledge(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected knowledge name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after knowledge name");
  }

  std::optional<std::string> vector_store;
  std::optional<std::string> embedding_model;
  std::optional<std::size_t> chunk_size;
  std::optional<std::size_t> chunk_overlap;
  std::vector<KnowledgeSource> sources;
  RetrievalStrategy retrieval_strategy = RetrievalStrategy::kBasic;
  RetrievalStrategyOptions strategy_options;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    // Skip optional commas between knowledge block fields
    while (match(TokenType::Comma)) {}
    if (check(TokenType::RightBrace))
    {
      break;
    }
    if (match(TokenType::VectorStore))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after vector_store");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for vector_store");
      }
      vector_store = previous().lexeme;
      continue;
    }
    if (match(TokenType::EmbeddingModel))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after embedding_model");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for embedding_model");
      }
      embedding_model = previous().lexeme;
      continue;
    }
    if (match(TokenType::ChunkSize))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after chunk_size");
      }
      if (!match(TokenType::Number))
      {
        error("Expected number literal for chunk_size");
      }
      chunk_size = static_cast<std::size_t>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match(TokenType::ChunkOverlap))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after chunk_overlap");
      }
      if (!match(TokenType::Number))
      {
        error("Expected number literal for chunk_overlap");
      }
      chunk_overlap = static_cast<std::size_t>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match(TokenType::Sources))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after sources");
      }
      sources = parse_knowledge_sources();
      continue;
    }
    // Parse retrieval_strategy
    if (match(TokenType::RetrievalStrategy))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after retrieval_strategy");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for retrieval_strategy");
      }
      retrieval_strategy = parse_strategy_name(previous().lexeme);
      continue;
    }
    // Parse strategy options
    if (match(TokenType::TopK))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after top_k");
      }
      if (!match(TokenType::Number))
      {
        error("Expected number literal for top_k");
      }
      strategy_options.top_k = static_cast<std::size_t>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match(TokenType::RelevanceThreshold))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after relevance_threshold");
      }
      if (!match(TokenType::Number))
      {
        error("Expected number literal for relevance_threshold");
      }
      strategy_options.relevance_threshold = std::strtod(previous().lexeme.c_str(), nullptr);
      continue;
    }
    if (match(TokenType::MmrLambda))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after mmr_lambda");
      }
      if (!match(TokenType::Number))
      {
        error("Expected number literal for mmr_lambda");
      }
      strategy_options.mmr_lambda = std::strtod(previous().lexeme.c_str(), nullptr);
      continue;
    }
    if (match(TokenType::NumHypothetical))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after num_hypothetical");
      }
      if (!match(TokenType::Number))
      {
        error("Expected number literal for num_hypothetical");
      }
      strategy_options.num_hypothetical = static_cast<std::size_t>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match(TokenType::EnableRelevanceCheck))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after enable_relevance_check");
      }
      if (match(TokenType::True))
      {
        strategy_options.enable_relevance_check = true;
      }
      else if (match(TokenType::False))
      {
        strategy_options.enable_relevance_check = false;
      }
      else
      {
        error("Expected boolean for enable_relevance_check");
      }
      continue;
    }
    if (match(TokenType::EnableSupportCheck))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after enable_support_check");
      }
      if (match(TokenType::True))
      {
        strategy_options.enable_support_check = true;
      }
      else if (match(TokenType::False))
      {
        strategy_options.enable_support_check = false;
      }
      else
      {
        error("Expected boolean for enable_support_check");
      }
      continue;
    }
    if (match(TokenType::EnableWebFallback))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after enable_web_fallback");
      }
      if (match(TokenType::True))
      {
        strategy_options.enable_web_fallback = true;
      }
      else if (match(TokenType::False))
      {
        strategy_options.enable_web_fallback = false;
      }
      else
      {
        error("Expected boolean for enable_web_fallback");
      }
      continue;
    }
    if (match(TokenType::EnableQueryDecomposition))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after enable_query_decomposition");
      }
      if (match(TokenType::True))
      {
        strategy_options.enable_query_decomposition = true;
      }
      else if (match(TokenType::False))
      {
        strategy_options.enable_query_decomposition = false;
      }
      else
      {
        error("Expected boolean for enable_query_decomposition");
      }
      continue;
    }
    if (match(TokenType::MaxCorrections))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after max_corrections");
      }
      if (!match(TokenType::Number))
      {
        error("Expected number literal for max_corrections");
      }
      strategy_options.max_corrections = static_cast<std::size_t>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match(TokenType::MaxIterations))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after max_iterations");
      }
      if (!match(TokenType::Number))
      {
        error("Expected number literal for max_iterations");
      }
      strategy_options.max_iterations = static_cast<std::size_t>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match(TokenType::EnableReflection))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after enable_reflection");
      }
      if (match(TokenType::True))
      {
        strategy_options.enable_reflection = true;
      }
      else if (match(TokenType::False))
      {
        strategy_options.enable_reflection = false;
      }
      else
      {
        error("Expected boolean for enable_reflection");
      }
      continue;
    }
    if (match(TokenType::SearchDepth))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after search_depth");
      }
      if (!match(TokenType::Number))
      {
        error("Expected number literal for search_depth");
      }
      strategy_options.search_depth = static_cast<std::size_t>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match(TokenType::IncludeCommunities))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after include_communities");
      }
      if (match(TokenType::True))
      {
        strategy_options.include_communities = true;
      }
      else if (match(TokenType::False))
      {
        strategy_options.include_communities = false;
      }
      else
      {
        error("Expected boolean for include_communities");
      }
      continue;
    }
    error("Unexpected token in knowledge block");
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated knowledge block");
  }
  if (!vector_store.has_value())
  {
    error("Knowledge missing vector_store");
  }
  if (!embedding_model.has_value())
  {
    error("Knowledge missing embedding_model");
  }
  if (!chunk_size.has_value())
  {
    error("Knowledge missing chunk_size");
  }
  if (!chunk_overlap.has_value())
  {
    error("Knowledge missing chunk_overlap");
  }
  if (sources.empty())
  {
    error("Knowledge missing sources");
  }
  return make_knowledge_decl(visibility, name, *vector_store, *embedding_model, *chunk_size,
                             *chunk_overlap, std::move(sources), retrieval_strategy,
                             std::move(strategy_options), span);
}

SkillParam Parser::parse_skill_param()
{
  if (!match(TokenType::Identifier))
  {
    error("Expected param name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::Colon))
  {
    error("Expected ':' after param name");
  }
  if (!match(TokenType::Identifier))
  {
    error("Expected type name for param");
  }
  const auto type = previous().lexeme;
  auto schema_type = type;
  if (type == "bool")
  {
    schema_type = "boolean";
  }
  else if (type == "map")
  {
    schema_type = "object";
  }
  else if (type == "list")
  {
    schema_type = "array";
  }
  nlohmann::json schema;
  schema["type"] = schema_type;

  if (match(TokenType::LeftParen))
  {
    bool first = true;
    while (!check(TokenType::RightParen) && !is_at_end())
    {
      if (!first)
      {
        if (!match(TokenType::Comma))
        {
          error("Expected ',' between param annotations");
        }
      }
      first = false;
      if (!match(TokenType::Identifier))
      {
        error("Expected annotation key");
      }
      const auto key = previous().lexeme;
      if (!match(TokenType::Equal))
      {
        error("Expected '=' after annotation key");
      }
      if (key == "enum")
      {
        if (!match(TokenType::LeftBracket))
        {
          error("Expected '[' after enum=");
        }
        nlohmann::json values = nlohmann::json::array();
        if (!check(TokenType::RightBracket))
        {
          do
          {
            if (!match(TokenType::String))
            {
              error("Expected string literal in enum list");
            }
            values.push_back(previous().lexeme);
          } while (match(TokenType::Comma));
        }
        if (!match(TokenType::RightBracket))
        {
          error("Expected ']' after enum list");
        }
        schema["enum"] = std::move(values);
      }
      else if (key == "sensitive")
      {
        if (match(TokenType::True))
        {
          schema["sensitive"] = true;
        }
        else if (match(TokenType::False))
        {
          schema["sensitive"] = false;
        }
        else
        {
          error("Expected true or false for sensitive");
        }
      }
      else
      {
        error("Unsupported param annotation");
      }
    }
    if (!match(TokenType::RightParen))
    {
      error("Expected ')' after param annotations");
    }
  }

  return SkillParam{name, std::move(schema)};
}

ToolParam Parser::parse_tool_param()
{
  if (!match(TokenType::Identifier))
  {
    error("Expected parameter name");
  }
  ToolParam param;
  param.name = previous().lexeme;
  if (!match(TokenType::Colon))
  {
    error("Expected ':' after parameter name");
  }
  param.type_expr = parse_type_expression();
  if (match(TokenType::Equal))
  {
    param.default_value = parse_expression();
    param.has_default = true;
  }
  return param;
}

BudgetCost Parser::parse_budget_cost()
{
  if (!match(TokenType::Identifier))
  {
    error("Expected budget cost key");
  }
  BudgetCost cost;
  cost.resource = previous().lexeme;
  if (!match(TokenType::Colon))
  {
    error("Expected ':' after budget cost key");
  }
  if (!match(TokenType::Number))
  {
    error("Expected number literal for budget cost");
  }
  cost.amount = std::strtod(previous().lexeme.c_str(), nullptr);
  return cost;
}

std::unique_ptr<GuardHandler> Parser::parse_guard_handler()
{
  auto handler = std::make_unique<GuardHandler>();
  switch (previous().type)
  {
    case TokenType::OnObservation:
      handler->type = GuardHandler::Type::kOnObservation;
      break;
    case TokenType::OnAction:
      handler->type = GuardHandler::Type::kOnAction;
      break;
    case TokenType::OnToolInput:
      handler->type = GuardHandler::Type::kOnToolInput;
      break;
    case TokenType::OnToolOutput:
      handler->type = GuardHandler::Type::kOnToolOutput;
      break;
    case TokenType::OnToolCall:
      handler->type = GuardHandler::Type::kOnToolCall;
      break;
    case TokenType::OnResult:
      handler->type = GuardHandler::Type::kOnResult;
      break;
    default:
      error("Unknown guard handler type");
  }

  if (!match(TokenType::LeftParen))
  {
    error("Expected '(' after guard handler type");
  }
  if (!check(TokenType::RightParen))
  {
    do
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected parameter name in guard handler");
      }
      handler->parameters.push_back(previous().lexeme);
    } while (match(TokenType::Comma));
  }
  if (!match(TokenType::RightParen))
  {
    error("Expected ')' after guard handler params");
  }

  if (match(TokenType::Minus))
  {
    if (!match(TokenType::Greater))
    {
      error("Expected '>' after '-' in guard handler return type");
    }
    handler->return_type = parse_type_expression();
  }

  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' to start guard handler body");
  }
  handler->body = std::make_unique<BlockStmt>(parse_block_node());
  return handler;
}

std::vector<SkillParam> Parser::parse_skill_params()
{
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' to start params");
  }
  std::vector<SkillParam> params;
  if (!check(TokenType::RightBrace))
  {
    do
    {
      params.push_back(parse_skill_param());
    } while (match(TokenType::Comma));
  }
  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' after params");
  }
  return params;
}

std::vector<IdentifierRef> Parser::parse_identifier_list()
{
  if (!match(TokenType::LeftBracket))
  {
    error("Expected '[' to start list");
  }
  std::vector<IdentifierRef> values;
  if (!check(TokenType::RightBracket))
  {
    do
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected identifier in list");
      }
      values.push_back(IdentifierRef{previous().lexeme, span_from_token(previous())});
    } while (match(TokenType::Comma));
  }
  if (!match(TokenType::RightBracket))
  {
    error("Expected ']' after list");
  }
  return values;
}

BudgetDimension Parser::parse_budget_dimension()
{
  if (!match(TokenType::Identifier))
  {
    error("Expected budget dimension name");
  }
  BudgetDimension dimension;
  dimension.name = previous().lexeme;
  if (!match(TokenType::Colon))
  {
    error("Expected ':' after budget dimension name");
  }

  if (match(TokenType::Dollar))
  {
    if (!match(TokenType::Number))
    {
      error("Expected number after '$' in budget value");
    }
    dimension.value = std::strtod(previous().lexeme.c_str(), nullptr);
    dimension.unit = "$";
    return dimension;
  }

  if (!match(TokenType::Number))
  {
    error("Expected number for budget value");
  }
  const auto number_token = previous();
  dimension.value = std::strtod(number_token.lexeme.c_str(), nullptr);
  if (check(TokenType::Identifier) && peek().line == number_token.line)
  {
    advance();
    const auto unit = previous().lexeme;
    if (unit == "s" || unit == "ms" || unit == "min" || unit == "h")
    {
      dimension.unit = unit;
    }
    else
    {
      error("Unknown budget unit");
    }
  }
  return dimension;
}

Visibility Parser::parse_visibility()
{
  if (!match(TokenType::Pub))
  {
    error("Expected visibility modifier");
  }
  Visibility visibility;
  visibility.span = span_from_token(previous());
  visibility.level = Visibility::Level::kPublic;

  if (match(TokenType::LeftParen))
  {
    if (match(TokenType::Crate))
    {
      visibility.level = Visibility::Level::kCrate;
    }
    else if (match(TokenType::Super))
    {
      visibility.level = Visibility::Level::kSuper;
    }
    else
    {
      error("Expected 'crate' or 'super' in visibility modifier");
    }
    if (!match(TokenType::RightParen))
    {
      error("Expected ')' after visibility modifier");
    }
  }
  return visibility;
}

std::vector<std::string> Parser::parse_module_path()
{
  std::vector<std::string> path;
  if (!match(TokenType::Identifier))
  {
    error("Expected module path");
  }
  path.push_back(previous().lexeme);
  while (check(TokenType::Dot))
  {
    if (current_ + 1 < tokens_.size())
    {
      const auto next_type = tokens_[current_ + 1].type;
      if (next_type == TokenType::Star || next_type == TokenType::LeftBrace)
      {
        break;
      }
    }
    advance();
    if (!match(TokenType::Identifier))
    {
      error("Expected identifier in module path");
    }
    path.push_back(previous().lexeme);
  }
  return path;
}

std::vector<std::string> Parser::parse_import_items()
{
  std::vector<std::string> items;
  if (!check(TokenType::RightBrace))
  {
    do
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected identifier in import list");
      }
      items.push_back(previous().lexeme);
    } while (match(TokenType::Comma));
  }
  return items;
}

std::vector<KnowledgeSource> Parser::parse_knowledge_sources()
{
  if (!match(TokenType::LeftBracket))
  {
    error("Expected '[' to start sources list");
  }
  std::vector<KnowledgeSource> sources;
  if (!check(TokenType::RightBracket))
  {
    do
    {
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' to start source");
      }
      std::optional<std::string> type;
      std::optional<std::string> path;
      std::optional<std::string> content;
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        std::string key;
        if (match(TokenType::Identifier))
        {
          key = previous().lexeme;
        }
        else if (match(TokenType::Type))
        {
          key = previous().lexeme;
        }
        else if (match(TokenType::String))
        {
          key = previous().lexeme;
        }
        else
        {
          error("Expected key in source entry");
        }
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after source key");
        }
        if (key == "type")
        {
          if (!match(TokenType::String))
          {
            error("Expected string literal for source type");
          }
          type = previous().lexeme;
        }
        else if (key == "path")
        {
          if (!match(TokenType::String))
          {
            error("Expected string literal for source path");
          }
          path = previous().lexeme;
        }
        else if (key == "content")
        {
          if (!match(TokenType::String))
          {
            error("Expected string literal for source content");
          }
          content = previous().lexeme;
        }
        else
        {
          error("Unexpected key in source entry");
        }
        if (!match(TokenType::Comma))
        {
          break;
        }
      }
      if (!match(TokenType::RightBrace))
      {
        error("Expected '}' after source entry");
      }
      if (!type.has_value() || (!path.has_value() && !content.has_value()))
      {
        error("Source entry requires type and either path or content");
      }
      KnowledgeSource ks;
      ks.type = std::move(*type);
      if (path.has_value())
      {
        ks.path = std::move(*path);
      }
      if (content.has_value())
      {
        ks.content = std::move(*content);
      }
      sources.push_back(std::move(ks));
    } while (match(TokenType::Comma));
  }
  if (!match(TokenType::RightBracket))
  {
    error("Expected ']' after sources list");
  }
  return sources;
}

StmtPtr Parser::parse_let()
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected identifier after 'let'");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::Equal))
  {
    error("Expected '=' after identifier");
  }
  auto initializer = parse_expression();
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after let initializer");
  }
  span = merge_span(span, initializer->span);
  return make_let_stmt(name, std::move(initializer), span);
}

StmtPtr Parser::parse_block()
{
  SourceSpan span = span_from_token(previous());
  auto block = parse_block_node();
  return make_block_stmt(std::move(block.statements), span);
}

BlockStmt Parser::parse_block_node()
{
  std::vector<StmtPtr> statements;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    statements.push_back(parse_declaration());
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated block");
  }
  return BlockStmt{std::move(statements)};
}

StmtPtr Parser::parse_if()
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::LeftParen))
  {
    error("Expected '(' after 'if'");
  }
  auto condition = parse_expression();
  if (!match(TokenType::RightParen))
  {
    error("Expected ')' after condition");
  }
  auto then_branch = parse_statement();
  StmtPtr else_branch;
  if (match(TokenType::Else))
  {
    else_branch = parse_statement();
  }
  span = merge_span(span, condition->span);
  return make_if_stmt(std::move(condition), std::move(then_branch), std::move(else_branch), span);
}

StmtPtr Parser::parse_while()
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::LeftParen))
  {
    error("Expected '(' after 'while'");
  }
  auto condition = parse_expression();
  if (!match(TokenType::RightParen))
  {
    error("Expected ')' after condition");
  }
  auto body = parse_statement();
  span = merge_span(span, condition->span);
  return make_while_stmt(std::move(condition), std::move(body), span);
}

ExprPtr Parser::parse_expression()
{
  return parse_assignment();
}

ExprPtr Parser::parse_assignment()
{
  auto expr = parse_equality();
  if (match(TokenType::Equal))
  {
    auto value = parse_assignment();
    if (auto ident = std::get_if<IdentifierExpr>(&expr->node))
    {
      auto span = merge_span(expr->span, value->span);
      return make_assignment(ident->name, std::move(value), span);
    }
    error("Invalid assignment target");
  }
  return expr;
}

ExprPtr Parser::parse_equality()
{
  auto expr = parse_comparison();
  while (match(TokenType::BangEqual) || match(TokenType::EqualEqual))
  {
    const auto op_token = previous().type;
    auto right = parse_comparison();
    auto span = merge_span(expr->span, right->span);
    expr = make_binary(op_token == TokenType::EqualEqual ? BinaryOp::Equal : BinaryOp::NotEqual,
                       std::move(expr), std::move(right), span);
  }
  return expr;
}

ExprPtr Parser::parse_comparison()
{
  auto expr = parse_term();
  while (match(TokenType::Greater) || match(TokenType::GreaterEqual) || match(TokenType::Less) ||
         match(TokenType::LessEqual))
  {
    const auto op_token = previous().type;
    auto right = parse_term();
    BinaryOp op = BinaryOp::Greater;
    switch (op_token)
    {
      case TokenType::Greater:
        op = BinaryOp::Greater;
        break;
      case TokenType::GreaterEqual:
        op = BinaryOp::GreaterEqual;
        break;
      case TokenType::Less:
        op = BinaryOp::Less;
        break;
      case TokenType::LessEqual:
        op = BinaryOp::LessEqual;
        break;
      default:
        break;
    }
    auto span = merge_span(expr->span, right->span);
    expr = make_binary(op, std::move(expr), std::move(right), span);
  }
  return expr;
}

ExprPtr Parser::parse_term()
{
  auto expr = parse_factor();
  while (match(TokenType::Plus) || match(TokenType::Minus))
  {
    const auto op_token = previous().type;
    auto right = parse_factor();
    auto span = merge_span(expr->span, right->span);
    expr = make_binary(op_token == TokenType::Plus ? BinaryOp::Add : BinaryOp::Subtract, std::move(expr),
                       std::move(right), span);
  }
  return expr;
}

ExprPtr Parser::parse_factor()
{
  auto expr = parse_unary();
  while (match(TokenType::Star) || match(TokenType::Slash) || match(TokenType::Percent))
  {
    const auto op_token = previous().type;
    auto right = parse_unary();
    auto span = merge_span(expr->span, right->span);
    BinaryOp op = BinaryOp::Modulo;
    if (op_token == TokenType::Star)
    {
      op = BinaryOp::Multiply;
    }
    else if (op_token == TokenType::Slash)
    {
      op = BinaryOp::Divide;
    }
    expr = make_binary(op, std::move(expr), std::move(right), span);
  }
  return expr;
}

ExprPtr Parser::parse_unary()
{
  if (match(TokenType::Bang))
  {
    const Token op_token = previous();
    auto operand = parse_unary();
    auto span = merge_span(span_from_token(op_token), operand->span);
    return make_unary(UnaryOp::Not, std::move(operand), span);
  }
  if (match(TokenType::Minus))
  {
    const Token op_token = previous();
    auto operand = parse_unary();
    auto span = merge_span(span_from_token(op_token), operand->span);
    return make_unary(UnaryOp::Negate, std::move(operand), span);
  }
  return parse_call();
}

ExprPtr Parser::parse_call()
{
  auto expr = parse_primary();
  while (true)
  {
    if (match(TokenType::LeftParen))
    {
      std::vector<ExprPtr> args;
      if (!check(TokenType::RightParen))
      {
        do
        {
          args.push_back(parse_expression());
        } while (match(TokenType::Comma));
      }
      if (!match(TokenType::RightParen))
      {
        error("Expected ')' after arguments");
      }
      auto span = merge_span(expr->span, span_from_token(previous()));
      bool handled = false;
      if (const auto* get_expr = std::get_if<GetExpr>(&expr->node))
      {
        if (const auto* ident = std::get_if<IdentifierExpr>(&get_expr->object->node))
        {
          if (ident->name == "File" && (get_expr->name == "open" || get_expr->name == "create"))
          {
            if (args.empty())
            {
              error("File open requires a path");
            }
            if (args.size() > 2)
            {
              error("File open accepts at most two arguments");
            }
            auto file_expr = std::make_unique<Expression>();
            file_expr->span = span;
            std::string mode;
            if (args.size() == 2)
            {
              if (const auto* mode_ident = std::get_if<IdentifierExpr>(&args[1]->node))
              {
                mode = mode_ident->name;
              }
              else if (const auto* mode_literal = std::get_if<LiteralExpr>(&args[1]->node))
              {
                if (!mode_literal->value.is_string())
                {
                  error("File mode must be an identifier or string");
                }
                auto* str = vm::as_string(mode_literal->value);
                mode = std::string(str->chars, str->length);
              }
              else
              {
                error("File mode must be an identifier or string");
              }
            }
            else
            {
              mode = get_expr->name == "create" ? "Create" : "Read";
            }
            file_expr->node = FileOpenExpr{std::move(args[0]), std::move(mode)};
            expr = std::move(file_expr);
            handled = true;
          }
        }
      }
      if (!handled)
      {
        if (auto* get_expr = std::get_if<GetExpr>(&expr->node))
        {
          if (get_expr->name == "context")
          {
            if (args.size() != 1)
            {
              error("context expects a single message argument");
            }
            expr =
                make_context_expr(std::move(get_expr->object), std::move(args[0]), span);
            handled = true;
          }
          else if (get_expr->name == "with_context")
          {
            if (args.size() != 2)
            {
              error("with_context expects a key and value");
            }
            const auto key = extract_string_key(*args[0]);
            if (!key.has_value())
            {
              error("with_context key must be a string literal or identifier");
            }
            expr = make_with_context_expr(std::move(get_expr->object), *key, std::move(args[1]),
                                          span);
            handled = true;
          }
        }
      }
      if (!handled)
      {
        expr = make_call(std::move(expr), std::move(args), span);
      }
      continue;
    }
    if (match(TokenType::Dot))
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected property name after '.'");
      }
      auto span = merge_span(expr->span, span_from_token(previous()));
      expr = make_get(std::move(expr), previous().lexeme, span);
      continue;
    }
    if (match(TokenType::LeftBracket))
    {
      auto index = parse_expression();
      if (!match(TokenType::RightBracket))
      {
        error("Expected ']' after index expression");
      }
      auto span = merge_span(expr->span, span_from_token(previous()));
      expr = make_index(std::move(expr), std::move(index), span);
      continue;
    }
    if (match(TokenType::Question))
    {
      auto span = merge_span(expr->span, span_from_token(previous()));
      expr = make_try_expr(std::move(expr), span);
      continue;
    }
    break;
  }
  return expr;
}

ExprPtr Parser::parse_primary()
{
  if (match(TokenType::Panic))
  {
    SourceSpan span = span_from_token(previous());
    if (!match(TokenType::Bang))
    {
      error("Expected '!' after panic");
    }
    if (!match(TokenType::LeftParen))
    {
      error("Expected '(' after panic!");
    }
    auto message = parse_expression();
    if (!match(TokenType::RightParen))
    {
      error("Expected ')' after panic message");
    }
    span = merge_span(span, span_from_token(previous()));
    return make_panic_expr(std::move(message), span);
  }
  if (match(TokenType::CatchPanic))
  {
    SourceSpan span = span_from_token(previous());
    if (!match(TokenType::LeftParen))
    {
      error("Expected '(' after catch_panic");
    }
    auto closure = parse_expression();
    if (!match(TokenType::RightParen))
    {
      error("Expected ')' after catch_panic");
    }
    span = merge_span(span, span_from_token(previous()));
    return make_catch_panic_expr(std::move(closure), span);
  }
  if (match(TokenType::Number))
  {
    const double value = std::strtod(previous().lexeme.c_str(), nullptr);
    return make_literal(vm::Value::Number(value), span_from_token(previous()));
  }
  if (match(TokenType::String))
  {
    return make_literal(
        vm::Value::String(previous().lexeme.c_str(), previous().lexeme.size()),
        span_from_token(previous()));
  }
  if (match(TokenType::PathLiteral))
  {
    auto expr = std::make_unique<Expression>();
    expr->span = span_from_token(previous());
    expr->node = PathLiteralExpr{previous().lexeme};
    return expr;
  }
  if (match(TokenType::True))
  {
    return make_literal(vm::Value::Bool(true), span_from_token(previous()));
  }
  if (match(TokenType::False))
  {
    return make_literal(vm::Value::Bool(false), span_from_token(previous()));
  }
  if (match(TokenType::Nil))
  {
    return make_literal(vm::Value::Nil(), span_from_token(previous()));
  }
  if (match(TokenType::Identifier))
  {
    return make_identifier(previous().lexeme, span_from_token(previous()));
  }
  if (match(TokenType::LeftBracket))
  {
    SourceSpan span = span_from_token(previous());
    std::vector<ExprPtr> elements;
    if (!check(TokenType::RightBracket))
    {
      while (true)
      {
        elements.push_back(parse_expression());
        if (!match(TokenType::Comma))
        {
          break;
        }
        if (check(TokenType::RightBracket))
        {
          break;
        }
      }
    }
    if (!match(TokenType::RightBracket))
    {
      error("Expected ']' after list literal");
    }
    span = merge_span(span, span_from_token(previous()));
    return make_list(std::move(elements), span);
  }
  if (match(TokenType::LeftBrace))
  {
    SourceSpan span = span_from_token(previous());
    std::vector<MapEntry> entries;
    if (!check(TokenType::RightBrace))
    {
      while (true)
      {
        auto key = parse_expression();
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after map key");
        }
        auto value = parse_expression();
        entries.push_back(MapEntry{std::move(key), std::move(value)});
        if (!match(TokenType::Comma))
        {
          break;
        }
        if (check(TokenType::RightBrace))
        {
          break;
        }
      }
    }
    if (!match(TokenType::RightBrace))
    {
      error("Expected '}' after map literal");
    }
    span = merge_span(span, span_from_token(previous()));
    return make_map(std::move(entries), span);
  }
  if (match(TokenType::LeftParen))
  {
    auto expr = parse_expression();
    if (!match(TokenType::RightParen))
    {
      error("Expected ')' after expression");
    }
    return expr;
  }
  error("Unexpected token");
}

std::unique_ptr<TypeExpression> Parser::parse_type_expression()
{
  if (!match(TokenType::Identifier))
  {
    error("Expected type name");
  }
  auto type = std::make_unique<TypeExpression>();
  type->name = previous().lexeme;
  return type;
}

Program Parser::parse()
{
  Program program;
  while (!is_at_end())
  {
    program.statements.push_back(parse_declaration());
  }
  return program;
}

// ============================================================================
// Agentic Orchestration Parsing Methods
// ============================================================================

namespace
{
StmtPtr make_handoff_decl(Visibility visibility, std::string name,
                          std::vector<HandoffTarget> targets,
                          std::optional<ExprPtr> on_handoff_callback,
                          SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = HandoffDecl{std::move(visibility), std::move(name),
                           std::move(targets), std::move(on_handoff_callback)};
  return stmt;
}

StmtPtr make_agent_card_decl(Visibility visibility, std::string name,
                             std::string version, std::string description,
                             std::vector<std::string> capabilities,
                             std::vector<AgentCardSchema> input_schema,
                             std::vector<AgentCardSchema> output_schema,
                             std::optional<std::string> endpoint_url,
                             std::optional<std::string> authentication,
                             SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = AgentCardDecl{std::move(visibility), std::move(name),
                             std::move(version), std::move(description),
                             std::move(capabilities), std::move(input_schema),
                             std::move(output_schema), std::move(endpoint_url),
                             std::move(authentication)};
  return stmt;
}

StmtPtr make_task_decl(Visibility visibility, std::string name,
                       std::optional<IdentifierRef> target_agent,
                       std::vector<AgentCardSchema> input_schema,
                       std::optional<ExprPtr> timeout_ms,
                       std::optional<ExprPtr> on_status_change,
                       SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = TaskDecl{std::move(visibility), std::move(name),
                        std::move(target_agent), std::move(input_schema),
                        std::move(timeout_ms), std::move(on_status_change)};
  return stmt;
}

StmtPtr make_runner_decl(Visibility visibility, std::string name,
                         IdentifierRef entry_agent,
                         std::optional<std::size_t> max_turns,
                         std::optional<IdentifierRef> tracing,
                         std::vector<IdentifierRef> guardrails,
                         std::vector<IdentifierRef> input_guardrails,
                         std::vector<IdentifierRef> output_guardrails,
                         std::optional<ExprPtr> on_turn,
                         std::optional<ExprPtr> on_complete,
                         SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = RunnerDecl{std::move(visibility), std::move(name),
                          std::move(entry_agent), max_turns,
                          std::move(tracing), std::move(guardrails),
                          std::move(input_guardrails), std::move(output_guardrails),
                          std::move(on_turn), std::move(on_complete)};
  return stmt;
}
}  // namespace

HandoffTarget Parser::parse_handoff_target()
{
  HandoffTarget target;

  // Check for simple identifier reference or handoff_to(...) syntax
  if (match(TokenType::HandoffTo))
  {
    // handoff_to(AgentName) { ... } syntax
    if (!match(TokenType::LeftParen))
    {
      error("Expected '(' after handoff_to");
    }
    if (!match(TokenType::Identifier))
    {
      error("Expected agent identifier in handoff_to");
    }
    target.agent = IdentifierRef{previous().lexeme, span_from_token(previous())};
    if (!match(TokenType::RightParen))
    {
      error("Expected ')' after handoff_to agent");
    }

    // Parse optional configuration block
    if (match(TokenType::LeftBrace))
    {
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match(TokenType::ToolName))
        {
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after tool_name");
          }
          if (!match(TokenType::String))
          {
            error("Expected string literal for tool_name");
          }
          target.tool_name_override = previous().lexeme;
        }
        else if (match(TokenType::Description))
        {
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after description");
          }
          if (!match(TokenType::String))
          {
            error("Expected string literal for description");
          }
          target.description = previous().lexeme;
        }
        else if (match(TokenType::InputFilter))
        {
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after input_filter");
          }
          if (!match(TokenType::Identifier))
          {
            error("Expected identifier for input_filter");
          }
          target.input_filter = IdentifierRef{previous().lexeme, span_from_token(previous())};
        }
        else if (match(TokenType::IsEnabled))
        {
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after is_enabled");
          }
          target.is_enabled = parse_expression();
        }
        else if (match(TokenType::OnHandoff))
        {
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after on_handoff");
          }
          target.on_handoff = parse_expression();
        }
        else if (match(TokenType::InputType))
        {
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after input_type");
          }
          if (!match(TokenType::Identifier))
          {
            error("Expected type identifier for input_type");
          }
          target.input_type = IdentifierRef{previous().lexeme, span_from_token(previous())};
        }
        else
        {
          error("Unknown handoff target config key");
        }
      }
      if (!match(TokenType::RightBrace))
      {
        error("Expected '}' after handoff target config");
      }
    }
  }
  else if (match(TokenType::Identifier))
  {
    // Simple agent reference
    target.agent = IdentifierRef{previous().lexeme, span_from_token(previous())};
  }
  else
  {
    error("Expected agent identifier or handoff_to");
  }

  return target;
}

std::vector<HandoffTarget> Parser::parse_handoff_list()
{
  std::vector<HandoffTarget> targets;
  if (!match(TokenType::LeftBracket))
  {
    error("Expected '[' for handoff list");
  }

  if (!check(TokenType::RightBracket))
  {
    while (true)
    {
      targets.push_back(parse_handoff_target());
      if (!match(TokenType::Comma))
      {
        break;
      }
      if (check(TokenType::RightBracket))
      {
        break;  // Allow trailing comma
      }
    }
  }

  if (!match(TokenType::RightBracket))
  {
    error("Expected ']' after handoff list");
  }
  return targets;
}

AgentCardSchema Parser::parse_card_schema_field()
{
  AgentCardSchema field;

  if (!match(TokenType::Identifier))
  {
    error("Expected field name in schema");
  }
  field.field_name = previous().lexeme;

  if (!match(TokenType::Colon))
  {
    error("Expected ':' after field name");
  }

  // Parse field configuration block
  if (match(TokenType::LeftBrace))
  {
    while (!check(TokenType::RightBrace) && !is_at_end())
    {
      if (match(TokenType::Type))
      {
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after type");
        }
        if (!match(TokenType::String))
        {
          error("Expected string literal for type");
        }
        field.field_type = previous().lexeme;
      }
      else if (match(TokenType::Identifier) && previous().lexeme == "required")
      {
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after required");
        }
        if (match(TokenType::True))
        {
          field.required = true;
        }
        else if (match(TokenType::False))
        {
          field.required = false;
        }
        else
        {
          error("Expected boolean for required");
        }
      }
      else if (match(TokenType::Description))
      {
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after description");
        }
        if (!match(TokenType::String))
        {
          error("Expected string literal for description");
        }
        field.description = previous().lexeme;
      }
      else
      {
        error("Unknown schema field config key");
      }
    }
    if (!match(TokenType::RightBrace))
    {
      error("Expected '}' after schema field config");
    }
  }
  else
  {
    // Simple type syntax: field_name: "type"
    if (!match(TokenType::String))
    {
      error("Expected string literal or '{' for field type");
    }
    field.field_type = previous().lexeme;
  }

  return field;
}

std::vector<AgentCardSchema> Parser::parse_card_schema()
{
  std::vector<AgentCardSchema> schema;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' for schema");
  }

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    schema.push_back(parse_card_schema_field());
    // Allow optional comma between fields
    match(TokenType::Comma);
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' after schema");
  }
  return schema;
}

StmtPtr Parser::parse_handoff(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected handoff name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after handoff name");
  }

  std::vector<HandoffTarget> targets;
  std::optional<ExprPtr> on_handoff_callback;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Identifier) && previous().lexeme == "targets")
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after targets");
      }
      targets = parse_handoff_list();
    }
    else if (match(TokenType::OnHandoff))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after on_handoff");
      }
      on_handoff_callback = parse_expression();
    }
    else
    {
      error("Unknown handoff config key");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated handoff block");
  }
  return make_handoff_decl(visibility, name, std::move(targets),
                           std::move(on_handoff_callback), span);
}

StmtPtr Parser::parse_agent_card(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected card name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after card name");
  }

  std::string version;
  std::string description;
  std::vector<std::string> capabilities;
  std::vector<AgentCardSchema> input_schema;
  std::vector<AgentCardSchema> output_schema;
  std::optional<std::string> endpoint_url;
  std::optional<std::string> authentication;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Version))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after version");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for version");
      }
      version = previous().lexeme;
    }
    else if (match(TokenType::Description))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after description");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for description");
      }
      description = previous().lexeme;
    }
    else if (match(TokenType::Capabilities))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after capabilities");
      }
      if (!match(TokenType::LeftBracket))
      {
        error("Expected '[' for capabilities list");
      }
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::String))
        {
          error("Expected string literal in capabilities");
        }
        capabilities.push_back(previous().lexeme);
        if (!match(TokenType::Comma))
        {
          break;
        }
      }
      if (!match(TokenType::RightBracket))
      {
        error("Expected ']' after capabilities");
      }
    }
    else if (match(TokenType::InputSchema))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after input_schema");
      }
      input_schema = parse_card_schema();
    }
    else if (match(TokenType::OutputSchema))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after output_schema");
      }
      output_schema = parse_card_schema();
    }
    else if (match(TokenType::A2AEndpoint))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after a2a_endpoint");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for a2a_endpoint");
      }
      endpoint_url = previous().lexeme;
    }
    else if (match(TokenType::A2AAuth) || match(TokenType::AuthMethod))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after auth_method");
      }
      if (!match(TokenType::String))
      {
        error("Expected string literal for auth_method");
      }
      authentication = previous().lexeme;
    }
    else
    {
      error("Unknown card config key");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated card block");
  }
  return make_agent_card_decl(visibility, name, std::move(version),
                              std::move(description), std::move(capabilities),
                              std::move(input_schema), std::move(output_schema),
                              std::move(endpoint_url), std::move(authentication), span);
}

StmtPtr Parser::parse_task(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected task name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after task name");
  }

  std::optional<IdentifierRef> target_agent;
  std::vector<AgentCardSchema> input_schema;
  std::optional<ExprPtr> timeout_ms;
  std::optional<ExprPtr> on_status_change;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Identifier) && previous().lexeme == "target_agent")
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after target_agent");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected identifier for target_agent");
      }
      target_agent = IdentifierRef{previous().lexeme, span_from_token(previous())};
    }
    else if (match(TokenType::InputSchema))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after input_schema");
      }
      input_schema = parse_card_schema();
    }
    else if (match(TokenType::Timeout))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after timeout");
      }
      timeout_ms = parse_expression();
    }
    else if (match(TokenType::OnStatusChange))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after on_status_change");
      }
      on_status_change = parse_expression();
    }
    else
    {
      error("Unknown task config key");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated task block");
  }
  return make_task_decl(visibility, name, std::move(target_agent),
                        std::move(input_schema), std::move(timeout_ms),
                        std::move(on_status_change), span);
}

StmtPtr Parser::parse_runner(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected runner name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after runner name");
  }

  std::optional<IdentifierRef> entry_agent_opt;
  std::optional<std::size_t> max_turns;
  std::optional<IdentifierRef> tracing;
  std::vector<IdentifierRef> guardrails;
  std::vector<IdentifierRef> input_guardrails;
  std::vector<IdentifierRef> output_guardrails;
  std::optional<ExprPtr> on_turn;
  std::optional<ExprPtr> on_complete;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::EntryAgent))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after entry_agent");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected identifier for entry_agent");
      }
      entry_agent_opt = IdentifierRef{previous().lexeme, span_from_token(previous())};
    }
    else if (match(TokenType::MaxTurns))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after max_turns");
      }
      if (!match(TokenType::Number))
      {
        error("Expected number literal for max_turns");
      }
      max_turns = static_cast<std::size_t>(std::strtod(previous().lexeme.c_str(), nullptr));
    }
    else if (match(TokenType::Tracing))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after tracing");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected identifier for tracing");
      }
      tracing = IdentifierRef{previous().lexeme, span_from_token(previous())};
    }
    else if (match(TokenType::Guardrails))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after guardrails");
      }
      guardrails = parse_identifier_list();
    }
    else if (match(TokenType::InputGuardrails))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after input_guardrails");
      }
      input_guardrails = parse_identifier_list();
    }
    else if (match(TokenType::OutputGuardrails))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after output_guardrails");
      }
      output_guardrails = parse_identifier_list();
    }
    else if (match(TokenType::OnTurn))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after on_turn");
      }
      on_turn = parse_expression();
    }
    else if (match(TokenType::OnComplete))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after on_complete");
      }
      on_complete = parse_expression();
    }
    else
    {
      error("Unknown runner config key");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated runner block");
  }

  if (!entry_agent_opt)
  {
    error("Runner missing entry_agent");
  }

  return make_runner_decl(visibility, name, *entry_agent_opt, max_turns,
                          std::move(tracing), std::move(guardrails),
                          std::move(input_guardrails), std::move(output_guardrails),
                          std::move(on_turn), std::move(on_complete), span);
}
}  // namespace neamc
