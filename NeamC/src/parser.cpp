//
// NeamC - Parser implementation (tokenized, visitor-friendly)
//

#include "neamc/parser.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

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
                        std::optional<IdentifierRef> policy,
                        std::optional<IdentifierRef> budget,
                        std::optional<IdentifierRef> env,
                        std::optional<IdentifierRef> memory,
                        std::optional<IdentifierRef> world_model,
                        std::optional<IdentifierRef> plan,
                        std::optional<IdentifierRef> connector,
                        SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = AgentDecl{std::move(visibility), std::move(name), std::move(provider),
                         std::move(model), std::move(endpoint), std::move(api_key_env),
                         std::move(temperature), std::move(system), std::move(skills),
                         std::move(connected_knowledge), std::move(required_capabilities),
                         std::move(guardchains), std::move(policy), std::move(budget),
                         std::move(env), std::move(memory), std::move(world_model),
                         std::move(plan), std::move(connector)};
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
      // v0.6.7: External skill adoption keywords
      {"extern", TokenType::Extern},
      {"mcp_server", TokenType::McpServer},
      {"adopt", TokenType::Adopt},
      {"binding", TokenType::Binding},
      {"mcp", TokenType::Mcp},
      {"http", TokenType::Http},
      {"claude_builtin", TokenType::ClaudeBuiltin},
      {"method", TokenType::Method},
      {"url", TokenType::Url},
      {"headers", TokenType::Headers},
      {"response_path", TokenType::ResponsePath},
      {"server", TokenType::Server},
      {"command", TokenType::Command},
      {"args", TokenType::Args},
      {"body_template", TokenType::BodyTemplate},
      // v0.6.9: Security policy
      {"policy", TokenType::Policy},
      // v0.7.0: Data types keywords
      {"for", TokenType::For},
      {"in", TokenType::In},
      {"break", TokenType::Break},
      {"continue", TokenType::Continue},
      {"not", TokenType::Not},
      // v0.7.1: OOP keywords
      {"struct", TokenType::Struct},
      {"fn", TokenType::Fn},
      {"self", TokenType::SelfKw},
      {"mut", TokenType::Mut},
      // v0.7.1 Phase 2-5: trait, sealed, match, extend, agentic
      {"trait", TokenType::Trait},
      {"sealed", TokenType::Sealed},
      {"match", TokenType::Match},
      {"_", TokenType::Underscore},
      {"extend", TokenType::Extend},
      {"pipeline", TokenType::Pipeline},
      {"dispatch", TokenType::Dispatch},
      {"parallel", TokenType::Parallel},
      {"loop", TokenType::LoopPattern},
      {"willSet", TokenType::WillSet},
      {"didSet", TokenType::DidSet},
      {"claw", TokenType::Claw},
      {"forge", TokenType::Forge},
      {"channel", TokenType::Channel},
      {"mart", TokenType::Mart},
      {"semantic", TokenType::Semantic},
      {"spawn", TokenType::Spawn},
      {"warehouse", TokenType::Warehouse},
      {"assessment", TokenType::Assessment},
      {"cutover", TokenType::Cutover},
      {"movement", TokenType::Movement},
      {"waves", TokenType::Waves}};

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
        if (i + 2 < source_.size() && source_[i + 1] == '.' && source_[i + 2] == '.')
        {
          add(TokenType::DotDotDot, 3);
        }
        else
        {
          add(TokenType::Dot);
        }
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
      case '@':
        ++i;  // v0.9: skip @ prefix for schema constraints
        continue;
      case '|':
        if (i + 1 < source_.size() && source_[i + 1] == '>')
        {
          add(TokenType::Pipe, 2);
        }
        else
        {
          ++i;  // skip unknown single |
        }
        continue;
      case '+':
        add(TokenType::Plus);
        continue;
      case '-':
        if (i + 1 < source_.size() && source_[i + 1] == '>')
        {
          add(TokenType::Arrow, 2);
        }
        else
        {
          add(TokenType::Minus);
        }
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
        else if (i + 1 < source_.size() && source_[i + 1] == '>')
        {
          add(TokenType::FatArrow, 2);
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
  // v0.6.7: extern skill <name> { ... }
  if (match(TokenType::Extern))
  {
    if (!match(TokenType::Skill))
    {
      error("Expected 'skill' after 'extern'");
    }
    return parse_extern_skill(has_visibility ? visibility : Visibility{});
  }
  // v0.6.7: mcp_server <name> { ... }
  if (match(TokenType::McpServer))
  {
    return parse_mcp_server();
  }
  // v0.6.7: adopt server.* [as prefix]
  if (match(TokenType::Adopt))
  {
    return parse_adopt_statement();
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
  if (match(TokenType::Policy))
  {
    return parse_policy(has_visibility ? visibility : Visibility{});
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
  // v0.8: claw agent / forge agent dispatch
  if (match(TokenType::Claw))
  {
    if (!match(TokenType::Agent)) { error("Expected 'agent' after 'claw'"); }
    return parse_claw_agent(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Forge))
  {
    if (!match(TokenType::Agent)) { error("Expected 'agent' after 'forge'"); }
    return parse_forge_agent(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Channel))
  {
    return parse_channel_decl();
  }
  // v0.9.3: dataops agent declaration (contextual "dataops agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "dataops")
  {
    auto saved = current_;
    advance(); // consume "dataops"
    if (match(TokenType::Agent))
    {
      return parse_dataops_agent(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;  // not "dataops agent", retreat
  }
  // v0.9.3: scheduler declaration
  if (check(TokenType::Identifier) && peek().lexeme == "scheduler")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_scheduler_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.3: audit_table declaration
  if (check(TokenType::Identifier) && peek().lexeme == "audit_table")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_audit_table_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.3: log_source declaration
  if (check(TokenType::Identifier) && peek().lexeme == "log_source")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_log_source_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.3: platform declaration
  if (check(TokenType::Identifier) && peek().lexeme == "platform")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_platform_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.3: incident_policy declaration
  if (check(TokenType::Identifier) && peek().lexeme == "incident_policy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_incident_policy_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.3: correlation declaration
  if (check(TokenType::Identifier) && peek().lexeme == "correlation")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_correlation_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: governance agent declaration (contextual "governance agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "governance")
  {
    auto saved = current_;
    advance(); // consume "governance"
    if (match(TokenType::Agent))
    {
      return parse_governance_agent(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: catalog_source declaration
  if (check(TokenType::Identifier) && peek().lexeme == "catalog_source")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_gov_catalog_source(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: classification_policy declaration
  if (check(TokenType::Identifier) && peek().lexeme == "classification_policy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_classification_policy(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: access_policy declaration
  if (check(TokenType::Identifier) && peek().lexeme == "access_policy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_access_policy(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: quality_policy declaration
  if (check(TokenType::Identifier) && peek().lexeme == "quality_policy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_quality_policy(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: lineage_policy declaration
  if (check(TokenType::Identifier) && peek().lexeme == "lineage_policy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_lineage_policy(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: compliance_policy declaration
  if (check(TokenType::Identifier) && peek().lexeme == "compliance_policy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_compliance_policy(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: lifecycle_policy declaration
  if (check(TokenType::Identifier) && peek().lexeme == "lifecycle_policy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_lifecycle_policy(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: data_product declaration
  if (check(TokenType::Identifier) && peek().lexeme == "data_product")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_data_product(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: contract_policy declaration
  if (check(TokenType::Identifier) && peek().lexeme == "contract_policy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_contract_policy(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: master_data declaration
  if (check(TokenType::Identifier) && peek().lexeme == "master_data")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_master_data(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: external_tool declaration
  if (check(TokenType::Identifier) && peek().lexeme == "external_tool")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_gov_external_tool(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: glossary declaration
  if (check(TokenType::Identifier) && peek().lexeme == "glossary")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_glossary(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.4: enhanced catalog (gov_catalog) — after catalog_source, disambiguate from v0.9.0 catalog
  // The v0.9.0 catalog is handled below via match_identifier("catalog"). This v0.9.4 version
  // is triggered when the user writes "gov_catalog" explicitly. The existing "catalog" keyword
  // continues to route to the v0.9.0 parser for backward compat.
  if (check(TokenType::Identifier) && peek().lexeme == "gov_catalog")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_gov_catalog(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.5: modeling agent declaration (contextual "modeling agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "modeling")
  {
    auto saved = current_;
    advance(); // consume "modeling"
    if (match(TokenType::Agent))
    {
      return parse_modeling_agent_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.6: analyst agent declaration (contextual "analyst agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "analyst")
  {
    auto saved = current_;
    advance(); // consume "analyst"
    if (match(TokenType::Agent))
    {
      return parse_analyst_agent_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.6: sql_connection declaration
  if (check(TokenType::Identifier) && peek().lexeme == "sql_connection")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_sql_connection_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.6: domain_context declaration
  if (check(TokenType::Identifier) && peek().lexeme == "domain_context")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_domain_context_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.6: query_template declaration
  if (check(TokenType::Identifier) && peek().lexeme == "query_template")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_query_template_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.6: query_optimizer declaration
  if (check(TokenType::Identifier) && peek().lexeme == "query_optimizer")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_query_optimizer_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.6: execution_policy declaration
  if (check(TokenType::Identifier) && peek().lexeme == "execution_policy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_execution_policy_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.6: output_format declaration
  if (check(TokenType::Identifier) && peek().lexeme == "output_format")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_output_format_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.6: query_library declaration
  if (check(TokenType::Identifier) && peek().lexeme == "query_library")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_query_library_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.6: analysis_schedule declaration
  if (check(TokenType::Identifier) && peek().lexeme == "analysis_schedule")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_analysis_schedule_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.7: Data Pipeline Deployment declarations
  if (check(TokenType::Identifier) && peek().lexeme == "deploy_target")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_deploy_target_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "deploy_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_deploy_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "promotion_rule")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_promotion_rule_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "rollback_policy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_rollback_policy_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "artifact_registry")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_artifact_registry_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8: Data Scientist Agent declarations
  if (check(TokenType::Identifier) && peek().lexeme == "problem_statement")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_problem_statement_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "hypothesis_test")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_hypothesis_test_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "feature_engineering")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_feature_engineering_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "ml_experiment")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_ml_experiment_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "automl_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_automl_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "hyperparameter_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_hyperparameter_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "stacked_model")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_stacked_model_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "evaluation_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_evaluation_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "model_registry")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_model_registry_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "explainability_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_explainability_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "code_interpreter")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_code_interpreter_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "venv_manager")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_venv_manager_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "nlp_pipeline")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_nlp_pipeline_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "churn_analysis")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_churn_analysis_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "clv_model")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_clv_model_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "propensity_model")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_propensity_model_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "recommendation_engine")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_recommendation_engine_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "experiment_design")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_experiment_design_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "scenario_analysis")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_scenario_analysis_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "decision_support")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_decision_support_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "eda_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_eda_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "eda_technique_selector")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_eda_technique_selector_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "smart_connector")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_smart_connector_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "volume_router")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_volume_router_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "compute_connector")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_compute_connector_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "file_connector")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_file_connector_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "distributed_compute_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_distributed_compute_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "performance_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_performance_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "data_quality_pipeline")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_data_quality_pipeline_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "self_correction_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_self_correction_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "self_assessment")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_self_assessment_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "adaptive_knowledge_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_adaptive_knowledge_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "analysis_history")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_analysis_history_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "observability_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_observability_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8: datascientist agent declaration (contextual "datascientist agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "datascientist")
  {
    auto saved = current_;
    advance(); // consume "datascientist"
    if (match(TokenType::Agent))
    {
      return parse_datascientist_agent_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.1: causal agent declaration (contextual "causal agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "causal")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_causal_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }
  // v0.9.8.1: causal_discovery declaration
  if (check(TokenType::Identifier) && peek().lexeme == "causal_discovery")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_causal_discovery_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.1: scm declaration
  if (check(TokenType::Identifier) && peek().lexeme == "scm")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_scm_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.1: intervention declaration
  if (check(TokenType::Identifier) && peek().lexeme == "intervention")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_intervention_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.1: counterfactual declaration
  if (check(TokenType::Identifier) && peek().lexeme == "counterfactual")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_counterfactual_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.1: bayesian_model declaration
  if (check(TokenType::Identifier) && peek().lexeme == "bayesian_model")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_bayesian_model_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.1: causal_estimator declaration
  if (check(TokenType::Identifier) && peek().lexeme == "causal_estimator")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_causal_estimator_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.1: quasi_experiment declaration
  if (check(TokenType::Identifier) && peek().lexeme == "quasi_experiment")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_quasi_experiment_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.1: sensitivity_analysis declaration
  if (check(TokenType::Identifier) && peek().lexeme == "sensitivity_analysis")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_sensitivity_analysis_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.1: causal_data_requirements declaration
  if (check(TokenType::Identifier) && peek().lexeme == "causal_data_requirements")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_causal_data_requirements_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: mlops agent declaration (contextual "mlops agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "mlops")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_mlops_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }
  // v0.9.8.2: drift_monitor declaration
  if (check(TokenType::Identifier) && peek().lexeme == "drift_monitor")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_drift_monitor_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: retraining_pipeline declaration
  if (check(TokenType::Identifier) && peek().lexeme == "retraining_pipeline")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_retraining_pipeline_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: deployment_strategy declaration
  if (check(TokenType::Identifier) && peek().lexeme == "deployment_strategy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_deployment_strategy_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: champion_challenger declaration
  if (check(TokenType::Identifier) && peek().lexeme == "champion_challenger")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_champion_challenger_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: serving_infra declaration
  if (check(TokenType::Identifier) && peek().lexeme == "serving_infra")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_serving_infra_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: training_infra declaration
  if (check(TokenType::Identifier) && peek().lexeme == "training_infra")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_training_infra_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: mlops_rollback declaration
  if (check(TokenType::Identifier) && peek().lexeme == "mlops_rollback")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_mlops_rollback_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: monitoring_stack declaration
  if (check(TokenType::Identifier) && peek().lexeme == "monitoring_stack")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_monitoring_stack_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: mlflow_config declaration
  if (check(TokenType::Identifier) && peek().lexeme == "mlflow_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_mlflow_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: business_kpi_tracker declaration
  if (check(TokenType::Identifier) && peek().lexeme == "business_kpi_tracker")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_business_kpi_tracker_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: dataset_version declaration
  if (check(TokenType::Identifier) && peek().lexeme == "dataset_version")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_dataset_version_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: feedback_loop declaration
  if (check(TokenType::Identifier) && peek().lexeme == "feedback_loop")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_feedback_loop_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: decision_engine declaration
  if (check(TokenType::Identifier) && peek().lexeme == "decision_engine")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_decision_engine_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: event_bus declaration
  if (check(TokenType::Identifier) && peek().lexeme == "event_bus")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_event_bus_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.2: drift_rca declaration
  if (check(TokenType::Identifier) && peek().lexeme == "drift_rca")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_drift_rca_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: databa agent declaration (contextual "databa agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "databa")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_databa_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }
  // v0.9.8.3: requirements_elicitation declaration
  if (check(TokenType::Identifier) && peek().lexeme == "requirements_elicitation")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_requirements_elicitation_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: brd_generator declaration
  if (check(TokenType::Identifier) && peek().lexeme == "brd_generator")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_brd_generator_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: functional_spec declaration
  if (check(TokenType::Identifier) && peek().lexeme == "functional_spec")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_functional_spec_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: nonfunctional_spec declaration
  if (check(TokenType::Identifier) && peek().lexeme == "nonfunctional_spec")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_nonfunctional_spec_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: acceptance_criteria_generator declaration
  if (check(TokenType::Identifier) && peek().lexeme == "acceptance_criteria_generator")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_acceptance_criteria_gen_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: data_requirements_ba declaration
  if (check(TokenType::Identifier) && peek().lexeme == "data_requirements_ba")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_data_requirements_ba_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: impact_analysis_ba declaration
  if (check(TokenType::Identifier) && peek().lexeme == "impact_analysis_ba")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_impact_analysis_ba_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: traceability_matrix declaration
  if (check(TokenType::Identifier) && peek().lexeme == "traceability_matrix")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_traceability_matrix_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: etl_requirement_spec declaration
  if (check(TokenType::Identifier) && peek().lexeme == "etl_requirement_spec")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_etl_requirement_spec_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: ml_requirement_spec declaration
  if (check(TokenType::Identifier) && peek().lexeme == "ml_requirement_spec")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_ml_requirement_spec_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: governance_requirement_spec declaration
  if (check(TokenType::Identifier) && peek().lexeme == "governance_requirement_spec")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_governance_requirement_spec_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: analytics_requirement_spec declaration
  if (check(TokenType::Identifier) && peek().lexeme == "analytics_requirement_spec")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_analytics_requirement_spec_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: stakeholder_analysis declaration
  if (check(TokenType::Identifier) && peek().lexeme == "stakeholder_analysis")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_stakeholder_analysis_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: user_story_generator declaration
  if (check(TokenType::Identifier) && peek().lexeme == "user_story_generator")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_user_story_generator_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: scope_management declaration
  if (check(TokenType::Identifier) && peek().lexeme == "scope_management")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_scope_management_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.3: change_impact_analyzer declaration
  if (check(TokenType::Identifier) && peek().lexeme == "change_impact_analyzer")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_change_impact_analyzer_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: datatest agent declaration (contextual "datatest agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "datatest")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_datatest_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }
  // v0.9.8.4: test_strategy declaration
  if (check(TokenType::Identifier) && peek().lexeme == "test_strategy")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_test_strategy_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: test_case_generator declaration
  if (check(TokenType::Identifier) && peek().lexeme == "test_case_generator")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_test_case_generator_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: test_case declaration
  if (check(TokenType::Identifier) && peek().lexeme == "test_case")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_test_case_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: etl_test_suite declaration
  if (check(TokenType::Identifier) && peek().lexeme == "etl_test_suite")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_etl_test_suite_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: dw_test_suite declaration
  if (check(TokenType::Identifier) && peek().lexeme == "dw_test_suite")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_dw_test_suite_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: ml_test_suite declaration
  if (check(TokenType::Identifier) && peek().lexeme == "ml_test_suite")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_ml_test_suite_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: api_test_suite declaration
  if (check(TokenType::Identifier) && peek().lexeme == "api_test_suite")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_api_test_suite_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: performance_test_suite declaration
  if (check(TokenType::Identifier) && peek().lexeme == "performance_test_suite")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_performance_test_suite_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: edge_case_tests declaration
  if (check(TokenType::Identifier) && peek().lexeme == "edge_case_tests")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_edge_case_tests_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: sit_suite declaration
  if (check(TokenType::Identifier) && peek().lexeme == "sit_suite")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_sit_suite_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: uat_suite declaration
  if (check(TokenType::Identifier) && peek().lexeme == "uat_suite")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_uat_suite_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: regression_suite declaration
  if (check(TokenType::Identifier) && peek().lexeme == "regression_suite")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_regression_suite_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: quality_gate declaration
  if (check(TokenType::Identifier) && peek().lexeme == "quality_gate")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_quality_gate_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: test_report_config declaration
  if (check(TokenType::Identifier) && peek().lexeme == "test_report_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_test_report_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.8.4: defect_management declaration
  if (check(TokenType::Identifier) && peek().lexeme == "defect_management")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_defect_management_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: dio agent declaration (contextual "dio agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "dio")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_dio_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }
  // v0.9.9: agent_registry declaration
  if (check(TokenType::Identifier) && peek().lexeme == "agent_registry")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_agent_registry_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: agent_contracts declaration
  if (check(TokenType::Identifier) && peek().lexeme == "agent_contracts")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_agent_contracts_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: raci_matrix declaration
  if (check(TokenType::Identifier) && peek().lexeme == "raci_matrix")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_raci_matrix_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: task_understanding declaration
  if (check(TokenType::Identifier) && peek().lexeme == "task_understanding")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_task_understanding_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: task_decomposer declaration
  if (check(TokenType::Identifier) && peek().lexeme == "task_decomposer")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_task_decomposer_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: crew_formation declaration
  if (check(TokenType::Identifier) && peek().lexeme == "crew_formation")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_crew_formation_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: pattern_selector declaration
  if (check(TokenType::Identifier) && peek().lexeme == "pattern_selector")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_pattern_selector_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: execution_manager_dio declaration
  if (check(TokenType::Identifier) && peek().lexeme == "execution_manager_dio")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_execution_manager_dio_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: dio_state_machine declaration
  if (check(TokenType::Identifier) && peek().lexeme == "dio_state_machine")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_dio_state_machine_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: dio_error_handling declaration
  if (check(TokenType::Identifier) && peek().lexeme == "dio_error_handling")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_dio_error_handling_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: result_synthesizer declaration
  if (check(TokenType::Identifier) && peek().lexeme == "result_synthesizer")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_result_synthesizer_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: infrastructure_profile declaration
  if (check(TokenType::Identifier) && peek().lexeme == "infrastructure_profile")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_infrastructure_profile_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: role_framework declaration
  if (check(TokenType::Identifier) && peek().lexeme == "role_framework")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_role_framework_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: delegation_protocol declaration
  if (check(TokenType::Identifier) && peek().lexeme == "delegation_protocol")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_delegation_protocol_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.9: dio_accountability declaration
  if (check(TokenType::Identifier) && peek().lexeme == "dio_accountability")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_dio_accountability_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // ═══ v1.0: OWASP Security, MCP, Cloud, Evaluation, Special Agent dispatches ═══
  // All v1.0 constructs use the simple-block parser pattern: read name + brace-delimited fields
  {
    static const std::vector<std::pair<std::string, int>> v10_keywords = {
      {"goal_integrity", 1}, {"tool_validator", 2}, {"agent_identity", 3},
      {"supply_chain_policy", 4}, {"code_sandbox", 5}, {"memory_integrity", 6},
      {"message_security", 7}, {"circuit_breaker", 8}, {"human_gate", 9},
      {"agent_attestation", 10}, {"mcp_allowlist", 11}, {"tool_pinning", 12},
      {"context_guard", 13}, {"aibom_config", 14}, {"gym_evaluator", 15},
      {"gateway", 16}, {"model_router", 17}, {"marketplace", 18}
    };
    for (const auto& [kw, id] : v10_keywords) {
      if (check(TokenType::Identifier) && peek().lexeme == kw) {
        auto saved = current_;
        advance();
        if (check(TokenType::Identifier)) {
          return parse_v10_generic_decl(kw, id);
        }
        current_ = saved;
      }
    }
  }
  // v1.0: Special agents (2-keyword: "securitysentinel agent", "protocolbridge agent", "costguardian agent")
  if (check(TokenType::Identifier) && peek().lexeme == "securitysentinel")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_security_sentinel_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "protocolbridge")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_protocol_bridge_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "costguardian")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_cost_guardian_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }
  // ═══ v1.1: NeamOS Foundation keyword dispatches ═══
  // Simple keywords: knowledge_card, context_assembly, agent_persona, locale, governance_rule, agent_adapter, blueprint
  {
    static const std::vector<std::pair<std::string, int>> v11_keywords = {
      {"knowledge_card", 19}, {"context_assembly", 20}, {"agent_persona", 21},
      {"locale", 22}, {"governance_rule", 23}, {"agent_adapter", 24}, {"blueprint", 25}
    };
    for (const auto& [kw, id] : v11_keywords) {
      if (check(TokenType::Identifier) && peek().lexeme == kw) {
        auto saved = current_;
        advance();
        if (check(TokenType::Identifier)) {
          return parse_v10_generic_decl(kw, id);
        }
        current_ = saved;
      }
    }
  }
  // ═══ v1.2: NeamProd keyword dispatches ═══
  {
    static const std::vector<std::pair<std::string, int>> v12_keywords = {
      {"plugin", 26}, {"session_service", 27}, {"eval_test", 28},
      {"eval_set", 29}, {"artifact_store", 30}, {"stream_config", 31},
      {"a2a_config", 32}
    };
    for (const auto& [kw, id] : v12_keywords) {
      if (check(TokenType::Identifier) && peek().lexeme == kw) {
        auto saved = current_;
        advance();
        if (check(TokenType::Identifier)) {
          return parse_v10_generic_decl(kw, id);
        }
        current_ = saved;
      }
    }
  }
  // ═══ v1.3: NeamLab — Auto Research Agent dispatches ═══
  {
    static const std::vector<std::pair<std::string, int>> v13_keywords = {
      {"program", 33}, {"experiment_loop", 34}, {"metric_extractor", 35}
    };
    for (const auto& [kw, id] : v13_keywords) {
      if (check(TokenType::Identifier) && peek().lexeme == kw) {
        auto saved = current_;
        advance();
        if (check(TokenType::Identifier)) {
          return parse_v10_generic_decl(kw, id);
        }
        current_ = saved;
      }
    }
  }
  // v1.3: research agent (2-keyword: "research agent")
  if (check(TokenType::Identifier) && peek().lexeme == "research")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_research_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }
  // ═══ v1.4: NeamWiki dispatches ═══
  // v1.4: wiki agent (2-keyword: "wiki agent <name>") MUST be checked before single-keyword wiki
  if (check(TokenType::Identifier) && peek().lexeme == "wiki")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_wiki_agent_decl(has_visibility ? visibility : Visibility{});
      }
      current_ = saved;
    }
    else if (check(TokenType::Identifier))
    {
      // single-keyword wiki: "wiki <name>"
      return parse_v10_generic_decl("wiki", 36);
    }
    else
    {
      current_ = saved;
    }
  }
  // ═══ v1.4.5: NeamHarness dispatches ═══
  // Single-keyword decls that reuse parse_v10_generic_decl.
  // (harness, handoff, tool_registry, assertion_registry, harness_benchmark)
  if (check(TokenType::Identifier) && peek().lexeme == "harness")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("harness", 37);
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "handoff")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("handoff", 38);
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "tool_registry")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("tool_registry", 39);
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "assertion_registry")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("assertion_registry", 40);
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "harness_benchmark")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("harness_benchmark", 41);
    }
    current_ = saved;
  }
  // ═══ v1.5: NeamEvolve dispatches ═══
  // 2-keyword: "evolve agent <Name> { ... }"  (must be checked before single "evolve")
  if (check(TokenType::Identifier) && peek().lexeme == "evolve")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_v10_generic_decl("evolve_agent", 42);
      }
    }
    current_ = saved;
  }
  // 1-keyword: "belief <Name> { ... }"
  if (check(TokenType::Identifier) && peek().lexeme == "belief")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("belief", 43);
    }
    current_ = saved;
  }
  // 1-keyword: "skill_library <Name> { ... }"
  if (check(TokenType::Identifier) && peek().lexeme == "skill_library")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("skill_library", 44);
    }
    current_ = saved;
  }
  // 1-keyword: "curriculum <Name> { ... }" (P1)
  if (check(TokenType::Identifier) && peek().lexeme == "curriculum")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("curriculum", 45);
    }
    current_ = saved;
  }
  // ═══ v1.6: NeamMesh dispatches (process / task / decision / event / pool) ═══
  if (check(TokenType::Identifier) && peek().lexeme == "process")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("process", 46);
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "task")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("task", 47);
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "decision")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("decision", 48);
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "event")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("event", 49);
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "pool")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_v10_generic_decl("pool", 50);
    }
    current_ = saved;
  }
  // v1.1: Special agents (2-keyword: "knowledgeweaver agent", "adaptagent agent", "storyteller agent")
  if (check(TokenType::Identifier) && peek().lexeme == "knowledgeweaver")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_knowledge_weaver_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "adaptagent")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_adapt_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }
  if (check(TokenType::Identifier) && peek().lexeme == "storyteller")
  {
    auto saved = current_;
    advance();
    if (match(TokenType::Agent))
    {
      if (check(TokenType::Identifier))
      {
        return parse_storyteller_agent_decl(has_visibility ? visibility : Visibility{});
      }
    }
    current_ = saved;
  }

  // v0.9.5: schema_source declaration
  if (check(TokenType::Identifier) && peek().lexeme == "schema_source")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_schema_source_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.5: er_model declaration
  if (check(TokenType::Identifier) && peek().lexeme == "er_model")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_er_model_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.5: entity declaration (for modeling entities)
  if (check(TokenType::Identifier) && peek().lexeme == "entity")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      // Check next token after name — if it's '{', it's a declaration
      auto saved2 = current_;
      advance(); // consume name
      if (check(TokenType::LeftBrace))
      {
        current_ = saved2; // go back to name
        return parse_modeling_entity_decl(has_visibility ? visibility : Visibility{});
      }
      current_ = saved; // not entity decl
    }
    else
    {
      current_ = saved;
    }
  }
  // v0.9.5: dimensional_model declaration
  if (check(TokenType::Identifier) && peek().lexeme == "dimensional_model")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_dimensional_model_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.5: datamart declaration
  if (check(TokenType::Identifier) && peek().lexeme == "datamart")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      // Check it's followed by '{' (declaration, not assignment)
      auto saved2 = current_;
      advance();
      if (check(TokenType::LeftBrace))
      {
        current_ = saved2;
        return parse_datamart_v095_decl(has_visibility ? visibility : Visibility{});
      }
      current_ = saved;
    }
    else
    {
      current_ = saved;
    }
  }
  // v0.9.5: normalization_analysis declaration
  if (check(TokenType::Identifier) && peek().lexeme == "normalization_analysis")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_normalization_analysis_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.5: amendment_config declaration
  if (check(TokenType::Identifier) && peek().lexeme == "amendment_config")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_amendment_config_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.5: amendment declaration
  if (check(TokenType::Identifier) && peek().lexeme == "amendment")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      auto saved2 = current_;
      advance();
      if (check(TokenType::LeftBrace))
      {
        current_ = saved2;
        return parse_amendment_decl(has_visibility ? visibility : Visibility{});
      }
      current_ = saved;
    }
    else
    {
      current_ = saved;
    }
  }
  // v0.9.5: data_profile declaration
  if (check(TokenType::Identifier) && peek().lexeme == "data_profile")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_data_profile_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.5: modeling_tool declaration
  if (check(TokenType::Identifier) && peek().lexeme == "modeling_tool")
  {
    auto saved = current_;
    advance();
    if (check(TokenType::Identifier))
    {
      return parse_modeling_tool_decl(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;
  }
  // v0.9.2: migration agent declaration (contextual "migration agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "migration")
  {
    auto saved = current_;
    advance(); // consume "migration"
    if (match(TokenType::Agent))
    {
      return parse_migration_agent(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;  // not "migration agent", retreat
  }
  // v0.9.1: ETL agent declaration (contextual "etl agent" two-keyword prefix)
  if (check(TokenType::Identifier) && peek().lexeme == "etl")
  {
    auto saved = current_;
    advance(); // consume "etl"
    if (match(TokenType::Agent))
    {
      return parse_etl_agent(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;  // not "etl agent", retreat
  }
  // v0.9: data agent — contextual two-keyword prefix
  if (check(TokenType::Identifier) && peek().lexeme == "data")
  {
    auto saved = current_;
    advance(); // consume "data"
    if (match(TokenType::Agent))
    {
      return parse_data_agent(has_visibility ? visibility : Visibility{});
    }
    current_ = saved;  // not "data agent", retreat
  }
  // v0.9.1: Semantic declaration (top-level)
  if (match(TokenType::Semantic))
  {
    return parse_semantic(has_visibility ? visibility : Visibility{});
  }
  // v0.9: standalone declarations
  if (match_identifier("schema"))
  {
    return parse_schema(has_visibility ? visibility : Visibility{});
  }
  if (match_identifier("source"))
  {
    return parse_source(has_visibility ? visibility : Visibility{});
  }
  if (match_identifier("sink"))
  {
    return parse_sink(has_visibility ? visibility : Visibility{});
  }
  if (match_identifier("quality"))
  {
    return parse_quality(has_visibility ? visibility : Visibility{});
  }
  if (match_identifier("compute"))
  {
    return parse_compute(has_visibility ? visibility : Visibility{});
  }
  if (match_identifier("governance"))
  {
    return parse_governance(has_visibility ? visibility : Visibility{});
  }
  if (match_identifier("catalog"))
  {
    return parse_catalog(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Agent))
  {
    return parse_agent(has_visibility ? visibility : Visibility{});
  }
  // v0.7.1: struct declaration — "struct Foo { ... }" or "mut struct Foo { ... }"
  if (match(TokenType::Struct))
  {
    return parse_struct_decl(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Mut))
  {
    if (!match(TokenType::Struct))
    {
      error("Expected 'struct' after 'mut'");
    }
    Visibility vis = has_visibility ? visibility : Visibility{};
    auto result = parse_struct_decl(vis);
    // Mark the struct as mutable
    auto* sd = std::get_if<StructDecl>(&result->node);
    if (sd) sd->is_mutable = true;
    return result;
  }
  // v0.7.1: impl block — "impl Foo { ... }" or "impl Trait for Foo { ... }"
  if (match(TokenType::Impl))
  {
    // Disambiguate: if next token is an Identifier followed by '{', this is a type impl block.
    // Otherwise fall through (existing skill impl handling is inside parse_skill).
    if (check(TokenType::Identifier))
    {
      return parse_impl_block();
    }
  }
  // v0.7.1 Phase 2: trait declaration
  if (match(TokenType::Trait))
  {
    return parse_trait_decl(has_visibility ? visibility : Visibility{});
  }
  // v0.7.1 Phase 2: sealed type declaration
  if (match(TokenType::Sealed))
  {
    return parse_sealed_decl(has_visibility ? visibility : Visibility{});
  }
  // v0.7.1 Phase 3: extend block
  if (match(TokenType::Extend))
  {
    return parse_extend_block();
  }
  // v0.7.1 Phase 4: agentic patterns
  if (match(TokenType::Pipeline))
  {
    return parse_pipeline_decl();
  }
  if (match(TokenType::Dispatch))
  {
    return parse_dispatch_decl();
  }
  if (match(TokenType::Parallel))
  {
    return parse_parallel_decl();
  }
  if (match(TokenType::LoopPattern))
  {
    return parse_loop_pattern_decl();
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
  if (match(TokenType::For))
  {
    return parse_for_in();
  }
  if (match(TokenType::Break))
  {
    return parse_break_stmt();
  }
  if (match(TokenType::Continue))
  {
    return parse_continue_stmt();
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
  bool sensitive = false;

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
    // v0.6.9 D10: sensitive marker
    if (check(TokenType::Identifier) && peek().lexeme == "sensitive")
    {
      advance();
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after sensitive");
      }
      if (match(TokenType::True))
      {
        sensitive = true;
      }
      else if (match(TokenType::False))
      {
        sensitive = false;
      }
      else
      {
        error("Expected 'true' or 'false' for sensitive");
      }
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
  auto result = make_skill_decl(visibility, name, *description, std::move(params), std::move(*impl), span);
  if (sensitive)
  {
    auto& decl = std::get<SkillDecl>(result->node);
    decl.sensitive = true;
  }
  return result;
}

// v0.6.7: Parse extern skill declaration
StmtPtr Parser::parse_extern_skill(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected extern skill name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after extern skill name");
  }

  std::optional<std::string> description;
  std::vector<SkillParam> params;
  std::optional<SkillBindingSpec> binding;
  bool sensitive = false;

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
    if (match(TokenType::Binding))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after binding");
      }
      binding = parse_skill_binding();
      continue;
    }
    // v0.6.9 D10: sensitive marker
    if (check(TokenType::Identifier) && peek().lexeme == "sensitive")
    {
      advance();
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after sensitive");
      }
      if (match(TokenType::True))
      {
        sensitive = true;
      }
      else if (match(TokenType::False))
      {
        sensitive = false;
      }
      else
      {
        error("Expected 'true' or 'false' for sensitive");
      }
      continue;
    }
    error("Unexpected token in extern skill block");
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated extern skill block");
  }
  if (!description.has_value())
  {
    error("Extern skill missing description");
  }
  if (!binding.has_value())
  {
    error("Extern skill missing binding");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ExternSkillDecl{
      visibility, name, *description, std::move(params), std::move(*binding), sensitive};
  return stmt;
}

// v0.6.7: Parse skill binding specification (mcp, http, claude_builtin)
SkillBindingSpec Parser::parse_skill_binding()
{
  SkillBindingSpec spec;

  if (match(TokenType::Mcp))
  {
    spec.type = SkillBindingSpec::Type::kMcp;
    if (!match(TokenType::LeftBrace))
    {
      error("Expected '{' after mcp");
    }
    while (!check(TokenType::RightBrace) && !is_at_end())
    {
      if (match(TokenType::Server))
      {
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after server");
        }
        if (!match(TokenType::String))
        {
          error("Expected string for server name");
        }
        spec.mcp_server = previous().lexeme;
        continue;
      }
      if (match(TokenType::Tool))
      {
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after tool");
        }
        if (!match(TokenType::String))
        {
          error("Expected string for tool name");
        }
        spec.mcp_tool = previous().lexeme;
        continue;
      }
      error("Unexpected token in mcp binding");
    }
    if (!match(TokenType::RightBrace))
    {
      error("Unterminated mcp binding block");
    }
  }
  else if (match(TokenType::Http))
  {
    spec.type = SkillBindingSpec::Type::kHttp;
    if (!match(TokenType::LeftBrace))
    {
      error("Expected '{' after http");
    }
    while (!check(TokenType::RightBrace) && !is_at_end())
    {
      if (match(TokenType::Method))
      {
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after method");
        }
        if (!match(TokenType::String))
        {
          error("Expected string for method");
        }
        spec.http_method = previous().lexeme;
        continue;
      }
      if (match(TokenType::Url))
      {
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after url");
        }
        if (!match(TokenType::String))
        {
          error("Expected string for url");
        }
        spec.http_url = previous().lexeme;
        continue;
      }
      if (match(TokenType::Headers))
      {
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after headers");
        }
        if (!match(TokenType::LeftBracket))
        {
          error("Expected '[' for headers list");
        }
        while (!check(TokenType::RightBracket) && !is_at_end())
        {
          if (!match(TokenType::String))
          {
            error("Expected string in headers list");
          }
          const auto header = previous().lexeme;
          auto colon_pos = header.find(':');
          if (colon_pos != std::string::npos)
          {
            auto key = header.substr(0, colon_pos);
            auto value = header.substr(colon_pos + 1);
            // Trim leading whitespace from value
            auto start = value.find_first_not_of(" \t");
            if (start != std::string::npos)
            {
              value = value.substr(start);
            }
            spec.http_headers.emplace_back(std::move(key), std::move(value));
          }
          if (check(TokenType::Comma))
          {
            advance();
          }
        }
        if (!match(TokenType::RightBracket))
        {
          error("Expected ']' after headers list");
        }
        continue;
      }
      if (match(TokenType::ResponsePath))
      {
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after response_path");
        }
        if (!match(TokenType::String))
        {
          error("Expected string for response_path");
        }
        spec.http_response_path = previous().lexeme;
        continue;
      }
      if (match(TokenType::Timeout))
      {
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after timeout");
        }
        if (!match(TokenType::Number))
        {
          error("Expected number for timeout");
        }
        spec.http_timeout_ms = static_cast<long>(std::stod(previous().lexeme));
        continue;
      }
      // v0.6.8: body_template for HTTP binding
      if (match(TokenType::BodyTemplate))
      {
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after body_template");
        }
        if (!match(TokenType::String))
        {
          error("Expected string for body_template");
        }
        spec.http_body_template = previous().lexeme;
        continue;
      }
      error("Unexpected token in http binding");
    }
    if (!match(TokenType::RightBrace))
    {
      error("Unterminated http binding block");
    }
    // Default method to GET
    if (spec.http_method.empty())
    {
      spec.http_method = "GET";
    }
  }
  else if (match(TokenType::ClaudeBuiltin))
  {
    spec.type = SkillBindingSpec::Type::kClaudeBuiltin;
    if (!match(TokenType::LeftBrace))
    {
      error("Expected '{' after claude_builtin");
    }
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
          error("Expected string for claude_builtin type");
        }
        spec.claude_tool_type = previous().lexeme;
        continue;
      }
      error("Unexpected token in claude_builtin binding");
    }
    if (!match(TokenType::RightBrace))
    {
      error("Unterminated claude_builtin binding block");
    }
  }
  else
  {
    error("Expected binding type: mcp, http, or claude_builtin");
  }

  return spec;
}

// v0.6.7: Parse mcp_server declaration
StmtPtr Parser::parse_mcp_server()
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected mcp_server name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after mcp_server name");
  }

  McpServerDecl decl;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Command))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after command");
      }
      if (!match(TokenType::String))
      {
        error("Expected string for command");
      }
      decl.command = previous().lexeme;
      continue;
    }
    if (match(TokenType::Url))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after url");
      }
      if (!match(TokenType::String))
      {
        error("Expected string for url");
      }
      decl.url = previous().lexeme;
      continue;
    }
    if (match(TokenType::Args))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after args");
      }
      if (!match(TokenType::LeftBracket))
      {
        error("Expected '[' for args list");
      }
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::String))
        {
          error("Expected string in args list");
        }
        decl.args.push_back(previous().lexeme);
        if (check(TokenType::Comma))
        {
          advance();
        }
      }
      if (!match(TokenType::RightBracket))
      {
        error("Expected ']' after args list");
      }
      continue;
    }
    if (match(TokenType::Env))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after env");
      }
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' for env block");
      }
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (!match(TokenType::Identifier) && !match(TokenType::String))
        {
          error("Expected env key");
        }
        const auto key = previous().lexeme;
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after env key");
        }
        if (!match(TokenType::String))
        {
          error("Expected string for env value");
        }
        decl.env[key] = previous().lexeme;
        if (check(TokenType::Comma))
        {
          advance();
        }
      }
      if (!match(TokenType::RightBrace))
      {
        error("Expected '}' after env block");
      }
      continue;
    }
    error("Unexpected token in mcp_server block");
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated mcp_server block");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

// v0.6.7: Parse adopt statement
StmtPtr Parser::parse_adopt_statement()
{
  SourceSpan span = span_from_token(previous());

  // Parse server_name.* or server_name.{tool1, tool2}
  if (!match(TokenType::Identifier))
  {
    error("Expected server name after 'adopt'");
  }
  const auto server_name = previous().lexeme;

  if (!match(TokenType::Dot))
  {
    error("Expected '.' after server name in adopt");
  }

  AdoptStmt decl;
  decl.server_name = server_name;

  if (match(TokenType::Star))
  {
    // adopt server.* — wildcard, import all tools
    // tool_names stays empty to indicate wildcard
  }
  else if (match(TokenType::LeftBrace))
  {
    // adopt server.{tool1, tool2}
    while (!check(TokenType::RightBrace) && !is_at_end())
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected tool name in adopt list");
      }
      decl.tool_names.push_back(previous().lexeme);
      if (check(TokenType::Comma))
      {
        advance();
      }
    }
    if (!match(TokenType::RightBrace))
    {
      error("Expected '}' after adopt tool list");
    }
  }
  else
  {
    error("Expected '*' or '{tool1, tool2}' after 'server.'");
  }

  // Optional: as prefix
  if (match(TokenType::As))
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected alias prefix after 'as'");
    }
    decl.alias_prefix = previous().lexeme;
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
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
  std::optional<IdentifierRef> policy;  // v0.6.9
  std::optional<IdentifierRef> budget;
  std::optional<IdentifierRef> env;
  std::optional<IdentifierRef> memory;
  std::optional<IdentifierRef> world_model;
  std::optional<IdentifierRef> plan;
  std::optional<IdentifierRef> connector;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);  // consume optional comma between fields
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
    if (match(TokenType::Policy))
    {
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after policy");
      }
      if (!match(TokenType::Identifier))
      {
        error("Expected policy identifier");
      }
      policy = IdentifierRef{previous().lexeme, span_from_token(previous())};
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
    // Field rejection: data-agent-only fields
    if (match(TokenType::Sources) || match_identifier("sources"))
    {
      error("'sources' is not valid on a plain agent. Use 'data agent' for data pipeline agents.");
    }
    if (match_identifier("sinks"))
    {
      error("'sinks' is not valid on a plain agent. Use 'data agent' for data pipeline agents.");
    }
    if (match(TokenType::Pipeline))
    {
      error("'pipeline' is not valid on a plain agent. Use 'data agent' for data pipeline agents.");
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
                         std::move(policy), std::move(budget), std::move(env), std::move(memory),
                         std::move(world_model), std::move(plan), std::move(connector), span);
}

// v0.8: Helper — match identifier by name (for non-keyword field names)
bool Parser::match_identifier(const std::string& name)
{
  if (check(TokenType::Identifier) && peek().lexeme == name)
  {
    advance();
    return true;
  }
  return false;
}

// v0.9.1: Match certain keywords as if they were identifiers
bool Parser::match_keyword_as_identifier()
{
  const auto& current = peek();
  if (current.type == TokenType::Provider || current.type == TokenType::Model ||
      current.type == TokenType::System || current.type == TokenType::Skills ||
      current.type == TokenType::Budget || current.type == TokenType::Env ||
      current.type == TokenType::Mart || current.type == TokenType::Semantic ||
      current.type == TokenType::Warehouse || current.type == TokenType::Sources ||
      current.type == TokenType::Policy || current.type == TokenType::Channel ||
      current.type == TokenType::Guards ||
      current.type == TokenType::Endpoint || current.type == TokenType::Temperature ||
      current.type == TokenType::Tool || current.type == TokenType::Assessment ||
      current.type == TokenType::Cutover || current.type == TokenType::Movement ||
      current.type == TokenType::Waves || current.type == TokenType::Type ||
      current.type == TokenType::Description || current.type == TokenType::Memory ||
      current.type == TokenType::Connector || current.type == TokenType::Capabilities ||
      current.type == TokenType::Returns || current.type == TokenType::Requires ||
      current.type == TokenType::Timeout)
  {
    advance();
    return true;
  }
  return false;
}

// v0.8: Non-fatal warning to stderr
void Parser::warning(const std::string& message) const
{
  std::fprintf(stderr, "Warning [line %zu]: %s\n", peek().line, message.c_str());
}

// v0.8: Parse session { ... } config block
SessionConfig Parser::parse_session_config()
{
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after session");
  }
  SessionConfig config;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("idle_reset_minutes"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after idle_reset_minutes"); }
      if (!match(TokenType::Number)) { error("Expected number for idle_reset_minutes"); }
      config.idle_reset_minutes = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match_identifier("daily_reset_hour"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after daily_reset_hour"); }
      if (!match(TokenType::Number)) { error("Expected number for daily_reset_hour"); }
      config.daily_reset_hour = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match_identifier("max_history_turns"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after max_history_turns"); }
      if (!match(TokenType::Number)) { error("Expected number for max_history_turns"); }
      config.max_history_turns = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match_identifier("compaction"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after compaction"); }
      if (!match(TokenType::String)) { error("Expected string for compaction"); }
      config.compaction = previous().lexeme;
      continue;
    }
    error("Unexpected token in session block");
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated session block");
  }
  return config;
}

// v0.8: Parse loop { ... } config block
LoopConfig Parser::parse_loop_config()
{
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after loop");
  }
  LoopConfig config;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    // max_iterations is a keyword (TokenType::MaxIterations)
    if (match(TokenType::MaxIterations))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after max_iterations"); }
      if (!match(TokenType::Number)) { error("Expected number for max_iterations"); }
      config.max_iterations = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match_identifier("max_cost"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after max_cost"); }
      if (!match(TokenType::Number)) { error("Expected number for max_cost"); }
      config.max_cost = std::strtod(previous().lexeme.c_str(), nullptr);
      continue;
    }
    if (match_identifier("max_tokens"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after max_tokens"); }
      if (!match(TokenType::Number)) { error("Expected number for max_tokens"); }
      config.max_tokens = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
      continue;
    }
    if (match_identifier("prompt_file"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after prompt_file"); }
      if (!match(TokenType::String)) { error("Expected string for prompt_file"); }
      config.prompt_file = previous().lexeme;
      continue;
    }
    if (match_identifier("plan_file"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after plan_file"); }
      if (!match(TokenType::String)) { error("Expected string for plan_file"); }
      config.plan_file = previous().lexeme;
      continue;
    }
    if (match_identifier("progress_file"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after progress_file"); }
      if (!match(TokenType::String)) { error("Expected string for progress_file"); }
      config.progress_file = previous().lexeme;
      continue;
    }
    if (match_identifier("learnings_file"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after learnings_file"); }
      if (!match(TokenType::String)) { error("Expected string for learnings_file"); }
      config.learnings_file = previous().lexeme;
      continue;
    }
    error("Unexpected token in loop block");
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated loop block");
  }
  return config;
}

// v0.8: Parse semantic_memory { ... } config block
SemanticMemoryConfig Parser::parse_semantic_memory_config()
{
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after semantic_memory");
  }
  SemanticMemoryConfig config;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("backend"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after backend"); }
      if (!match(TokenType::String)) { error("Expected string for backend"); }
      config.backend = previous().lexeme;
      continue;
    }
    if (match_identifier("search"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after search"); }
      if (!match(TokenType::String)) { error("Expected string for search"); }
      config.search = previous().lexeme;
      continue;
    }
    if (match_identifier("flush_on_compact"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after flush_on_compact"); }
      if (match(TokenType::True))
      {
        config.flush_on_compact = true;
      }
      else if (match(TokenType::False))
      {
        config.flush_on_compact = false;
      }
      else
      {
        error("Expected true or false for flush_on_compact");
      }
      continue;
    }
    error("Unexpected token in semantic_memory block");
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated semantic_memory block");
  }
  return config;
}

// v0.8: Parse lanes { name: { concurrency: N, priority: "..." } ... }
std::vector<LaneConfig> Parser::parse_lane_configs()
{
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after lanes");
  }
  std::vector<LaneConfig> lanes;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected lane name");
    }
    LaneConfig lane;
    lane.name = previous().lexeme;
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after lane name");
    }
    if (!match(TokenType::LeftBrace))
    {
      error("Expected '{' for lane config");
    }
    while (!check(TokenType::RightBrace) && !is_at_end())
    {
      if (match_identifier("concurrency"))
      {
        if (!match(TokenType::Colon)) { error("Expected ':' after concurrency"); }
        if (!match(TokenType::Number)) { error("Expected number for concurrency"); }
        lane.concurrency = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
        continue;
      }
      if (match_identifier("priority"))
      {
        if (!match(TokenType::Colon)) { error("Expected ':' after priority"); }
        if (!match(TokenType::String)) { error("Expected string for priority"); }
        lane.priority = previous().lexeme;
        continue;
      }
      error("Unexpected token in lane config");
    }
    if (!match(TokenType::RightBrace))
    {
      error("Unterminated lane config block");
    }
    lanes.push_back(std::move(lane));
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated lanes block");
  }
  return lanes;
}

// v0.8: Parse claw agent declaration
StmtPtr Parser::parse_claw_agent(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected claw agent name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after claw agent name");
  }

  // Common fields
  std::optional<std::string> provider;
  std::optional<std::string> model;
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
  // Claw-specific
  SessionConfig session;
  std::vector<IdentifierRef> channels;
  std::vector<LaneConfig> lanes;
  std::optional<SemanticMemoryConfig> semantic_memory;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);  // consume optional comma between fields
    if (match(TokenType::Provider))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after provider"); }
      if (!match(TokenType::String)) { error("Expected string literal for provider"); }
      provider = previous().lexeme;
      continue;
    }
    if (match(TokenType::Model))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after model"); }
      if (!match(TokenType::String)) { error("Expected string literal for model"); }
      model = previous().lexeme;
      continue;
    }
    if (match(TokenType::Endpoint))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after endpoint"); }
      if (!match(TokenType::String)) { error("Expected string literal for endpoint"); }
      endpoint = previous().lexeme;
      continue;
    }
    if (match(TokenType::ApiKeyEnv))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after api_key_env"); }
      if (!match(TokenType::String)) { error("Expected string literal for api_key_env"); }
      api_key_env = previous().lexeme;
      continue;
    }
    if (match(TokenType::Temperature))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after temperature"); }
      if (!match(TokenType::Number)) { error("Expected number literal for temperature"); }
      temperature = std::strtod(previous().lexeme.c_str(), nullptr);
      continue;
    }
    if (match(TokenType::System))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after system"); }
      if (!match(TokenType::String)) { error("Expected string literal for system"); }
      system = previous().lexeme;
      continue;
    }
    if (match(TokenType::Skills))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after skills"); }
      skills = parse_identifier_list();
      continue;
    }
    if (match(TokenType::ConnectedKnowledge))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after connected_knowledge"); }
      connected_knowledge = parse_identifier_list();
      continue;
    }
    if (match(TokenType::Guards))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after guards"); }
      guardchains = parse_identifier_list();
      continue;
    }
    if (match(TokenType::Policy))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after policy"); }
      if (!match(TokenType::Identifier)) { error("Expected policy identifier"); }
      policy = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    if (match(TokenType::Budget))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after budget"); }
      if (!match(TokenType::Identifier)) { error("Expected budget identifier"); }
      budget = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    if (match(TokenType::Env))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after env"); }
      if (!match(TokenType::Identifier)) { error("Expected env identifier"); }
      env = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    // Claw-specific: session
    if (match_identifier("session"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after session"); }
      session = parse_session_config();
      continue;
    }
    // Claw-specific: channels
    if (match_identifier("channels"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after channels"); }
      channels = parse_identifier_list();
      continue;
    }
    // Claw-specific: lanes
    if (match_identifier("lanes"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after lanes"); }
      lanes = parse_lane_configs();
      continue;
    }
    // Claw-specific: semantic_memory
    if (match_identifier("semantic_memory"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after semantic_memory"); }
      semantic_memory = parse_semantic_memory_config();
      continue;
    }
    // Claw-specific: workspace
    if (match_identifier("workspace"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after workspace"); }
      if (!match(TokenType::String)) { error("Expected string for workspace"); }
      workspace = previous().lexeme;
      continue;
    }
    // Field rejection: forge-only fields
    if (match_identifier("verify"))
    {
      error("'verify' is not valid on a claw agent. Use 'forge agent' for verification-driven loops.");
    }
    if (match(TokenType::Checkpoint))
    {
      error("'checkpoint' is not valid on a claw agent. Use 'forge agent' for checkpointed loops.");
    }
    if (match(TokenType::LoopPattern))
    {
      error("'loop' is not valid on a claw agent. Use 'forge agent' for iteration loops.");
    }
    // Deprecation: mode field
    if (match_identifier("mode"))
    {
      warning("'mode' field is deprecated in v0.8. Use 'claw agent' or 'forge agent' keywords instead.");
      if (!match(TokenType::Colon)) { error("Expected ':' after mode"); }
      if (!match(TokenType::String)) { error("Expected string for mode"); }
      continue;
    }
    // Field rejection: data-agent-only fields
    if (match(TokenType::Sources) || match_identifier("sources"))
    {
      error("'sources' is not valid on a claw agent. Use 'data agent' for data pipeline agents.");
    }
    if (match_identifier("sinks"))
    {
      error("'sinks' is not valid on a claw agent. Use 'data agent' for data pipeline agents.");
    }
    if (match(TokenType::Pipeline))
    {
      error("'pipeline' is not valid on a claw agent. Use 'data agent' for data pipeline agents.");
    }
    error("Unexpected token in claw agent block");
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated claw agent block");
  }
  if (!provider.has_value())
  {
    error("Claw agent missing required field: provider");
  }
  if (!model.has_value())
  {
    error("Claw agent missing required field: model");
  }
  if (channels.empty())
  {
    error("Claw agent missing required field: channels (must be non-empty)");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ClawAgentDecl{
      std::move(visibility), std::move(name), std::move(*provider), std::move(*model),
      std::move(endpoint), std::move(api_key_env), std::move(temperature), std::move(system),
      std::move(skills), std::move(connected_knowledge), std::move(guardchains),
      std::move(policy), std::move(budget), std::move(env), std::move(workspace),
      std::move(session), std::move(channels), std::move(lanes), std::move(semantic_memory)};
  return stmt;
}

// v0.8: Parse forge agent declaration
StmtPtr Parser::parse_forge_agent(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected forge agent name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after forge agent name");
  }

  // Common fields
  std::optional<std::string> provider;
  std::optional<std::string> model;
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
  // Forge-specific
  LoopConfig loop;
  ExprPtr verify;
  std::optional<std::string> checkpoint;
  // v1.4.5: forge agent role extension
  std::string forge_role;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);  // consume optional comma between fields
    if (match(TokenType::Provider))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after provider"); }
      if (!match(TokenType::String)) { error("Expected string literal for provider"); }
      provider = previous().lexeme;
      continue;
    }
    if (match(TokenType::Model))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after model"); }
      if (!match(TokenType::String)) { error("Expected string literal for model"); }
      model = previous().lexeme;
      continue;
    }
    if (match(TokenType::Endpoint))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after endpoint"); }
      if (!match(TokenType::String)) { error("Expected string literal for endpoint"); }
      endpoint = previous().lexeme;
      continue;
    }
    if (match(TokenType::ApiKeyEnv))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after api_key_env"); }
      if (!match(TokenType::String)) { error("Expected string literal for api_key_env"); }
      api_key_env = previous().lexeme;
      continue;
    }
    if (match(TokenType::Temperature))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after temperature"); }
      if (!match(TokenType::Number)) { error("Expected number literal for temperature"); }
      temperature = std::strtod(previous().lexeme.c_str(), nullptr);
      continue;
    }
    if (match(TokenType::System))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after system"); }
      if (!match(TokenType::String)) { error("Expected string literal for system"); }
      system = previous().lexeme;
      continue;
    }
    if (match(TokenType::Skills))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after skills"); }
      skills = parse_identifier_list();
      continue;
    }
    if (match(TokenType::Guards))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after guards"); }
      guardchains = parse_identifier_list();
      continue;
    }
    if (match(TokenType::Policy))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after policy"); }
      if (!match(TokenType::Identifier)) { error("Expected policy identifier"); }
      policy = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    if (match(TokenType::Budget))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after budget"); }
      if (!match(TokenType::Identifier)) { error("Expected budget identifier"); }
      budget = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    if (match(TokenType::Env))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after env"); }
      if (!match(TokenType::Identifier)) { error("Expected env identifier"); }
      env = IdentifierRef{previous().lexeme, span_from_token(previous())};
      continue;
    }
    // Forge-specific: loop
    if (match(TokenType::LoopPattern))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after loop"); }
      loop = parse_loop_config();
      continue;
    }
    // Forge-specific: verify
    if (match_identifier("verify"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after verify"); }
      verify = parse_expression();
      continue;
    }
    // Forge-specific: checkpoint
    if (match(TokenType::Checkpoint))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after checkpoint"); }
      if (!match(TokenType::String)) { error("Expected string for checkpoint"); }
      checkpoint = previous().lexeme;
      continue;
    }
    // Forge-specific: workspace
    if (match_identifier("workspace"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after workspace"); }
      if (!match(TokenType::String)) { error("Expected string for workspace"); }
      workspace = previous().lexeme;
      continue;
    }
    // v1.4.5: forge agent role extension (planner/generator/evaluator)
    if (match_identifier("role"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after role"); }
      if (!match(TokenType::String)) { error("Expected string for role"); }
      const std::string r = previous().lexeme;
      if (r != "planner" && r != "generator" && r != "evaluator" && r != "human")
      {
        error("P-FR-001: forge agent role must be one of: planner, generator, evaluator, human (got '" + r + "')");
      }
      // Store on the local variable; attached to the decl below.
      // We use a parser-local variable for clarity:
      forge_role = r;
      continue;
    }
    // Field rejection: claw-only fields
    if (match_identifier("session"))
    {
      error("'session' is not valid on a forge agent. Use 'claw agent' for session management.");
    }
    if (match_identifier("channels"))
    {
      error("'channels' is not valid on a forge agent. Use 'claw agent' for multi-channel support.");
    }
    if (match_identifier("lanes"))
    {
      error("'lanes' is not valid on a forge agent. Use 'claw agent' for lane queues.");
    }
    // Deprecation: mode field
    if (match_identifier("mode"))
    {
      warning("'mode' field is deprecated in v0.8. Use 'claw agent' or 'forge agent' keywords instead.");
      if (!match(TokenType::Colon)) { error("Expected ':' after mode"); }
      if (!match(TokenType::String)) { error("Expected string for mode"); }
      continue;
    }
    // Field rejection: data-agent-only fields
    if (match(TokenType::Sources) || match_identifier("sources"))
    {
      error("'sources' is not valid on a forge agent. Use 'data agent' for data pipeline agents.");
    }
    if (match_identifier("sinks"))
    {
      error("'sinks' is not valid on a forge agent. Use 'data agent' for data pipeline agents.");
    }
    if (match(TokenType::Pipeline))
    {
      error("'pipeline' is not valid on a forge agent. Use 'data agent' for data pipeline agents.");
    }
    error("Unexpected token in forge agent block");
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated forge agent block");
  }
  if (!provider.has_value())
  {
    error("Forge agent missing required field: provider");
  }
  if (!model.has_value())
  {
    error("Forge agent missing required field: model");
  }
  if (!verify)
  {
    error("Forge agent missing required field: verify");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ForgeAgentDecl{
      std::move(visibility), std::move(name), std::move(*provider), std::move(*model),
      std::move(endpoint), std::move(api_key_env), std::move(temperature), std::move(system),
      std::move(skills), std::move(guardchains),
      std::move(policy), std::move(budget), std::move(env), std::move(workspace),
      std::move(loop), std::move(verify), std::move(checkpoint),
      // v1.4.5: role + function_json + ops_json (Phase 2 wires role; function/ops stay empty)
      std::move(forge_role), std::string{}, std::string{}};
  return stmt;
}

// v0.8 Phase 6: Parse channel declaration
// Syntax: channel my_channel { type: "cli", prompt: "> " }
StmtPtr Parser::parse_channel_decl()
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected channel name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after channel name");
  }

  std::unordered_map<std::string, std::string> config;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    // key: value pairs where key is identifier and value is string or number
    if (!match(TokenType::Identifier) && !match(TokenType::Type))
    {
      error("Expected config key in channel declaration");
    }
    const auto key = previous().lexeme;
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after channel config key");
    }
    if (match(TokenType::String))
    {
      config[key] = previous().lexeme;
    }
    else if (match(TokenType::Number))
    {
      config[key] = previous().lexeme;
    }
    else
    {
      error("Expected string or number value for channel config");
    }
    // Allow optional comma between entries
    match(TokenType::Comma);
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' after channel config");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ChannelDecl{name, std::move(config), span};
  return stmt;
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
    match(TokenType::Comma);  // consume optional comma between dimensions
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

// v0.6.9: parse policy block
// policy StrictOps {
//   allow: [tool1, tool2]
//   deny: [tool3]
//   confirm: [tool4]
//   default_deny: true
// }
StmtPtr Parser::parse_policy(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected policy name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after policy name");
  }

  std::vector<std::string> allow_tools;
  std::vector<std::string> deny_tools;
  std::vector<std::string> confirm_tools;
  bool default_deny = true;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Identifier))
    {
      const auto key = previous().lexeme;
      if (!match(TokenType::Colon))
      {
        error("Expected ':' after '" + key + "' in policy block");
      }

      if (key == "allow" || key == "deny" || key == "confirm")
      {
        auto list = parse_identifier_list();
        for (const auto& id : list)
        {
          if (key == "allow")
          {
            allow_tools.push_back(id.name);
          }
          else if (key == "deny")
          {
            deny_tools.push_back(id.name);
          }
          else
          {
            confirm_tools.push_back(id.name);
          }
        }
      }
      else if (key == "default_deny")
      {
        if (match(TokenType::True))
        {
          default_deny = true;
        }
        else if (match(TokenType::False))
        {
          default_deny = false;
        }
        else
        {
          error("Expected 'true' or 'false' for default_deny");
        }
      }
      else
      {
        error("Unknown policy field: " + key);
      }
      continue;
    }
    error("Unexpected token in policy block");
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated policy block");
  }

  PolicyDecl decl;
  decl.visibility = visibility;
  decl.name = name;
  decl.allow_tools = std::move(allow_tools);
  decl.deny_tools = std::move(deny_tools);
  decl.confirm_tools = std::move(confirm_tools);
  decl.default_deny = default_deny;

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
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
  // Accept identifiers and contextual keywords as param names (v0.6.7 fix:
  // keywords like "command", "url", "server" etc. are valid param names)
  if (!match(TokenType::Identifier) && !match(TokenType::Command) &&
      !match(TokenType::Url) && !match(TokenType::Server) &&
      !match(TokenType::Method) && !match(TokenType::Headers) &&
      !match(TokenType::Args) && !match(TokenType::Type) &&
      !match(TokenType::Model) && !match(TokenType::System) &&
      !match(TokenType::Timeout) && !match(TokenType::Description) &&
      !match(TokenType::Params) && !match(TokenType::BodyTemplate) &&
      !match(TokenType::Policy) &&
      !match(TokenType::For) && !match(TokenType::In) &&
      !match(TokenType::Break) && !match(TokenType::Continue) &&
      !match(TokenType::Not) &&
      !match(TokenType::Struct) && !match(TokenType::Fn) &&
      !match(TokenType::SelfKw) && !match(TokenType::Mut) &&
      !match(TokenType::Trait) && !match(TokenType::Sealed) &&
      !match(TokenType::Match) && !match(TokenType::Extend) &&
      !match(TokenType::Pipeline) && !match(TokenType::Dispatch) &&
      !match(TokenType::Parallel) && !match(TokenType::LoopPattern) &&
      !match(TokenType::Claw) && !match(TokenType::Forge) &&
      !match(TokenType::Channel) && !match(TokenType::Spawn) &&
      !match(TokenType::Mart) && !match(TokenType::Semantic) &&
      !match(TokenType::Warehouse))
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
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        std::string key;
        if (match(TokenType::Identifier))
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
      if (!type.has_value() || !path.has_value())
      {
        error("Source entry requires type and path");
      }
      sources.push_back(KnowledgeSource{std::move(*type), std::move(*path)});
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

  // v0.7.0: Check for destructuring patterns [a, b, ...rest] or (a, b)
  if (check(TokenType::LeftBracket) || check(TokenType::LeftParen))
  {
    DestructurePattern pattern;
    if (match(TokenType::LeftBracket))
    {
      pattern.kind = DestructurePattern::Kind::List;
      int idx = 0;
      do
      {
        if (match(TokenType::DotDotDot))
        {
          if (!match(TokenType::Identifier))
          {
            error("Expected identifier after '...'");
          }
          pattern.rest_name = previous().lexeme;
          pattern.has_rest = true;
          pattern.rest_position = idx;
          ++idx;
        }
        else if (match(TokenType::Identifier))
        {
          pattern.names.push_back(previous().lexeme);
          ++idx;
        }
        else
        {
          error("Expected identifier in destructuring pattern");
        }
      } while (match(TokenType::Comma));
      if (!match(TokenType::RightBracket))
      {
        error("Expected ']' after destructuring pattern");
      }
    }
    else
    {
      advance();  // consume (
      pattern.kind = DestructurePattern::Kind::Tuple;
      do
      {
        if (!match(TokenType::Identifier))
        {
          error("Expected identifier in tuple destructuring");
        }
        pattern.names.push_back(previous().lexeme);
      } while (match(TokenType::Comma));
      if (!match(TokenType::RightParen))
      {
        error("Expected ')' after tuple destructuring");
      }
    }
    if (!match(TokenType::Equal))
    {
      error("Expected '=' after destructuring pattern");
    }
    auto initializer = parse_expression();
    if (!match(TokenType::Semicolon))
    {
      error("Expected ';' after destructuring let");
    }
    span = merge_span(span, initializer->span);
    auto stmt = std::make_unique<Statement>();
    stmt->span = span;
    stmt->node = DestructureLetStmt{std::move(pattern), std::move(initializer)};
    return stmt;
  }

  if (!match(TokenType::Identifier) && !match(TokenType::Claw) && !match(TokenType::Forge))
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

// v0.7.0: For-in loop
StmtPtr Parser::parse_for_in()
{
  SourceSpan span = span_from_token(previous());
  std::string variable;
  std::string second_variable;

  // Check for (k, v) destructuring
  if (match(TokenType::LeftParen))
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected identifier in for-in destructuring");
    }
    variable = previous().lexeme;
    if (!match(TokenType::Comma))
    {
      error("Expected ',' in for-in destructuring");
    }
    if (!match(TokenType::Identifier))
    {
      error("Expected second identifier in for-in destructuring");
    }
    second_variable = previous().lexeme;
    if (!match(TokenType::RightParen))
    {
      error("Expected ')' after for-in destructuring");
    }
  }
  else
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected identifier after 'for'");
    }
    variable = previous().lexeme;
  }

  if (!match(TokenType::In))
  {
    error("Expected 'in' after for variable");
  }
  auto iterable = parse_expression();
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after for-in iterable");
  }
  auto body = parse_block();
  span = merge_span(span, iterable->span);

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ForInStmt{std::move(variable), std::move(second_variable),
                          std::move(iterable), std::move(body)};
  return stmt;
}

StmtPtr Parser::parse_break_stmt()
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after 'break'");
  }
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = BreakStmt{};
  return stmt;
}

StmtPtr Parser::parse_continue_stmt()
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after 'continue'");
  }
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ContinueStmt{};
  return stmt;
}

ExprPtr Parser::parse_expression()
{
  return parse_assignment();
}

ExprPtr Parser::parse_assignment()
{
  auto expr = parse_pipe();
  if (match(TokenType::Equal))
  {
    auto value = parse_assignment();
    if (auto ident = std::get_if<IdentifierExpr>(&expr->node))
    {
      auto span = merge_span(expr->span, value->span);
      return make_assignment(ident->name, std::move(value), span);
    }
    // v0.7.0: Index assignment (x[i] = val)
    if (auto idx = std::get_if<IndexExpr>(&expr->node))
    {
      auto span = merge_span(expr->span, value->span);
      auto result = std::make_unique<Expression>();
      result->span = span;
      result->node = IndexAssignExpr{std::move(idx->base), std::move(idx->index), std::move(value)};
      return result;
    }
    // v0.7.1: Property assignment (p.x = val)
    if (auto get = std::get_if<GetExpr>(&expr->node))
    {
      auto span = merge_span(expr->span, value->span);
      auto result = std::make_unique<Expression>();
      result->span = span;
      result->node = SetPropertyExpr{std::move(get->object), get->name, std::move(value)};
      return result;
    }
    error("Invalid assignment target");
  }
  return expr;
}

// v0.7.0: Pipe operator (|>) — desugars to function call
ExprPtr Parser::parse_pipe()
{
  auto expr = parse_equality();
  while (match(TokenType::Pipe))
  {
    // RHS is either .method(args) or fn(args)
    if (match(TokenType::Dot))
    {
      // expr |> .method(args) → expr.method(args)
      if (!match(TokenType::Identifier))
      {
        error("Expected method name after '|> .'");
      }
      auto method_name = previous().lexeme;
      auto span = merge_span(expr->span, span_from_token(previous()));
      auto get = std::make_unique<Expression>();
      get->span = span;
      get->node = GetExpr{std::move(expr), method_name};
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
          error("Expected ')' after pipe method arguments");
        }
        span = merge_span(span, span_from_token(previous()));
        auto call = std::make_unique<Expression>();
        call->span = span;
        call->node = CallExpr{std::move(get), std::move(args)};
        expr = std::move(call);
      }
      else
      {
        // No parens means call with no additional args
        std::vector<ExprPtr> args;
        auto call = std::make_unique<Expression>();
        call->span = span;
        call->node = CallExpr{std::move(get), std::move(args)};
        expr = std::move(call);
      }
    }
    else
    {
      // expr |> fn(args) → fn(expr, args)
      auto fn_expr = parse_call();
      auto span = merge_span(expr->span, fn_expr->span);
      if (auto* call = std::get_if<CallExpr>(&fn_expr->node))
      {
        // Prepend expr as first argument
        call->arguments.insert(call->arguments.begin(), std::move(expr));
        fn_expr->span = span;
        expr = std::move(fn_expr);
      }
      else
      {
        // Just a function name, call with expr as single arg
        std::vector<ExprPtr> args;
        args.push_back(std::move(expr));
        auto result = std::make_unique<Expression>();
        result->span = span;
        result->node = CallExpr{std::move(fn_expr), std::move(args)};
        expr = std::move(result);
      }
    }
  }
  return expr;
}

ExprPtr Parser::parse_equality()
{
  auto expr = parse_contains();
  while (match(TokenType::BangEqual) || match(TokenType::EqualEqual))
  {
    const auto op_token = previous().type;
    auto right = parse_contains();
    auto span = merge_span(expr->span, right->span);
    expr = make_binary(op_token == TokenType::EqualEqual ? BinaryOp::Equal : BinaryOp::NotEqual,
                       std::move(expr), std::move(right), span);
  }
  return expr;
}

// v0.7.0: Contains operator (in / not in)
ExprPtr Parser::parse_contains()
{
  auto expr = parse_comparison();
  if (match(TokenType::In))
  {
    auto right = parse_comparison();
    auto span = merge_span(expr->span, right->span);
    expr = make_binary(BinaryOp::In, std::move(expr), std::move(right), span);
  }
  else if (match(TokenType::Not))
  {
    if (!match(TokenType::In))
    {
      error("Expected 'in' after 'not'");
    }
    auto right = parse_comparison();
    auto span = merge_span(expr->span, right->span);
    expr = make_binary(BinaryOp::NotIn, std::move(expr), std::move(right), span);
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
  while (match(TokenType::Star) || match(TokenType::Slash))
  {
    const auto op_token = previous().type;
    auto right = parse_unary();
    auto span = merge_span(expr->span, right->span);
    expr = make_binary(op_token == TokenType::Star ? BinaryOp::Multiply : BinaryOp::Divide, std::move(expr),
                       std::move(right), span);
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
      // v0.7.1: Detect named construction — Type(name: val, ...)
      // Condition: callee is IdentifierExpr, first token is Identifier followed by Colon
      auto* callee_ident = std::get_if<IdentifierExpr>(&expr->node);
      bool is_named_construct = false;
      if (callee_ident && !check(TokenType::RightParen) &&
          check(TokenType::Identifier))
      {
        // Lookahead: is the token after the identifier a colon?
        std::size_t saved = current_;
        advance();  // consume identifier
        if (check(TokenType::Colon))
        {
          is_named_construct = true;
        }
        current_ = saved;  // restore
      }

      if (is_named_construct)
      {
        // Parse named fields: name: expr, ...
        std::vector<std::pair<std::string, ExprPtr>> fields;
        do
        {
          if (!match(TokenType::Identifier))
          {
            error("Expected field name in named construction");
          }
          std::string field_name = previous().lexeme;
          if (!match(TokenType::Colon))
          {
            error("Expected ':' after field name in named construction");
          }
          auto value = parse_expression();
          fields.push_back({std::move(field_name), std::move(value)});
        } while (match(TokenType::Comma));
        if (!match(TokenType::RightParen))
        {
          error("Expected ')' after named construction");
        }
        auto span = merge_span(expr->span, span_from_token(previous()));
        auto result = std::make_unique<Expression>();
        result->span = span;
        result->node = NamedConstructExpr{callee_ident->name, std::move(fields)};
        expr = std::move(result);
        continue;
      }

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
      // v0.7.0: Tuple positional access (.0, .1, ...)
      if (check(TokenType::Number))
      {
        auto num_tok = peek();
        double val = std::strtod(num_tok.lexeme.c_str(), nullptr);
        if (val >= 0 && val == static_cast<int>(val))
        {
          advance();
          auto span = merge_span(expr->span, span_from_token(previous()));
          expr = make_get(std::move(expr), previous().lexeme, span);
          continue;
        }
      }
      // v0.8: Accept identifiers AND keywords as property names
      // (e.g., agent.model, agent.system, agent.checkpoint)
      if (!match(TokenType::Identifier))
      {
        // Try to accept any keyword token as a property name
        auto tok = peek();
        if (tok.type != TokenType::Eof && tok.type != TokenType::LeftParen &&
            tok.type != TokenType::RightParen && tok.type != TokenType::LeftBrace &&
            tok.type != TokenType::RightBrace && tok.type != TokenType::LeftBracket &&
            tok.type != TokenType::RightBracket && tok.type != TokenType::Semicolon &&
            tok.type != TokenType::Dot && tok.type != TokenType::Comma &&
            !tok.lexeme.empty())
        {
          advance();
        }
        else
        {
          error("Expected property name after '.'");
        }
      }
      auto span = merge_span(expr->span, span_from_token(previous()));
      expr = make_get(std::move(expr), previous().lexeme, span);
      continue;
    }
    if (match(TokenType::LeftBracket))
    {
      // v0.7.0: Check for slice syntax [start:end:step]
      if (check(TokenType::Colon))
      {
        // [: ...] — no start
        ExprPtr start_expr;
        advance();  // consume ':'
        ExprPtr end_expr;
        ExprPtr step_expr;
        if (!check(TokenType::RightBracket) && !check(TokenType::Colon))
        {
          end_expr = parse_expression();
        }
        if (match(TokenType::Colon))
        {
          if (!check(TokenType::RightBracket))
          {
            step_expr = parse_expression();
          }
        }
        if (!match(TokenType::RightBracket))
        {
          error("Expected ']' after slice");
        }
        auto span = merge_span(expr->span, span_from_token(previous()));
        auto slice = std::make_unique<Expression>();
        slice->span = span;
        slice->node = SliceExpr{std::move(expr), std::move(start_expr),
                                 std::move(end_expr), std::move(step_expr)};
        expr = std::move(slice);
        continue;
      }
      auto index = parse_expression();
      // Check if this is a slice (colon after first expression)
      if (match(TokenType::Colon))
      {
        ExprPtr end_expr;
        ExprPtr step_expr;
        if (!check(TokenType::RightBracket) && !check(TokenType::Colon))
        {
          end_expr = parse_expression();
        }
        if (match(TokenType::Colon))
        {
          if (!check(TokenType::RightBracket))
          {
            step_expr = parse_expression();
          }
        }
        if (!match(TokenType::RightBracket))
        {
          error("Expected ']' after slice");
        }
        auto span = merge_span(expr->span, span_from_token(previous()));
        auto slice = std::make_unique<Expression>();
        slice->span = span;
        slice->node = SliceExpr{std::move(expr), std::move(index),
                                 std::move(end_expr), std::move(step_expr)};
        expr = std::move(slice);
        continue;
      }
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
    // v0.7.1: Copy-with expression — expr with (field: val, ...)
    if (match(TokenType::With))
    {
      if (!match(TokenType::LeftParen))
      {
        error("Expected '(' after 'with'");
      }
      std::vector<std::pair<std::string, ExprPtr>> overrides;
      do
      {
        if (!match(TokenType::Identifier))
        {
          error("Expected field name in copy-with");
        }
        std::string field_name = previous().lexeme;
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after field name in copy-with");
        }
        auto value = parse_expression();
        overrides.push_back({std::move(field_name), std::move(value)});
      } while (match(TokenType::Comma));
      if (!match(TokenType::RightParen))
      {
        error("Expected ')' after copy-with fields");
      }
      auto span = merge_span(expr->span, span_from_token(previous()));
      auto result = std::make_unique<Expression>();
      result->span = span;
      result->node = CopyWithExpr{std::move(expr), std::move(overrides)};
      expr = std::move(result);
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
  // v0.8 Phase 8: spawn expression — `spawn agent_name(args...)`
  // If followed by identifier (agent name), parse as spawn keyword.
  // If followed by `(`, fall through to be treated as function call (native spawn).
  if (match(TokenType::Spawn))
  {
    if (check(TokenType::Identifier))
    {
      SourceSpan span = span_from_token(previous());
      advance();  // consume agent name
      std::string agent_name = previous().lexeme;
      if (!match(TokenType::LeftParen))
      {
        error("Expected '(' after agent name in spawn expression");
      }
      std::vector<ExprPtr> args;
      if (!check(TokenType::RightParen))
      {
        while (true)
        {
          args.push_back(parse_expression());
          if (!match(TokenType::Comma))
          {
            break;
          }
        }
      }
      if (!match(TokenType::RightParen))
      {
        error("Expected ')' after spawn arguments");
      }
      span = merge_span(span, span_from_token(previous()));
      auto expr = std::make_unique<Expression>();
      expr->span = span;
      expr->node = SpawnExpr{std::move(agent_name), std::move(args)};
      return expr;
    }
    // No identifier after spawn — treat "spawn" as an identifier (for native fn call)
    return make_identifier("spawn", span_from_token(previous()));
  }
  // v0.7.1 Phase 2: match expression
  if (match(TokenType::Match))
  {
    return parse_match_expr();
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
    // v0.7.0: F-string (f"Hello, {name}!")
    if (previous().lexeme == "f" && check(TokenType::String))
    {
      advance();
      return parse_fstring(previous().lexeme);
    }
    return make_identifier(previous().lexeme, span_from_token(previous()));
  }
  // v0.7.1: 'self' keyword treated as identifier in expressions
  if (match(TokenType::SelfKw))
  {
    return make_identifier("self", span_from_token(previous()));
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
    SourceSpan span = span_from_token(previous());
    // v0.7.0: Empty tuple ()
    if (match(TokenType::RightParen))
    {
      auto tuple = std::make_unique<Expression>();
      tuple->span = merge_span(span, span_from_token(previous()));
      tuple->node = TupleExpr{{}};
      return tuple;
    }
    auto first = parse_expression();
    // v0.7.0: Tuple with trailing comma or multiple elements
    if (match(TokenType::Comma))
    {
      std::vector<ExprPtr> elements;
      elements.push_back(std::move(first));
      if (!check(TokenType::RightParen))
      {
        do
        {
          elements.push_back(parse_expression());
        } while (match(TokenType::Comma));
      }
      if (!match(TokenType::RightParen))
      {
        error("Expected ')' after tuple elements");
      }
      auto tuple = std::make_unique<Expression>();
      tuple->span = merge_span(span, span_from_token(previous()));
      tuple->node = TupleExpr{std::move(elements)};
      return tuple;
    }
    if (!match(TokenType::RightParen))
    {
      error("Expected ')' after expression");
    }
    return first;
  }
  // v0.9: Allow keyword tokens as identifiers in expression context (e.g., map keys)
  if (match(TokenType::Agent))
  {
    return make_identifier("agent", span_from_token(previous()));
  }
  // v0.8: Allow claw/forge as contextual keywords in expressions
  if (match(TokenType::Claw))
  {
    return make_identifier("claw", span_from_token(previous()));
  }
  if (match(TokenType::Forge))
  {
    return make_identifier("forge", span_from_token(previous()));
  }
  error("Unexpected token");
}

// v0.7.0: Parse f-string — extract {expr} segments
ExprPtr Parser::parse_fstring(const std::string& raw)
{
  SourceSpan span = span_from_token(previous());
  std::vector<FStringSegment> segments;
  std::string text;
  std::size_t i = 0;
  while (i < raw.size())
  {
    if (raw[i] == '{')
    {
      if (!text.empty())
      {
        FStringSegment seg;
        seg.is_expr = false;
        seg.text = text;
        segments.push_back(std::move(seg));
        text.clear();
      }
      ++i;
      std::string expr_str;
      int depth = 1;
      while (i < raw.size() && depth > 0)
      {
        if (raw[i] == '{') ++depth;
        else if (raw[i] == '}') --depth;
        if (depth > 0) expr_str += raw[i];
        ++i;
      }
      // Sub-parse the expression (append ; so the parser accepts it as a statement)
      Parser sub_parser(expr_str + ";");
      auto sub_program = sub_parser.parse();
      if (sub_program.statements.empty())
      {
        error("Empty expression in f-string");
      }
      auto* expr_stmt = std::get_if<ExpressionStmt>(&sub_program.statements[0]->node);
      if (!expr_stmt)
      {
        error("Expected expression in f-string");
      }
      FStringSegment seg;
      seg.is_expr = true;
      seg.expr = std::move(expr_stmt->expression);
      segments.push_back(std::move(seg));
    }
    else
    {
      text += raw[i];
      ++i;
    }
  }
  if (!text.empty())
  {
    FStringSegment seg;
    seg.is_expr = false;
    seg.text = text;
    segments.push_back(std::move(seg));
  }
  auto result = std::make_unique<Expression>();
  result->span = span;
  result->node = FStringExpr{std::move(segments)};
  return result;
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
// v0.7.1: Parse struct declaration
// struct Point { x: number, y: number }
StmtPtr Parser::parse_struct_decl(const Visibility& visibility)
{
  auto start_span = span_from_token(previous());

  if (!match(TokenType::Identifier))
  {
    error("Expected struct name");
  }
  std::string name = previous().lexeme;

  // v0.7.1 Phase 5: Parse optional generic type params <T, U>
  std::vector<std::string> type_params;
  if (match(TokenType::Less))
  {
    do
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected type parameter name");
      }
      type_params.push_back(previous().lexeme);
    } while (match(TokenType::Comma));
    if (!match(TokenType::Greater))
    {
      error("Expected '>' after type parameters");
    }
  }

  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after struct name");
  }

  std::vector<FieldDef> fields;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected field name in struct definition");
    }
    std::string field_name = previous().lexeme;

    std::string type_name;
    if (match(TokenType::Colon))
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected type name after ':' in field definition");
      }
      type_name = previous().lexeme;
    }

    ExprPtr default_value;
    if (match(TokenType::Equal))
    {
      default_value = parse_expression();
    }

    // v0.7.1 Phase 5: Parse optional property observer block
    StmtPtr will_set_body;
    std::string will_set_param = "newValue";
    StmtPtr did_set_body;
    ExprPtr guard_expr;

    if (match(TokenType::LeftBrace))
    {
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match(TokenType::WillSet))
        {
          // willSet(paramName) { body } or willSet { body }
          if (match(TokenType::LeftParen))
          {
            if (!match(TokenType::Identifier))
            {
              error("Expected parameter name in willSet");
            }
            will_set_param = previous().lexeme;
            if (!match(TokenType::RightParen))
            {
              error("Expected ')' after willSet parameter");
            }
          }
          if (!match(TokenType::LeftBrace))
          {
            error("Expected '{' for willSet body");
          }
          will_set_body = parse_block();
        }
        else if (match(TokenType::DidSet))
        {
          if (!match(TokenType::LeftBrace))
          {
            error("Expected '{' for didSet body");
          }
          did_set_body = parse_block();
        }
        else if (match(TokenType::Guard))
        {
          guard_expr = parse_expression();
          match(TokenType::Semicolon);
        }
        else
        {
          error("Expected 'willSet', 'didSet', or 'guard' in field observer block");
        }
      }
      if (!match(TokenType::RightBrace))
      {
        error("Expected '}' at end of field observer block");
      }
    }

    FieldDef fd;
    fd.name = std::move(field_name);
    fd.type_name = std::move(type_name);
    fd.default_value = std::move(default_value);
    fd.will_set_body = std::move(will_set_body);
    fd.will_set_param = std::move(will_set_param);
    fd.did_set_body = std::move(did_set_body);
    fd.guard_expr = std::move(guard_expr);
    fields.push_back(std::move(fd));

    // Allow comma or newline separation
    match(TokenType::Comma);
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' at end of struct definition");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = start_span;
  stmt->node = StructDecl{visibility, std::move(name), std::move(type_params),
                           std::move(fields), false};
  return stmt;
}

// v0.7.1: Parse impl block
// impl Point { fn distance_to(other) { ... } }
// impl Describable for Point { fn describe(self) { ... } }
StmtPtr Parser::parse_impl_block()
{
  auto start_span = span_from_token(previous());

  if (!match(TokenType::Identifier))
  {
    error("Expected type name after 'impl'");
  }
  std::string type_name = previous().lexeme;
  std::optional<std::string> trait_name;

  // Check for "impl Trait for Type" syntax
  if (check(TokenType::Identifier) && previous().lexeme != "" && peek().lexeme == "for")
  {
    // Actually: we consumed the first Identifier. Check if next token is For-like.
    // Neam doesn't have a For keyword for this purpose in the right position,
    // but we can check for identifier "for".
  }
  // Re-check: if next is the word "for" (which is TokenType::For), this is a trait impl
  if (match(TokenType::For))
  {
    trait_name = type_name;  // First identifier was the trait name
    if (!match(TokenType::Identifier))
    {
      error("Expected type name after 'for' in trait impl");
    }
    type_name = previous().lexeme;
  }

  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after type name in impl block");
  }

  std::vector<MethodDef> methods;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    bool is_static = false;
    // Check for 'static' prefix (not a keyword, just identifier check)
    // We don't have a Static token — use convention: if method has no 'self' usage,
    // user explicitly marks with no self parameter. But we also support Point.origin()
    // style. For simplicity: methods that don't reference self are static.
    // Actually the plan says "static method calls" — let's not add a keyword.
    // Instead: If first token after fn name ( is not "self", it's static.
    // No — the plan says "Compiler inserts 'self' as first parameter for non-static methods."
    // So we need to detect static vs instance. Let's use: if the method starts with
    // a comment or if the user doesn't use self. Actually, simpler: just check if the
    // method name is preceded by 'fn' keyword.

    if (!match(TokenType::Fn))
    {
      error("Expected 'fn' for method definition in impl block");
    }

    if (!match(TokenType::Identifier))
    {
      error("Expected method name after 'fn'");
    }
    std::string method_name = previous().lexeme;

    if (!match(TokenType::LeftParen))
    {
      error("Expected '(' after method name");
    }

    std::vector<std::string> params;
    // Check if first param is 'self' — if so, this is an instance method
    if (match(TokenType::SelfKw))
    {
      is_static = false;
      // self is consumed, check for more params
      if (match(TokenType::Comma))
      {
        // Parse remaining params
        do
        {
          if (!match(TokenType::Identifier))
          {
            error("Expected parameter name");
          }
          params.push_back(previous().lexeme);
        } while (match(TokenType::Comma));
      }
    }
    else if (check(TokenType::RightParen))
    {
      // No params at all — static method
      is_static = true;
    }
    else
    {
      // First param is not 'self' — static method
      is_static = true;
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
      error("Expected ')' after method parameters");
    }

    if (!match(TokenType::LeftBrace))
    {
      error("Expected '{' for method body");
    }
    auto body = parse_block();

    methods.push_back(MethodDef{std::move(method_name), std::move(params),
                                std::move(body), is_static});
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' at end of impl block");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = start_span;
  stmt->node = ImplBlock{std::move(type_name), std::move(trait_name), std::move(methods)};
  return stmt;
}

// v0.7.1 Phase 2: Parse trait declaration
// trait Describable { fn describe(self); fn default_method(self) { ... } }
StmtPtr Parser::parse_trait_decl(const Visibility& visibility)
{
  auto start_span = span_from_token(previous());

  if (!match(TokenType::Identifier))
  {
    error("Expected trait name");
  }
  std::string name = previous().lexeme;

  // Optional supertraits: trait A : B + C
  std::vector<std::string> supertraits;
  if (match(TokenType::Colon))
  {
    do
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected supertrait name");
      }
      supertraits.push_back(previous().lexeme);
    } while (match(TokenType::Plus));
  }

  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after trait name");
  }

  std::vector<TraitMethodSig> required_methods;
  std::vector<MethodDef> default_methods;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Fn))
    {
      error("Expected 'fn' in trait body");
    }

    if (!match(TokenType::Identifier))
    {
      error("Expected method name after 'fn'");
    }
    std::string method_name = previous().lexeme;

    if (!match(TokenType::LeftParen))
    {
      error("Expected '(' after method name");
    }

    std::vector<std::string> params;
    bool has_self = false;
    if (match(TokenType::SelfKw))
    {
      has_self = true;
      if (match(TokenType::Comma))
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
    }
    else if (!check(TokenType::RightParen))
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
      error("Expected ')' after method parameters");
    }

    // Check for optional return type annotation "-> type"
    if (match(TokenType::Arrow))
    {
      // Consume the return type (currently unused at runtime — Neam is dynamically typed)
      if (!match(TokenType::Identifier))
      {
        error("Expected return type after '->'");
      }
    }

    // If next is '{', this is a default method. Otherwise it's required (ends with ';').
    if (match(TokenType::LeftBrace))
    {
      auto body = parse_block();
      default_methods.push_back(MethodDef{std::move(method_name), std::move(params),
                                           std::move(body), !has_self});
    }
    else
    {
      if (!match(TokenType::Semicolon))
      {
        error("Expected ';' after required method signature or '{' for default body");
      }
      required_methods.push_back(TraitMethodSig{std::move(method_name), std::move(params)});
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' at end of trait declaration");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = start_span;
  stmt->node = TraitDecl{visibility, std::move(name), std::move(supertraits),
                          std::move(required_methods), std::move(default_methods)};
  return stmt;
}

// v0.7.1 Phase 2: Parse sealed type
// sealed Shape { Circle(radius: number), Rect(width: number, height: number), Point }
StmtPtr Parser::parse_sealed_decl(const Visibility& visibility)
{
  auto start_span = span_from_token(previous());

  if (!match(TokenType::Identifier))
  {
    error("Expected sealed type name");
  }
  std::string name = previous().lexeme;

  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after sealed type name");
  }

  std::vector<VariantDef> variants;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected variant name in sealed type");
    }
    std::string variant_name = previous().lexeme;

    std::vector<FieldDef> fields;
    if (match(TokenType::LeftParen))
    {
      // Parse variant fields
      if (!check(TokenType::RightParen))
      {
        do
        {
          if (!match(TokenType::Identifier))
          {
            error("Expected field name in variant");
          }
          std::string field_name = previous().lexeme;

          std::string type_name;
          if (match(TokenType::Colon))
          {
            if (!match(TokenType::Identifier))
            {
              error("Expected type name after ':'");
            }
            type_name = previous().lexeme;
          }

          fields.push_back(FieldDef{std::move(field_name), std::move(type_name), nullptr});
        } while (match(TokenType::Comma));
      }

      if (!match(TokenType::RightParen))
      {
        error("Expected ')' after variant fields");
      }
    }

    variants.push_back(VariantDef{std::move(variant_name), std::move(fields)});
    match(TokenType::Comma);  // Allow optional comma separation
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' at end of sealed type");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = start_span;
  stmt->node = SealedDecl{visibility, std::move(name), std::move(variants)};
  return stmt;
}

// v0.7.1 Phase 2: Parse match expression
// match expr { Idle => ..., Running(task) => ..., _ => ... }
ExprPtr Parser::parse_match_expr()
{
  auto start_span = span_from_token(previous());
  auto subject = parse_expression();

  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after match subject");
  }

  std::vector<MatchArm> arms;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    std::string pattern_name;
    std::vector<std::string> bindings;

    if (match(TokenType::Underscore))
    {
      pattern_name = "_";
    }
    else if (match(TokenType::Identifier))
    {
      pattern_name = previous().lexeme;
      // Optional destructuring: Pattern(a, b, c)
      if (match(TokenType::LeftParen))
      {
        if (!check(TokenType::RightParen))
        {
          do
          {
            if (!match(TokenType::Identifier))
            {
              error("Expected binding name in match arm");
            }
            bindings.push_back(previous().lexeme);
          } while (match(TokenType::Comma));
        }
        if (!match(TokenType::RightParen))
        {
          error("Expected ')' after match bindings");
        }
      }
    }
    else
    {
      error("Expected variant name or '_' in match arm");
    }

    // Optional guard: if condition
    ExprPtr guard;
    if (match(TokenType::If))
    {
      guard = parse_expression();
    }

    if (!match(TokenType::FatArrow))
    {
      error("Expected '=>' after match pattern");
    }

    // Body can be a block or an expression
    ExprPtr body;
    if (check(TokenType::LeftBrace))
    {
      // Parse block as an expression — last expression in block is the result
      // For simplicity, wrap the block in a call to an inline function
      advance();  // consume '{'
      auto block_stmt = parse_block();
      // Wrap block in a function call expression
      // Actually, simpler: just parse expression
      (void)block_stmt;
      error("Block bodies in match arms not yet supported — use expression");
    }
    else
    {
      body = parse_expression();
    }

    arms.push_back(MatchArm{std::move(pattern_name), std::move(bindings),
                              std::move(guard), std::move(body)});
    match(TokenType::Comma);  // Allow optional comma
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' at end of match expression");
  }

  auto expr = std::make_unique<Expression>();
  expr->span = start_span;
  expr->node = MatchExpr{std::move(subject), std::move(arms)};
  return expr;
}

// v0.7.1 Phase 3: Parse extend block
// extend Point { fn rotate(self, angle) { ... } }
StmtPtr Parser::parse_extend_block()
{
  auto start_span = span_from_token(previous());

  if (!match(TokenType::Identifier))
  {
    error("Expected type name after 'extend'");
  }
  std::string target = previous().lexeme;

  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after extend target");
  }

  std::vector<MethodDef> methods;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Fn))
    {
      error("Expected 'fn' in extend block");
    }
    if (!match(TokenType::Identifier))
    {
      error("Expected method name");
    }
    std::string method_name = previous().lexeme;

    if (!match(TokenType::LeftParen))
    {
      error("Expected '(' after method name");
    }

    std::vector<std::string> params;
    bool is_static = true;
    if (match(TokenType::SelfKw))
    {
      is_static = false;
      if (match(TokenType::Comma))
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
    }
    else if (!check(TokenType::RightParen))
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
      error("Expected ')' after method parameters");
    }

    if (!match(TokenType::LeftBrace))
    {
      error("Expected '{' for method body");
    }
    auto body = parse_block();

    methods.push_back(MethodDef{std::move(method_name), std::move(params),
                                 std::move(body), is_static});
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' at end of extend block");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = start_span;
  stmt->node = ExtendBlock{std::move(target), std::move(methods)};
  return stmt;
}

// v0.7.1 Phase 4: Parse pipeline declaration
// pipeline DataAnalysis { steps: [Agent1, Agent2, Agent3] }
StmtPtr Parser::parse_pipeline_decl()
{
  auto start_span = span_from_token(previous());

  if (!match(TokenType::Identifier))
  {
    error("Expected pipeline name");
  }
  std::string name = previous().lexeme;

  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after pipeline name");
  }

  std::vector<std::string> steps;
  // Expect "steps:" key
  if (match(TokenType::Identifier) && previous().lexeme == "steps")
  {
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after 'steps'");
    }
    if (!match(TokenType::LeftBracket))
    {
      error("Expected '[' for steps list");
    }
    if (!check(TokenType::RightBracket))
    {
      do
      {
        if (!match(TokenType::Identifier))
        {
          error("Expected agent name in steps list");
        }
        steps.push_back(previous().lexeme);
      } while (match(TokenType::Comma));
    }
    if (!match(TokenType::RightBracket))
    {
      error("Expected ']' after steps list");
    }
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' at end of pipeline declaration");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = start_span;
  stmt->node = PipelineDecl{std::move(name), std::move(steps)};
  return stmt;
}

// v0.7.1 Phase 4: Parse dispatch declaration
// dispatch Support { router: Triage, routes: { billing: BillingAgent }, fallback: General }
StmtPtr Parser::parse_dispatch_decl()
{
  auto start_span = span_from_token(previous());

  if (!match(TokenType::Identifier))
  {
    error("Expected dispatch name");
  }
  std::string name = previous().lexeme;

  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after dispatch name");
  }

  std::string router_agent;
  std::vector<std::pair<std::string, std::string>> routes;
  std::optional<std::string> fallback_agent;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected key in dispatch declaration");
    }
    std::string key = previous().lexeme;
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after key");
    }

    if (key == "router")
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected agent name for router");
      }
      router_agent = previous().lexeme;
    }
    else if (key == "routes")
    {
      if (!match(TokenType::LeftBrace))
      {
        error("Expected '{' for routes map");
      }
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (!match(TokenType::Identifier))
        {
          error("Expected route key");
        }
        std::string route_key = previous().lexeme;
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after route key");
        }
        if (!match(TokenType::Identifier))
        {
          error("Expected agent name for route");
        }
        routes.push_back({std::move(route_key), previous().lexeme});
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBrace))
      {
        error("Expected '}' after routes");
      }
    }
    else if (key == "fallback")
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected agent name for fallback");
      }
      fallback_agent = previous().lexeme;
    }
    match(TokenType::Comma);
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' at end of dispatch declaration");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = start_span;
  stmt->node = DispatchDecl{std::move(name), std::move(router_agent),
                              std::move(routes), std::move(fallback_agent)};
  return stmt;
}

// v0.7.1 Phase 4: Parse parallel declaration
// parallel Research { agents: [A1, A2], gather: Synthesizer }
StmtPtr Parser::parse_parallel_decl()
{
  auto start_span = span_from_token(previous());

  if (!match(TokenType::Identifier))
  {
    error("Expected parallel name");
  }
  std::string name = previous().lexeme;

  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after parallel name");
  }

  std::vector<std::string> agents;
  std::string gather_agent;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier))
    {
      error("Expected key in parallel declaration");
    }
    std::string key = previous().lexeme;
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after key");
    }

    if (key == "agents")
    {
      if (!match(TokenType::LeftBracket))
      {
        error("Expected '[' for agents list");
      }
      if (!check(TokenType::RightBracket))
      {
        do
        {
          if (!match(TokenType::Identifier))
          {
            error("Expected agent name");
          }
          agents.push_back(previous().lexeme);
        } while (match(TokenType::Comma));
      }
      if (!match(TokenType::RightBracket))
      {
        error("Expected ']' after agents list");
      }
    }
    else if (key == "gather")
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected agent name for gather");
      }
      gather_agent = previous().lexeme;
    }
    match(TokenType::Comma);
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' at end of parallel declaration");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = start_span;
  stmt->node = ParallelDecl{std::move(name), std::move(agents), std::move(gather_agent)};
  return stmt;
}

// v0.7.1 Phase 4: Parse loop pattern declaration
// loop Review { generator: Writer, critic: Reviewer, max_iterations: 5 }
StmtPtr Parser::parse_loop_pattern_decl()
{
  auto start_span = span_from_token(previous());

  if (!match(TokenType::Identifier))
  {
    error("Expected loop pattern name");
  }
  std::string name = previous().lexeme;

  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after loop pattern name");
  }

  std::string generator_agent;
  std::string critic_agent;
  int max_iterations = 5;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match(TokenType::MaxIterations))
    {
      error("Expected key in loop pattern declaration");
    }
    std::string key = previous().lexeme;
    if (!match(TokenType::Colon))
    {
      error("Expected ':' after key");
    }

    if (key == "generator")
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected agent name for generator");
      }
      generator_agent = previous().lexeme;
    }
    else if (key == "critic")
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected agent name for critic");
      }
      critic_agent = previous().lexeme;
    }
    else if (key == "max_iterations")
    {
      if (!match(TokenType::Number))
      {
        error("Expected number for max_iterations");
      }
      max_iterations = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
    }
    match(TokenType::Comma);
  }

  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' at end of loop pattern declaration");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = start_span;
  stmt->node = LoopPatternDecl{std::move(name), std::move(generator_agent),
                                 std::move(critic_agent), max_iterations, nullptr};
  return stmt;
}

// ============================================================================
// v0.9: Data Agent Parse Functions
// ============================================================================

void Parser::consume_optional_comma()
{
  match(TokenType::Comma);
}

std::vector<std::string> Parser::parse_string_list()
{
  std::vector<std::string> result;
  if (!match(TokenType::LeftBracket))
  {
    error("Expected '[' for string list");
  }
  while (!check(TokenType::RightBracket) && !is_at_end())
  {
    if (!match(TokenType::String))
    {
      error("Expected string in string list");
    }
    result.push_back(previous().lexeme);
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBracket))
  {
    error("Expected ']' after string list");
  }
  return result;
}

ExprPtr Parser::parse_config_block()
{
  // Parse { key: value, ... } where keys can be identifiers or strings
  // Returns a MapExpr
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' for config block");
  }
  std::vector<MapEntry> entries;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    // Key: identifier or string
    std::string key;
    if (match(TokenType::String))
    {
      key = previous().lexeme;
    }
    else if (match(TokenType::Identifier) || match(TokenType::Model) ||
             match(TokenType::Type) || match(TokenType::Env) ||
             match(TokenType::System) || match(TokenType::Provider) ||
             match(TokenType::Budget) || match(TokenType::Guards) ||
             match(TokenType::Skills) || match(TokenType::Pipeline) ||
             match(TokenType::Sources) || match(TokenType::Memory))
    {
      key = previous().lexeme;
    }
    else
    {
      error("Expected key (identifier or string) in config block");
    }

    if (!match(TokenType::Colon))
    {
      error("Expected ':' after key in config block");
    }

    // Value: expression (handles strings, numbers, bools, lists, nested maps, identifiers)
    ExprPtr value;
    if (check(TokenType::LeftBrace))
    {
      value = parse_config_block();
    }
    else if (check(TokenType::LeftBracket))
    {
      // Parse array — could be list of maps, strings, or identifiers
      value = parse_expression();
    }
    else
    {
      value = parse_expression();
    }

    auto key_expr = std::make_unique<Expression>();
    key_expr->span = value->span;
    key_expr->node = LiteralExpr{vm::Value::String(key.c_str(), key.size())};
    entries.push_back(MapEntry{std::move(key_expr), std::move(value)});

    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace))
  {
    error("Expected '}' in config block");
  }
  auto expr = std::make_unique<Expression>();
  expr->span = span_from_token(previous());
  expr->node = MapExpr{std::move(entries)};
  return expr;
}

StmtPtr Parser::parse_schema(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected schema name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after schema name");
  }

  std::optional<int> version;
  std::vector<SchemaFieldDecl> fields;

  // Valid field types
  static const std::unordered_set<std::string> valid_types = {
      "string", "int", "float", "bool", "datetime"};

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    // Check for version field
    if (match_identifier("version"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after version"); }
      if (!match(TokenType::Number)) { error("Expected number for version"); }
      version = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
      match(TokenType::Comma);
      continue;
    }

    // Field: name: type @constraint ...
    if (!match(TokenType::Identifier))
    {
      error("Expected field name in schema");
    }
    SchemaFieldDecl field;
    field.name = previous().lexeme;

    if (!match(TokenType::Colon)) { error("Expected ':' after field name"); }

    if (!match(TokenType::Identifier))
    {
      error("Expected type name after ':'");
    }
    field.type_name = previous().lexeme;

    // Validate field type (DATA-010)
    if (valid_types.find(field.type_name) == valid_types.end())
    {
      error("Invalid field type '" + field.type_name + "'. Valid types: string, int, float, bool, datetime");
    }

    // Parse @constraint annotations
    // The '@' character is skipped by the tokenizer, so @primary_key becomes just "primary_key" as an Identifier
    static const std::unordered_set<std::string> constraint_names = {
        "primary_key", "not_null", "unique", "pattern", "length",
        "range", "positive", "enum", "default", "foreign_key"};

    while (check(TokenType::Identifier) &&
           constraint_names.count(peek().lexeme) > 0)
    {
      advance();
      SchemaConstraint constraint;
      constraint.type = previous().lexeme;

      if (constraint.type == "range" || constraint.type == "length")
      {
        if (!match(TokenType::LeftParen)) { error("Expected '(' after @" + constraint.type); }
        if (!match(TokenType::Number)) { error("Expected number"); }
        double min_val = std::strtod(previous().lexeme.c_str(), nullptr);
        if (!match(TokenType::Comma)) { error("Expected ','"); }
        if (!match(TokenType::Number)) { error("Expected number"); }
        double max_val = std::strtod(previous().lexeme.c_str(), nullptr);
        // DATA-012: range min > max
        if (constraint.type == "range" && min_val > max_val)
        {
          error("@range min (" + std::to_string(min_val) + ") must be <= max (" +
                std::to_string(max_val) + ")");
        }
        constraint.number_args = {min_val, max_val};
        if (!match(TokenType::RightParen)) { error("Expected ')'"); }
      }
      else if (constraint.type == "pattern")
      {
        if (!match(TokenType::LeftParen)) { error("Expected '(' after @pattern"); }
        if (!match(TokenType::String)) { error("Expected regex string"); }
        constraint.string_args.push_back(previous().lexeme);
        if (!match(TokenType::RightParen)) { error("Expected ')'"); }
      }
      else if (constraint.type == "enum")
      {
        if (!match(TokenType::LeftParen)) { error("Expected '(' after @enum"); }
        auto values = parse_string_list();
        // DATA-013: empty enum
        if (values.empty())
        {
          error("@enum must have at least one value");
        }
        constraint.string_args = std::move(values);
        if (!match(TokenType::RightParen)) { error("Expected ')'"); }
      }
      else if (constraint.type == "default")
      {
        if (!match(TokenType::LeftParen)) { error("Expected '(' after @default"); }
        // Accept any expression as default
        if (match(TokenType::Identifier))
        {
          constraint.string_args.push_back(previous().lexeme);
          if (match(TokenType::LeftParen)) // function call like now()
          {
            match(TokenType::RightParen);
          }
        }
        else if (match(TokenType::String))
        {
          constraint.string_args.push_back(previous().lexeme);
        }
        else if (match(TokenType::Number))
        {
          constraint.number_args.push_back(std::strtod(previous().lexeme.c_str(), nullptr));
        }
        if (!match(TokenType::RightParen)) { error("Expected ')'"); }
      }
      else if (constraint.type == "foreign_key")
      {
        if (!match(TokenType::LeftParen)) { error("Expected '(' after @foreign_key"); }
        if (!match(TokenType::Identifier)) { error("Expected schema reference"); }
        constraint.ref = previous().lexeme;
        if (!match(TokenType::RightParen)) { error("Expected ')'"); }
      }
      // primary_key, not_null, unique, positive: no args

      field.constraints.push_back(std::move(constraint));
    }

    fields.push_back(std::move(field));
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated schema block");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = SchemaDecl{
      std::move(visibility), std::move(name), std::move(version), std::move(fields)};
  return stmt;
}

StmtPtr Parser::parse_source(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected source name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after source name");
  }

  static const std::unordered_set<std::string> valid_source_types = {
      "postgres", "s3", "http", "kafka"};

  std::optional<std::string> type;
  std::optional<std::string> connection;
  std::optional<std::string> format;
  std::optional<std::string> refresh;
  std::optional<std::string> schema_ref;
  std::vector<std::string> partition_by;
  std::optional<std::string> classification;
  std::optional<std::string> mode;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Type))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after type"); }
      if (!match(TokenType::String)) { error("Expected string for source type"); }
      std::string t = previous().lexeme;
      // DATA-006: validate source type
      if (valid_source_types.find(t) == valid_source_types.end())
      {
        error("Unknown source type '" + t + "'. Valid types: postgres, s3, http, kafka");
      }
      type = std::move(t);
      consume_optional_comma();
      continue;
    }
    if (match_identifier("connection"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after connection"); }
      // connection can be a string or a function call like env("...")
      if (match(TokenType::String))
      {
        connection = previous().lexeme;
      }
      else if (match_identifier("env") || match(TokenType::Env))
      {
        // env("VAR_NAME") — store as-is
        if (!match(TokenType::LeftParen)) { error("Expected '(' after env"); }
        if (!match(TokenType::String)) { error("Expected string in env()"); }
        connection = "env(" + previous().lexeme + ")";
        if (!match(TokenType::RightParen)) { error("Expected ')'"); }
      }
      else
      {
        error("Expected string or env() for connection");
      }
      consume_optional_comma();
      continue;
    }
    if (match_identifier("format"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after format"); }
      if (!match(TokenType::String)) { error("Expected string for format"); }
      format = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match_identifier("refresh"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after refresh"); }
      if (!match(TokenType::String)) { error("Expected string for refresh"); }
      refresh = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match_identifier("schema"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after schema"); }
      if (!match(TokenType::Identifier)) { error("Expected schema identifier"); }
      schema_ref = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match_identifier("partition_by"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after partition_by"); }
      partition_by = parse_string_list();
      consume_optional_comma();
      continue;
    }
    if (match_identifier("classification"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after classification"); }
      if (!match(TokenType::String)) { error("Expected string for classification"); }
      classification = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match_identifier("mode"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after mode"); }
      if (!match(TokenType::String)) { error("Expected string for mode"); }
      mode = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    error("Unexpected token in source block");
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated source block");
  }
  if (!type.has_value())
  {
    error("Source missing required field: type");
  }
  if (!connection.has_value())
  {
    error("Source missing required field: connection");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = SourceDecl{
      std::move(visibility), std::move(name), std::move(*type), std::move(*connection),
      std::move(format), std::move(refresh), std::move(schema_ref), std::move(partition_by),
      std::move(classification), std::move(mode)};
  return stmt;
}

StmtPtr Parser::parse_sink(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected sink name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after sink name");
  }

  static const std::unordered_set<std::string> valid_write_modes = {
      "append", "upsert", "replace", "merge"};

  std::optional<std::string> type;
  std::optional<std::string> connection;
  std::optional<std::string> format;
  std::optional<std::string> write_mode;
  std::optional<int> batch_size;
  std::optional<std::string> schema_ref;
  std::optional<std::string> compute_ref;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Type))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after type"); }
      if (!match(TokenType::String)) { error("Expected string for sink type"); }
      type = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match_identifier("connection"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after connection"); }
      if (match(TokenType::String))
      {
        connection = previous().lexeme;
      }
      else if (match_identifier("env") || match(TokenType::Env))
      {
        if (!match(TokenType::LeftParen)) { error("Expected '(' after env"); }
        if (!match(TokenType::String)) { error("Expected string in env()"); }
        connection = "env(" + previous().lexeme + ")";
        if (!match(TokenType::RightParen)) { error("Expected ')'"); }
      }
      else
      {
        error("Expected string or env() for connection");
      }
      consume_optional_comma();
      continue;
    }
    if (match_identifier("format"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after format"); }
      if (!match(TokenType::String)) { error("Expected string for format"); }
      format = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match_identifier("write_mode"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after write_mode"); }
      if (!match(TokenType::String)) { error("Expected string for write_mode"); }
      std::string wm = previous().lexeme;
      // DATA-008: validate write mode
      if (valid_write_modes.find(wm) == valid_write_modes.end())
      {
        error("Invalid write_mode '" + wm + "'. Valid modes: append, upsert, replace, merge");
      }
      write_mode = std::move(wm);
      consume_optional_comma();
      continue;
    }
    if (match_identifier("batch_size"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after batch_size"); }
      if (!match(TokenType::Number)) { error("Expected number for batch_size"); }
      batch_size = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr));
      consume_optional_comma();
      continue;
    }
    if (match_identifier("schema"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after schema"); }
      if (!match(TokenType::Identifier)) { error("Expected schema identifier"); }
      schema_ref = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match_identifier("compute"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':' after compute"); }
      if (!match(TokenType::Identifier)) { error("Expected compute identifier"); }
      compute_ref = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    error("Unexpected token in sink block");
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated sink block");
  }
  if (!type.has_value())
  {
    error("Sink missing required field: type");
  }
  if (!connection.has_value())
  {
    error("Sink missing required field: connection");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = SinkDecl{
      std::move(visibility), std::move(name), std::move(*type), std::move(*connection),
      std::move(format), std::move(write_mode), std::move(batch_size),
      std::move(schema_ref), std::move(compute_ref)};
  return stmt;
}

StmtPtr Parser::parse_quality(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected quality name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after quality name");
  }

  static const std::unordered_set<std::string> valid_violations = {
      "warn", "gate", "gate_and_alert", "gate_and_escalate", "quarantine_and_alert"};

  std::optional<std::string> freshness;
  std::optional<double> completeness;
  std::vector<std::string> uniqueness;
  std::optional<bool> drift_detection;
  std::optional<double> anomaly_threshold;
  std::optional<std::string> on_violation;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("freshness"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for freshness"); }
      freshness = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match_identifier("completeness"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::Number)) { error("Expected number for completeness"); }
      double val = std::strtod(previous().lexeme.c_str(), nullptr);
      // DATA-022: completeness range
      if (val < 0.0 || val > 1.0)
      {
        error("completeness must be between 0.0 and 1.0, got " + std::to_string(val));
      }
      completeness = val;
      consume_optional_comma();
      continue;
    }
    if (match_identifier("uniqueness"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      uniqueness = parse_string_list();
      consume_optional_comma();
      continue;
    }
    if (match_identifier("drift_detection"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (match(TokenType::True))
        drift_detection = true;
      else if (match(TokenType::False))
        drift_detection = false;
      else
        error("Expected boolean for drift_detection");
      consume_optional_comma();
      continue;
    }
    if (match_identifier("anomaly_threshold"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::Number)) { error("Expected number for anomaly_threshold"); }
      anomaly_threshold = std::strtod(previous().lexeme.c_str(), nullptr);
      consume_optional_comma();
      continue;
    }
    if (match_identifier("on_violation"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for on_violation"); }
      std::string ov = previous().lexeme;
      // DATA-021: validate on_violation
      if (valid_violations.find(ov) == valid_violations.end())
      {
        error("Invalid on_violation '" + ov + "'. Valid: warn, gate, gate_and_alert, gate_and_escalate, quarantine_and_alert");
      }
      on_violation = std::move(ov);
      consume_optional_comma();
      continue;
    }
    error("Unexpected token in quality block");
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated quality block");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = QualityDecl{
      std::move(visibility), std::move(name), std::move(freshness), std::move(completeness),
      std::move(uniqueness), std::move(drift_detection), std::move(anomaly_threshold),
      std::move(on_violation)};
  return stmt;
}

StmtPtr Parser::parse_compute(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected compute name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after compute name");
  }

  static const std::unordered_set<std::string> valid_engines = {
      "spark", "snowflake", "databricks", "bigquery", "local"};

  std::optional<std::string> engine;
  ExprPtr config;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("engine"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for engine"); }
      std::string e = previous().lexeme;
      // DATA-016: validate engine
      if (valid_engines.find(e) == valid_engines.end())
      {
        error("Unknown compute engine '" + e + "'. Valid: spark, snowflake, databricks, bigquery, local");
      }
      engine = std::move(e);
      consume_optional_comma();
      continue;
    }
    if (match_identifier("config"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      config = parse_config_block();
      consume_optional_comma();
      continue;
    }
    error("Unexpected token in compute block");
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated compute block");
  }
  if (!engine.has_value())
  {
    error("Compute missing required field: engine");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = ComputeDecl{
      std::move(visibility), std::move(name), std::move(*engine), std::move(config)};
  return stmt;
}

StmtPtr Parser::parse_governance(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected governance name");
  }
  const auto name = previous().lexeme;

  // Parse entire body as a config block
  auto body = parse_config_block();

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = GovernanceDecl{
      std::move(visibility), std::move(name), std::move(body)};
  return stmt;
}

StmtPtr Parser::parse_catalog(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected catalog name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after catalog name");
  }

  static const std::unordered_set<std::string> valid_engines = {
      "unity_catalog", "datahub"};

  std::optional<std::string> engine;
  ExprPtr register_opts;
  std::optional<bool> discovery;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("engine"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for engine"); }
      std::string e = previous().lexeme;
      // DATA-024: validate catalog engine
      if (valid_engines.find(e) == valid_engines.end())
      {
        error("Unknown catalog engine '" + e + "'. Valid: unity_catalog, datahub");
      }
      engine = std::move(e);
      consume_optional_comma();
      continue;
    }
    if (match_identifier("register"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      register_opts = parse_config_block();
      consume_optional_comma();
      continue;
    }
    if (match_identifier("discovery"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (match(TokenType::True))
        discovery = true;
      else if (match(TokenType::False))
        discovery = false;
      else
        error("Expected boolean for discovery");
      consume_optional_comma();
      continue;
    }
    error("Unexpected token in catalog block");
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated catalog block");
  }
  if (!engine.has_value())
  {
    error("Catalog missing required field: engine");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = CatalogDecl{
      std::move(visibility), std::move(name), std::move(*engine),
      std::move(register_opts), std::move(discovery)};
  return stmt;
}

PipelineBlock Parser::parse_pipeline_block()
{
  PipelineBlock block;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' for pipeline block");
  }

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("extract"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      block.extract = parse_identifier_list();
      consume_optional_comma();
      continue;
    }
    if (match_identifier("transform"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::LeftBracket)) { error("Expected '['"); }

      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        PipelineTransformOp op;
        // Accept identifier or keyword token as transform name
        if (match(TokenType::Identifier) || match(TokenType::Type))
        {
          op.name = previous().lexeme;
        }
        else
        {
          error("Expected transform operator name");
        }

        // Parse named arguments: (key: value, ...)
        if (match(TokenType::LeftParen))
        {
          while (!check(TokenType::RightParen) && !is_at_end())
          {
            // Key — accept identifiers and keyword tokens
            std::string key;
            if (match(TokenType::Identifier) || match(TokenType::Type) ||
                match(TokenType::Sources))
            {
              key = previous().lexeme;
            }
            else if (match_identifier("source") || match_identifier("column") ||
                     match_identifier("op") || match_identifier("value") ||
                     match_identifier("keys") || match_identifier("rules") ||
                     match_identifier("group_by") || match_identifier("metrics") ||
                     match_identifier("expr") || match_identifier("ascending") ||
                     match_identifier("n") || match_identifier("agent") ||
                     match_identifier("on") || match_identifier("stage") ||
                     match_identifier("engine"))
            {
              key = previous().lexeme;
            }
            else
            {
              // Try consuming any token as key
              advance();
              key = previous().lexeme;
            }

            if (!match(TokenType::Colon)) { error("Expected ':' in transform args"); }

            ExprPtr value = parse_expression();
            op.args.push_back({std::move(key), std::move(value)});
            match(TokenType::Comma);
          }
          if (!match(TokenType::RightParen)) { error("Expected ')'"); }
        }

        block.transforms.push_back(std::move(op));
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) { error("Expected ']'"); }
      consume_optional_comma();
      continue;
    }
    if (match_identifier("load"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      block.load = parse_identifier_list();
      consume_optional_comma();
      continue;
    }
    error("Unexpected token in pipeline block. Expected: extract, transform, load");
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated pipeline block");
  }
  return block;
}

DataAgentComputeBlock Parser::parse_data_agent_compute_block()
{
  DataAgentComputeBlock block;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' for compute block");
  }

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("default"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::Identifier)) { error("Expected compute identifier"); }
      block.default_engine = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma();
      continue;
    }
    if (match_identifier("available"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      block.available = parse_identifier_list();
      consume_optional_comma();
      continue;
    }
    if (match_identifier("routing"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::LeftBracket)) { error("Expected '['"); }
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::LeftBrace)) { error("Expected '{'"); }
        std::string stage;
        std::string engine_name;
        while (!check(TokenType::RightBrace) && !is_at_end())
        {
          if (match_identifier("stage"))
          {
            if (!match(TokenType::Colon)) { error("Expected ':'"); }
            if (!match(TokenType::String)) { error("Expected string for stage"); }
            stage = previous().lexeme;
            consume_optional_comma();
            continue;
          }
          if (match_identifier("engine"))
          {
            if (!match(TokenType::Colon)) { error("Expected ':'"); }
            if (!match(TokenType::Identifier)) { error("Expected engine identifier"); }
            engine_name = previous().lexeme;
            consume_optional_comma();
            continue;
          }
          // Skip unknown fields in routing rules (e.g., condition)
          if (match(TokenType::Identifier) || match(TokenType::String))
          {
            if (match(TokenType::Colon)) { parse_expression(); }
            consume_optional_comma();
            continue;
          }
          error("Expected 'stage' or 'engine' in routing rule");
        }
        if (!match(TokenType::RightBrace)) { error("Expected '}'"); }
        block.routing.push_back({std::move(stage), IdentifierRef{std::move(engine_name), {}}});
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) { error("Expected ']'"); }
      consume_optional_comma();
      continue;
    }
    error("Expected 'default', 'available', or 'routing' in compute block");
  }
  if (!match(TokenType::RightBrace))
  {
    error("Unterminated compute block");
  }
  return block;
}

StmtPtr Parser::parse_data_agent(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
  {
    error("Expected data agent name");
  }
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace))
  {
    error("Expected '{' after data agent name");
  }

  // Common fields
  std::optional<std::string> provider;
  std::optional<std::string> model;
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
  std::optional<std::string> agent_md;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match(TokenType::Provider))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for provider"); }
      provider = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match(TokenType::Model))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for model"); }
      model = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match(TokenType::Endpoint))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for endpoint"); }
      endpoint = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match(TokenType::ApiKeyEnv))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for api_key_env"); }
      api_key_env = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match(TokenType::Temperature))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::Number)) { error("Expected number for temperature"); }
      temperature = std::strtod(previous().lexeme.c_str(), nullptr);
      consume_optional_comma();
      continue;
    }
    if (match(TokenType::System))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for system"); }
      system = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match(TokenType::Skills))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      skills = parse_identifier_list();
      consume_optional_comma();
      continue;
    }
    if (match(TokenType::Guards) || match_identifier("guardchains"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      guardchains = parse_identifier_list();
      consume_optional_comma();
      continue;
    }
    if (match(TokenType::Policy))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::Identifier)) { error("Expected policy identifier"); }
      policy = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma();
      continue;
    }
    if (match(TokenType::Budget))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::Identifier)) { error("Expected budget identifier"); }
      budget = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma();
      continue;
    }
    if (match(TokenType::Env))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::Identifier)) { error("Expected env identifier"); }
      env = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma();
      continue;
    }
    // Data agent specific fields
    if (match(TokenType::Sources) || match_identifier("sources"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      sources = parse_identifier_list();
      consume_optional_comma();
      continue;
    }
    if (match_identifier("sinks"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      sinks = parse_identifier_list();
      consume_optional_comma();
      continue;
    }
    if (match_identifier("schema"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::Identifier)) { error("Expected schema identifier"); }
      schema_ref = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma();
      continue;
    }
    if (match_identifier("quality"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::Identifier)) { error("Expected quality identifier"); }
      quality_ref = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma();
      continue;
    }
    if (match_identifier("compute"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      compute = parse_data_agent_compute_block();
      consume_optional_comma();
      continue;
    }
    if (match_identifier("governance"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::Identifier)) { error("Expected governance identifier"); }
      governance_ref = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma();
      continue;
    }
    if (match_identifier("catalog"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::Identifier)) { error("Expected catalog identifier"); }
      catalog_ref = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma();
      continue;
    }
    if (match_identifier("lineage"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (match(TokenType::True))
        lineage = true;
      else if (match(TokenType::False))
        lineage = false;
      else
        error("Expected boolean for lineage");
      consume_optional_comma();
      continue;
    }
    if (match_identifier("role"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for role"); }
      role = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match_identifier("purpose"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for purpose"); }
      purpose = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match_identifier("autonomy"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string for autonomy"); }
      autonomy = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    if (match(TokenType::Pipeline))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      pipeline = parse_pipeline_block();
      consume_optional_comma();
      continue;
    }
    if (match_identifier("agent_md"))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      if (!match(TokenType::String)) { error("Expected string path for agent_md"); }
      agent_md = previous().lexeme;
      consume_optional_comma();
      continue;
    }
    // Field rejection: claw/forge-only fields
    if (match_identifier("verify"))
    {
      error("'verify' is not valid on a data agent. Use 'forge agent' for verification-driven loops.");
    }
    if (match_identifier("channels"))
    {
      error("'channels' is not valid on a data agent. Use 'claw agent' for channel-based agents.");
    }
    if (match(TokenType::Checkpoint))
    {
      error("'checkpoint' is not valid on a data agent. Use 'forge agent' for checkpointed loops.");
    }
    if (match(TokenType::LoopPattern))
    {
      error("'loop' is not valid on a data agent. Use 'forge agent' for iteration loops.");
    }
    if (match_identifier("session"))
    {
      error("'session' is not valid on a data agent. Use 'claw agent' for session management.");
    }
    if (match_identifier("semantic_memory"))
    {
      error("'semantic_memory' is not valid on a data agent. Use 'claw agent' for semantic memory.");
    }
    if (match(TokenType::ConnectedKnowledge))
    {
      if (!match(TokenType::Colon)) { error("Expected ':'"); }
      // Accept connected_knowledge for data agents but store as-is
      auto ck = parse_identifier_list();
      // Data agents don't use connected_knowledge, just skip it for now
      consume_optional_comma();
      continue;
    }
    error("Unexpected token in data agent block");
  }

  if (!match(TokenType::RightBrace))
  {
    error("Unterminated data agent block");
  }
  if (!provider.has_value())
  {
    error("Data agent missing required field: provider");
  }
  if (!model.has_value())
  {
    error("Data agent missing required field: model");
  }
  // DATA-001: data agent must have at least one source
  if (sources.empty())
  {
    error("Data agent must have at least one source");
  }

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = DataAgentDecl{
      std::move(visibility), std::move(name), std::move(*provider), std::move(*model),
      std::move(endpoint), std::move(api_key_env), std::move(temperature), std::move(system),
      std::move(skills), std::move(guardchains), std::move(policy), std::move(budget),
      std::move(env), std::move(sources), std::move(sinks), std::move(schema_ref),
      std::move(quality_ref), std::move(compute), std::move(governance_ref),
      std::move(catalog_ref), std::move(lineage), std::move(role), std::move(purpose),
      std::move(autonomy), std::move(pipeline), std::move(agent_md)};
  return stmt;
}

// ---------------------------------------------------------------------------
// v0.9.1  ETL Agent parse functions
// ---------------------------------------------------------------------------

StmtPtr Parser::parse_semantic(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier))
    error("Expected semantic layer name");
  const auto name = previous().lexeme;

  if (!match(TokenType::LeftBrace))
    error("Expected '{' after semantic name");

  SemanticDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("metrics"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        SemanticMetricDecl metric;
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier())
          error("Expected metric name");
        metric.name = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::LeftBrace)) error("Expected '{'");

        while (!check(TokenType::RightBrace) && !is_at_end())
        {
          if (match_identifier("sql"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected SQL string");
            metric.sql = previous().lexeme;
            consume_optional_comma(); continue;
          }
          if (match_identifier("description") || match(TokenType::Description))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected description string");
            metric.description = previous().lexeme;
            consume_optional_comma(); continue;
          }
          if (match_identifier("type") || match(TokenType::Type))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected type string");
            std::string t = previous().lexeme;
            if (t != "measure" && t != "ratio" && t != "cumulative" && t != "derived")
              error("Invalid metric type '" + t + "'. Expected: measure, ratio, cumulative, derived");
            metric.type = t;
            consume_optional_comma(); continue;
          }
          if (match_identifier("time_grains"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            metric.time_grains = parse_string_list();
            consume_optional_comma(); continue;
          }
          if (match_identifier("dimensions"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            metric.dimensions = parse_string_list();
            consume_optional_comma(); continue;
          }
          if (match_identifier("filters"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            metric.filters = parse_string_list();
            consume_optional_comma(); continue;
          }
          if (match_identifier("owner"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected owner string");
            metric.owner = previous().lexeme;
            consume_optional_comma(); continue;
          }
          error("Unexpected field in metric: " + peek().lexeme);
        }
        if (!match(TokenType::RightBrace)) error("Expected '}' after metric body");
        consume_optional_comma();

        if (metric.sql.empty()) error("Metric '" + metric.name + "' missing 'sql'");
        decl.metrics.push_back(std::move(metric));
      }
      if (!match(TokenType::RightBrace)) error("Expected '}' after metrics");
      consume_optional_comma(); continue;
    }

    if (match_identifier("entities"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        SemanticEntityDecl entity;
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier())
          error("Expected entity name");
        entity.name = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::LeftBrace)) error("Expected '{'");

        while (!check(TokenType::RightBrace) && !is_at_end())
        {
          if (match_identifier("table"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected table string");
            entity.table = previous().lexeme;
            consume_optional_comma(); continue;
          }
          if (match_identifier("key"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected key string");
            entity.key = previous().lexeme;
            consume_optional_comma(); continue;
          }
          if (match_identifier("description") || match(TokenType::Description))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected description string");
            entity.description = previous().lexeme;
            consume_optional_comma(); continue;
          }
          error("Unexpected field in entity: " + peek().lexeme);
        }
        if (!match(TokenType::RightBrace)) error("Expected '}' after entity body");
        consume_optional_comma();

        if (entity.table.empty()) error("Entity '" + entity.name + "' missing 'table'");
        if (entity.key.empty()) error("Entity '" + entity.name + "' missing 'key'");
        decl.entities.push_back(std::move(entity));
      }
      if (!match(TokenType::RightBrace)) error("Expected '}' after entities");
      consume_optional_comma(); continue;
    }

    if (match_identifier("relationships"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        SemanticRelationshipDecl rel;
        if (!match(TokenType::LeftBrace)) error("Expected '{'");
        while (!check(TokenType::RightBrace) && !is_at_end())
        {
          if (match_identifier("from"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected string");
            rel.from_entity = previous().lexeme;
            consume_optional_comma(); continue;
          }
          if (match_identifier("to") || match(TokenType::To))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected string");
            rel.to_entity = previous().lexeme;
            consume_optional_comma(); continue;
          }
          if (match_identifier("type") || match(TokenType::Type))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected string");
            rel.type = previous().lexeme;
            consume_optional_comma(); continue;
          }
          if (match_identifier("join"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected string");
            rel.join_condition = previous().lexeme;
            consume_optional_comma(); continue;
          }
          error("Unexpected field in relationship");
        }
        if (!match(TokenType::RightBrace)) error("Expected '}'");
        decl.relationships.push_back(std::move(rel));
        consume_optional_comma();
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }

    if (match_identifier("synonyms"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (!match(TokenType::String)) error("Expected synonym key string");
        std::string key = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::String)) error("Expected synonym value string");
        decl.synonyms[key] = previous().lexeme;
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      consume_optional_comma(); continue;
    }

    if (match_identifier("time_intelligence"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      SemanticTimeIntelligence ti;
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match_identifier("fiscal_year_start"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string");
          ti.fiscal_year_start = previous().lexeme;
          consume_optional_comma(); continue;
        }
        if (match_identifier("week_start"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string");
          ti.week_start = previous().lexeme;
          consume_optional_comma(); continue;
        }
        if (match_identifier("default_timezone"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string");
          ti.default_timezone = previous().lexeme;
          consume_optional_comma(); continue;
        }
        error("Unexpected field in time_intelligence");
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      decl.time_intelligence = ti;
      consume_optional_comma(); continue;
    }

    error("Unexpected field in semantic declaration: " + peek().lexeme);
  }

  if (!match(TokenType::RightBrace))
    error("Expected '}' after semantic body");

  if (decl.metrics.empty())
    error("semantic '" + name + "' must have at least one metric");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

MartDecl Parser::parse_mart()
{
  // At entry: 'mart' keyword already consumed
  if (!match(TokenType::String) && !match(TokenType::Identifier))
    error("Expected mart name");
  const auto name = previous().lexeme;

  if (!match(TokenType::LeftBrace))
    error("Expected '{' after mart name '" + name + "'");

  MartDecl decl;
  decl.name = name;

  bool has_facts = false, has_dimensions = false;
  bool has_grain = false, has_measures = false;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("facts"))
    {
      if (has_facts) error("Duplicate 'facts' in mart '" + name + "'");
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.facts = parse_string_list();
      has_facts = true; consume_optional_comma(); continue;
    }
    if (match_identifier("dimensions"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.dimensions = parse_string_list();
      has_dimensions = true; consume_optional_comma(); continue;
    }
    if (match_identifier("grain"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected grain description string");
      decl.grain = previous().lexeme;
      has_grain = true; consume_optional_comma(); continue;
    }
    if (match_identifier("measures"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.measures = parse_string_list();
      has_measures = true; consume_optional_comma(); continue;
    }
    if (match_identifier("scd"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier())
          error("Expected dimension name for SCD config");
        std::string dim_name = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::String)) error("Expected SCD type string");
        std::string scd_type = previous().lexeme;
        if (scd_type != "type0" && scd_type != "type1" && scd_type != "type2" &&
            scd_type != "type3" && scd_type != "type4" && scd_type != "type6")
          error("Invalid SCD type '" + scd_type + "'. Expected: type0-type6");
        decl.scd.push_back({dim_name, scd_type});
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("conformed"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.conformed = parse_string_list();
      consume_optional_comma(); continue;
    }
    if (match_identifier("aggregate_tables"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        MartAggregateTableDef agg;
        if (!match(TokenType::LeftBrace)) error("Expected '{'");
        while (!check(TokenType::RightBrace) && !is_at_end())
        {
          if (match_identifier("name"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected string");
            agg.name = previous().lexeme;
            consume_optional_comma(); continue;
          }
          if (match_identifier("grain"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::String)) error("Expected string");
            agg.grain = previous().lexeme;
            consume_optional_comma(); continue;
          }
          if (match_identifier("group_by"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            agg.group_by = parse_string_list();
            consume_optional_comma(); continue;
          }
          if (match_identifier("measures"))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            agg.measures = parse_string_list();
            consume_optional_comma(); continue;
          }
          error("Unexpected field in aggregate_table");
        }
        if (!match(TokenType::RightBrace)) error("Expected '}'");
        decl.aggregate_tables.push_back(std::move(agg));
        consume_optional_comma();
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("materialization"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected materialization string");
      std::string m = previous().lexeme;
      if (m != "table" && m != "incremental" && m != "view")
        error("Invalid materialization '" + m + "'");
      decl.materialization = m;
      consume_optional_comma(); continue;
    }
    error("Unexpected field in mart '" + name + "': " + peek().lexeme);
  }

  if (!match(TokenType::RightBrace))
    error("Expected '}' after mart body");

  if (!has_facts) error("mart '" + name + "' missing required field 'facts'");
  if (!has_dimensions) error("mart '" + name + "' missing required field 'dimensions'");
  if (!has_grain) error("mart '" + name + "' missing required field 'grain'");
  if (!has_measures) error("mart '" + name + "' missing required field 'measures'");

  return decl;
}

LayersBlock Parser::parse_layers_block()
{
  LayersBlock block;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("staging"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      block.staging = parse_layer_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("integration"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      block.integration = parse_layer_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("marts"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (match(TokenType::Mart) || match_identifier("mart"))
        {
          block.marts.push_back(parse_mart());
        }
        else
        {
          error("Expected 'mart' keyword in marts list");
        }
        consume_optional_comma();
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    error("Unexpected field in layers: " + peek().lexeme);
  }

  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return block;
}

LayerConfig Parser::parse_layer_config()
{
  LayerConfig config;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("prefix"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected prefix string");
      config.prefix = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("operations"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      config.operations = parse_string_list();
      consume_optional_comma(); continue;
    }
    if (match_identifier("materialization"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected materialization string");
      config.materialization = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("naming"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected naming pattern string");
      config.naming = previous().lexeme;
      consume_optional_comma(); continue;
    }
    error("Unexpected field in layer config");
  }

  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return config;
}

IncrementalBlock Parser::parse_incremental_block()
{
  IncrementalBlock block;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  bool has_strategy = false;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("strategy"))
    {
      if (has_strategy) error("Duplicate 'strategy'");
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected strategy string");
      std::string s = previous().lexeme;
      if (s != "timestamp" && s != "id" && s != "cdc" && s != "full_refresh")
        error("Invalid incremental strategy '" + s + "'. Expected: timestamp, id, cdc, full_refresh");
      block.strategy = s;
      has_strategy = true; consume_optional_comma(); continue;
    }
    if (match_identifier("key"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected key column string");
      block.key = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("lookback"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected lookback interval string");
      block.lookback = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("unique_key"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected unique_key string");
      block.unique_key = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("on_schema_change"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      std::string v = previous().lexeme;
      if (v != "append" && v != "sync" && v != "fail")
        error("Invalid on_schema_change '" + v + "'");
      block.on_schema_change = v;
      consume_optional_comma(); continue;
    }
    if (match_identifier("full_refresh_schedule"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected interval string");
      block.full_refresh_schedule = previous().lexeme;
      consume_optional_comma(); continue;
    }
    error("Unexpected field in incremental block");
  }

  if (!match(TokenType::RightBrace)) error("Expected '}'");
  if (!has_strategy) error("incremental block requires 'strategy'");
  return block;
}

AutoModelBlock Parser::parse_auto_model_block()
{
  AutoModelBlock block;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("enabled"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::True) && !match(TokenType::False))
        error("Expected true or false");
      block.enabled = (previous().type == TokenType::True);
      consume_optional_comma(); continue;
    }
    if (match_identifier("methodology"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected methodology string");
      std::string m = previous().lexeme;
      if (m != "kimball" && m != "inmon" && m != "data_vault")
        error("Invalid methodology '" + m + "'. Expected: kimball, inmon, data_vault");
      block.methodology = m;
      consume_optional_comma(); continue;
    }
    if (match_identifier("discover"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      AutoModelDiscover discover;
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        auto parse_bool_field = [&](const std::string& field, bool& target) {
          if (match_identifier(field))
          {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::True) && !match(TokenType::False))
              error("Expected bool for " + field);
            target = (previous().type == TokenType::True);
            consume_optional_comma();
            return true;
          }
          return false;
        };
        if (parse_bool_field("facts", discover.facts)) continue;
        if (parse_bool_field("dimensions", discover.dimensions)) continue;
        if (parse_bool_field("relationships", discover.relationships)) continue;
        if (parse_bool_field("scd_types", discover.scd_types)) continue;
        if (parse_bool_field("conformed_dimensions", discover.conformed_dimensions)) continue;
        if (parse_bool_field("degenerate_dimensions", discover.degenerate_dimensions)) continue;
        if (parse_bool_field("junk_dimensions", discover.junk_dimensions)) continue;
        if (parse_bool_field("bridge_tables", discover.bridge_tables)) continue;
        if (parse_bool_field("hubs", discover.hubs)) continue;
        if (parse_bool_field("links", discover.links)) continue;
        if (parse_bool_field("satellites", discover.satellites)) continue;
        if (parse_bool_field("effectivity", discover.effectivity)) continue;
        error("Unexpected field in auto_model.discover: " + peek().lexeme);
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      block.discover = discover;
      consume_optional_comma(); continue;
    }
    if (match_identifier("generate"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      AutoModelGenerate gen;
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match_identifier("surrogate_keys"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string");
          gen.surrogate_keys = previous().lexeme;
          consume_optional_comma(); continue;
        }
        if (match_identifier("hash_keys"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string");
          gen.hash_keys = previous().lexeme;
          consume_optional_comma(); continue;
        }
        auto parse_bool = [&](const std::string& f, bool& t) {
          if (match_identifier(f)) {
            if (!match(TokenType::Colon)) error("Expected ':'");
            if (!match(TokenType::True) && !match(TokenType::False)) error("Expected bool");
            t = (previous().type == TokenType::True);
            consume_optional_comma();
            return true;
          }
          return false;
        };
        if (parse_bool("date_dimension", gen.date_dimension)) continue;
        if (parse_bool("audit_columns", gen.audit_columns)) continue;
        if (parse_bool("hash_diff", gen.hash_diff)) continue;
        if (parse_bool("load_date_column", gen.load_date_column)) continue;
        if (parse_bool("record_source_column", gen.record_source_column)) continue;
        if (parse_bool("hash_diff_for_satellites", gen.hash_diff_for_satellites)) continue;
        error("Unexpected field in auto_model.generate");
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      block.generate = gen;
      consume_optional_comma(); continue;
    }
    if (match_identifier("approval"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      std::string a = previous().lexeme;
      if (a != "manual" && a != "auto")
        error("Invalid approval '" + a + "'. Expected: manual, auto");
      block.approval = a;
      consume_optional_comma(); continue;
    }
    error("Unexpected field in auto_model: " + peek().lexeme);
  }

  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return block;
}

SelfHealBlock Parser::parse_self_heal_block()
{
  SelfHealBlock block;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (match_identifier("enabled"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::True) && !match(TokenType::False)) error("Expected bool");
      block.enabled = (previous().type == TokenType::True);
      consume_optional_comma(); continue;
    }
    if (match_identifier("capabilities") || match(TokenType::Capabilities))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      SelfHealCapabilities caps;
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match_identifier("auto_retry"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::LeftBrace)) error("Expected '{'");
          while (!check(TokenType::RightBrace) && !is_at_end())
          {
            if (match_identifier("max_attempts"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::Number)) error("Expected number");
              caps.auto_retry_max = static_cast<int>(
                std::strtod(previous().lexeme.c_str(), nullptr));
              consume_optional_comma(); continue;
            }
            if (match_identifier("backoff"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::String)) error("Expected string");
              caps.auto_retry_backoff = previous().lexeme;
              consume_optional_comma(); continue;
            }
            error("Unexpected field in auto_retry");
          }
          if (!match(TokenType::RightBrace)) error("Expected '}'");
          consume_optional_comma(); continue;
        }
        if (match_identifier("auto_scale"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::LeftBrace)) error("Expected '{'");
          while (!check(TokenType::RightBrace) && !is_at_end())
          {
            if (match_identifier("scale_factor"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::Number)) error("Expected number");
              caps.auto_scale_factor = std::strtod(previous().lexeme.c_str(), nullptr);
              consume_optional_comma(); continue;
            }
            if (match_identifier("on"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::String)) error("Expected string");
              consume_optional_comma(); continue;
            }
            error("Unexpected field in auto_scale");
          }
          if (!match(TokenType::RightBrace)) error("Expected '}'");
          consume_optional_comma(); continue;
        }
        if (match_identifier("schema_migration"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::LeftBrace)) error("Expected '{'");
          while (!check(TokenType::RightBrace) && !is_at_end())
          {
            if (match_identifier("approval"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::String)) error("Expected string");
              caps.schema_migration_approval = previous().lexeme;
              consume_optional_comma(); continue;
            }
            if (match_identifier("on"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::String)) error("Expected string");
              consume_optional_comma(); continue;
            }
            error("Unexpected field in schema_migration");
          }
          if (!match(TokenType::RightBrace)) error("Expected '}'");
          consume_optional_comma(); continue;
        }
        if (match_identifier("data_patching"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::LeftBrace)) error("Expected '{'");
          while (!check(TokenType::RightBrace) && !is_at_end())
          {
            if (match_identifier("strategy"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::String)) error("Expected string");
              caps.data_patching_strategy = previous().lexeme;
              consume_optional_comma(); continue;
            }
            if (match_identifier("on"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::String)) error("Expected string");
              consume_optional_comma(); continue;
            }
            error("Unexpected field in data_patching");
          }
          if (!match(TokenType::RightBrace)) error("Expected '}'");
          consume_optional_comma(); continue;
        }
        if (match_identifier("fallback_source"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::LeftBrace)) error("Expected '{'");
          while (!check(TokenType::RightBrace) && !is_at_end())
          {
            if (match_identifier("use"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::Identifier)) error("Expected source name");
              caps.fallback_source = IdentifierRef{previous().lexeme, span_from_token(previous())};
              consume_optional_comma(); continue;
            }
            if (match_identifier("on"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::String)) error("Expected string");
              consume_optional_comma(); continue;
            }
            error("Unexpected field in fallback_source");
          }
          if (!match(TokenType::RightBrace)) error("Expected '}'");
          consume_optional_comma(); continue;
        }
        if (match_identifier("circuit_breaker"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::LeftBrace)) error("Expected '{'");
          while (!check(TokenType::RightBrace) && !is_at_end())
          {
            if (match_identifier("threshold"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::Number)) error("Expected number");
              caps.circuit_breaker_threshold = static_cast<int>(
                std::strtod(previous().lexeme.c_str(), nullptr));
              consume_optional_comma(); continue;
            }
            if (match_identifier("on"))
            {
              if (!match(TokenType::Colon)) error("Expected ':'");
              if (!match(TokenType::String)) error("Expected string");
              consume_optional_comma(); continue;
            }
            error("Unexpected field in circuit_breaker");
          }
          if (!match(TokenType::RightBrace)) error("Expected '}'");
          consume_optional_comma(); continue;
        }
        error("Unexpected capability in self_heal");
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      block.capabilities = caps;
      consume_optional_comma(); continue;
    }
    if (match_identifier("learning"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      SelfHealLearning learning;
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match_identifier("record_incidents"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::True) && !match(TokenType::False)) error("Expected bool");
          learning.record_incidents = (previous().type == TokenType::True);
          consume_optional_comma(); continue;
        }
        if (match_identifier("incident_store"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string");
          learning.incident_store = previous().lexeme;
          consume_optional_comma(); continue;
        }
        if (match_identifier("improve_from_history"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::True) && !match(TokenType::False)) error("Expected bool");
          learning.improve_from_history = (previous().type == TokenType::True);
          consume_optional_comma(); continue;
        }
        error("Unexpected field in learning");
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      block.learning = learning;
      consume_optional_comma(); continue;
    }
    if (match_identifier("notification"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      SelfHealNotification notif;
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        if (match_identifier("channel"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string");
          notif.channel = previous().lexeme;
          consume_optional_comma(); continue;
        }
        if (match_identifier("webhook"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          notif.webhook = parse_expression();
          consume_optional_comma(); continue;
        }
        if (match_identifier("escalation"))
        {
          if (!match(TokenType::Colon)) error("Expected ':'");
          notif.escalation = parse_string_list();
          consume_optional_comma(); continue;
        }
        error("Unexpected field in notification");
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      block.notification = std::move(notif);
      consume_optional_comma(); continue;
    }
    error("Unexpected field in self_heal block");
  }

  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return block;
}

StmtPtr Parser::parse_etl_agent(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected etl agent name");
  const auto name = previous().lexeme;

  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  ETLAgentDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  bool has_provider = false, has_model = false;
  bool has_sources = false, has_warehouse = false;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);  // optional comma between fields

    // --- Shared agent fields ---
    if (match(TokenType::Provider) || match_identifier("provider"))
    {
      if (has_provider) error("Duplicate 'provider'");
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected provider string");
      decl.provider = previous().lexeme;
      has_provider = true; consume_optional_comma(); continue;
    }
    if (match(TokenType::Model) || match_identifier("model"))
    {
      if (has_model) error("Duplicate 'model'");
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected model string");
      decl.model = previous().lexeme;
      has_model = true; consume_optional_comma(); continue;
    }
    if (match(TokenType::System) || match_identifier("system"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected system prompt string");
      decl.system = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Temperature) || match_identifier("temperature"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Number)) error("Expected number for temperature");
      decl.temperature = std::strtod(previous().lexeme.c_str(), nullptr);
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Endpoint) || match_identifier("endpoint"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected endpoint string");
      decl.endpoint = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::ApiKeyEnv) || match_identifier("api_key_env"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected api_key_env string");
      decl.api_key_env = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Skills) || match_identifier("skills"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.skills = parse_identifier_list();
      consume_optional_comma(); continue;
    }
    if (match_identifier("connected_knowledge"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.connected_knowledge = parse_identifier_list();
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Guards) || match_identifier("guardchains"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.guardchains = parse_identifier_list();
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Policy) || match_identifier("policy"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected policy identifier");
      decl.policy = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Budget) || match_identifier("budget"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected budget name");
      decl.budget = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma(); continue;
    }
    if (match_identifier("env") || match(TokenType::Env))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected env identifier");
      decl.env = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma(); continue;
    }

    // --- Inherited DataAgent fields ---
    if (match(TokenType::Sources) || match_identifier("sources"))
    {
      if (has_sources) error("Duplicate 'sources'");
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.sources = parse_identifier_list();
      has_sources = true; consume_optional_comma(); continue;
    }
    if (match_identifier("sinks"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.sinks = parse_identifier_list();
      consume_optional_comma(); continue;
    }
    if (match_identifier("quality"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected quality name");
      decl.quality = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma(); continue;
    }
    if (match_identifier("governance"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected governance name");
      decl.governance = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma(); continue;
    }
    if (match_identifier("lineage"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::True) && !match(TokenType::False))
        error("Expected bool");
      decl.lineage = (previous().type == TokenType::True);
      consume_optional_comma(); continue;
    }
    if (match_identifier("catalog"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected catalog name");
      decl.catalog = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma(); continue;
    }
    if (match_identifier("compute"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.compute = parse_data_agent_compute_block();
      consume_optional_comma(); continue;
    }
    if (match_identifier("role"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected role string");
      decl.role = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("purpose"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected purpose string");
      decl.purpose = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("autonomy"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected autonomy string");
      decl.autonomy = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("pipeline"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.pipeline = parse_pipeline_block();
      consume_optional_comma(); continue;
    }

    // --- ETL agent-specific fields ---
    if (match(TokenType::Warehouse) || match_identifier("warehouse"))
    {
      if (has_warehouse) error("Duplicate 'warehouse'");
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected warehouse compute name");
      decl.warehouse = IdentifierRef{previous().lexeme, span_from_token(previous())};
      has_warehouse = true; consume_optional_comma(); continue;
    }
    if (match_identifier("model_type"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected model_type string");
      std::string mt = previous().lexeme;
      if (mt != "star" && mt != "snowflake" && mt != "data_vault" && mt != "wide" && mt != "custom")
        error("Invalid model_type '" + mt + "'. Expected: star, snowflake, data_vault, wide, custom");
      decl.model_type = mt;
      consume_optional_comma(); continue;
    }
    if (match_identifier("layers"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.layers = parse_layers_block();
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Semantic) || match_identifier("semantic"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected semantic layer name");
      decl.semantic = IdentifierRef{previous().lexeme, span_from_token(previous())};
      consume_optional_comma(); continue;
    }
    if (match_identifier("incremental"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.incremental = parse_incremental_block();
      consume_optional_comma(); continue;
    }
    if (match_identifier("self_heal"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      // Two forms: self_heal: true/false OR self_heal: { ... }
      if (match(TokenType::True) || match(TokenType::False))
      {
        decl.self_heal_flag = (previous().type == TokenType::True);
      }
      else if (check(TokenType::LeftBrace))
      {
        decl.self_heal_block = parse_self_heal_block();
      }
      else
      {
        error("Expected true/false or block for self_heal");
      }
      consume_optional_comma(); continue;
    }
    if (match_identifier("on_failure"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected on_failure string");
      std::string of = previous().lexeme;
      if (of != "retry" && of != "fallback" && of != "alert" && of != "auto_fix")
        error("Invalid on_failure '" + of + "'. Expected: retry, fallback, alert, auto_fix");
      decl.on_failure = of;
      consume_optional_comma(); continue;
    }
    if (match_identifier("auto_model"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.auto_model = parse_auto_model_block();
      consume_optional_comma(); continue;
    }

    // FIELD REJECTION: claw-agent-only
    if (match_identifier("idle_reset_minutes") || match_identifier("compaction") ||
        match_identifier("channels") || match_identifier("lanes") ||
        match_identifier("semantic_memory"))
      error("'" + previous().lexeme + "' is not valid in an etl agent (claw agent only)");

    // FIELD REJECTION: forge-agent-only
    if (match_identifier("verify") || match_identifier("checkpoint") ||
        match_identifier("loop"))
      error("'" + previous().lexeme + "' is not valid in an etl agent (forge agent only)");

    error("Unexpected field in etl agent: " + peek().lexeme);
  }

  if (!match(TokenType::RightBrace)) error("Expected '}'");

  if (!has_provider) error("etl agent '" + name + "' missing 'provider'");
  if (!has_model) error("etl agent '" + name + "' missing 'model'");
  if (!has_sources) error("etl agent '" + name + "' requires at least one source");
  if (!has_warehouse) error("etl agent '" + name + "' requires a 'warehouse' compute reference");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

// ============================================================================
// v0.9.2: Migration Agent parsing
// ============================================================================

MigrationStrategy Parser::parse_migration_strategy()
{
  if (!match(TokenType::String)) error("Expected strategy string");
  auto s = previous().lexeme;
  if (s == "lift_and_shift") return MigrationStrategy::LIFT_AND_SHIFT;
  if (s == "re_platform") return MigrationStrategy::RE_PLATFORM;
  if (s == "re_architecture") return MigrationStrategy::RE_ARCHITECTURE;
  error("Invalid migration strategy: " + s);
  return MigrationStrategy::RE_PLATFORM;
}

CDCConfig Parser::parse_cdc_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for cdc block");
  CDCConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "mechanism") { if (!match(TokenType::String)) error("Expected string"); cfg.mechanism = previous().lexeme; }
    else if (fname == "tool") { if (!match(TokenType::String)) error("Expected string"); cfg.tool = previous().lexeme; }
    else if (fname == "lag_threshold") { if (!match(TokenType::String)) error("Expected string"); cfg.lag_threshold = previous().lexeme; }
    else if (fname == "lag_critical") { if (!match(TokenType::String)) error("Expected string"); cfg.lag_critical = previous().lexeme; }
    else error("Unknown cdc field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after cdc block");
  return cfg;
}

WaveConfig Parser::parse_wave_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for waves block");
  WaveConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "mode") { if (!match(TokenType::String)) error("Expected string"); cfg.mode = previous().lexeme; }
    else if (fname == "max_tables_per_wave") { if (!match(TokenType::Number)) error("Expected number"); cfg.max_tables_per_wave = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else if (fname == "max_parallel_extractions") { if (!match(TokenType::Number)) error("Expected number"); cfg.max_parallel_extractions = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else error("Unknown waves field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after waves block");
  return cfg;
}

MovementConfig Parser::parse_movement_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for movement block");
  MovementConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "strategy")
    {
      if (!match(TokenType::String)) error("Expected string");
      auto s = previous().lexeme;
      if (s == "full_dump") cfg.strategy = DataMovementStrategy::FULL_DUMP;
      else if (s == "incremental") cfg.strategy = DataMovementStrategy::INCREMENTAL;
      else if (s == "parallel_run") cfg.strategy = DataMovementStrategy::PARALLEL_RUN;
      else if (s == "trickle") cfg.strategy = DataMovementStrategy::TRICKLE;
      else if (s == "blue_green") cfg.strategy = DataMovementStrategy::BLUE_GREEN;
      else error("Invalid movement strategy: " + s);
    }
    else if (fname == "cdc") { cfg.cdc = parse_cdc_config(); }
    else if (fname == "extraction_threads") { if (!match(TokenType::Number)) error("Expected number"); cfg.extraction_threads = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else if (fname == "load_threads") { if (!match(TokenType::Number)) error("Expected number"); cfg.load_threads = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else if (fname == "partition_strategy") { if (!match(TokenType::String)) error("Expected string"); cfg.partition_strategy = previous().lexeme; }
    else if (fname == "staging_format") { if (!match(TokenType::String)) error("Expected string"); cfg.staging_format = previous().lexeme; }
    else if (fname == "checkpoint_interval") { if (!match(TokenType::String)) error("Expected string"); cfg.checkpoint_interval = previous().lexeme; }
    else error("Unknown movement field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after movement block");
  return cfg;
}

SchemaTranslationConfig::PlatformSpecific Parser::parse_platform_specific()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for platform-specific block");
  SchemaTranslationConfig::PlatformSpecific ps;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "empty_string_handling") { if (!match(TokenType::String)) error("Expected string"); ps.empty_string_handling = previous().lexeme; }
    else if (fname == "date_to_timestamp") { if (match(TokenType::True)) ps.date_to_timestamp = true; else if (match(TokenType::False)) ps.date_to_timestamp = false; else error("Expected bool"); }
    else if (fname == "clob_threshold") { if (!match(TokenType::String)) error("Expected string"); ps.clob_threshold = previous().lexeme; }
    else if (fname == "number_no_precision") { if (!match(TokenType::String)) error("Expected string"); ps.number_no_precision = previous().lexeme; }
    else error("Unknown platform-specific field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after platform-specific block");
  return ps;
}

SchemaTranslationConfig Parser::parse_schema_translation_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for schema_translation block");
  SchemaTranslationConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "type_mapping") { if (!match(TokenType::String)) error("Expected string"); cfg.type_mapping = previous().lexeme; }
    else if (fname == "stored_procedures") { if (!match(TokenType::String)) error("Expected string"); cfg.stored_procedures = previous().lexeme; }
    else if (fname == "views") { if (!match(TokenType::String)) error("Expected string"); cfg.views = previous().lexeme; }
    else if (fname == "materialized_views") { if (!match(TokenType::String)) error("Expected string"); cfg.materialized_views = previous().lexeme; }
    else if (fname == "indexes") { if (!match(TokenType::String)) error("Expected string"); cfg.indexes = previous().lexeme; }
    else if (fname == "sequences") { if (!match(TokenType::String)) error("Expected string"); cfg.sequences = previous().lexeme; }
    else if (fname == "oracle_specific") { cfg.oracle_specific = parse_platform_specific(); }
    else if (fname == "teradata_specific") { cfg.teradata_specific = parse_platform_specific(); }
    else error("Unknown schema_translation field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after schema_translation block");
  return cfg;
}

ReconciliationConfig Parser::parse_reconciliation_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for reconciliation block");
  ReconciliationConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    auto parse_b = [&]() -> bool {
      if (match(TokenType::True)) return true;
      if (match(TokenType::False)) return false;
      error("Expected bool"); return false;
    };
    if (fname == "row_counts") cfg.row_counts = parse_b();
    else if (fname == "column_aggregates") cfg.column_aggregates = parse_b();
    else if (fname == "hash_comparison") { if (!match(TokenType::String)) error("Expected string"); cfg.hash_comparison = previous().lexeme; }
    else if (fname == "statistical_distribution") { if (!match(TokenType::String)) error("Expected string"); cfg.statistical_distribution = previous().lexeme; }
    else if (fname == "boundary_values") cfg.boundary_values = parse_b();
    else if (fname == "golden_queries") cfg.golden_queries = parse_b();
    else if (fname == "referential_integrity") cfg.referential_integrity = parse_b();
    else error("Unknown reconciliation field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after reconciliation block");
  return cfg;
}

ToleranceConfig Parser::parse_tolerance_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for tolerances block");
  ToleranceConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "financial_columns") { if (!match(TokenType::String)) error("Expected string"); cfg.financial_columns = previous().lexeme; }
    else if (fname == "floating_point") { if (!match(TokenType::Number)) error("Expected number"); cfg.floating_point = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (fname == "timestamp_precision") { if (!match(TokenType::String)) error("Expected string"); cfg.timestamp_precision = previous().lexeme; }
    else error("Unknown tolerances field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after tolerances block");
  return cfg;
}

ValidationConfig Parser::parse_validation_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for validation block");
  ValidationConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "mode") { if (!match(TokenType::String)) error("Expected string"); cfg.mode = previous().lexeme; }
    else if (fname == "reconciliation") { cfg.reconciliation = parse_reconciliation_config(); }
    else if (fname == "tolerances") { cfg.tolerances = parse_tolerance_config(); }
    else if (fname == "golden_queries") { cfg.golden_queries = parse_string_list(); }
    else if (fname == "continuous")
    {
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        auto cf = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (cf == "enabled") { if (match(TokenType::True)) cfg.continuous_enabled = true; else if (match(TokenType::False)) cfg.continuous_enabled = false; else error("Expected bool"); }
        else if (cf == "interval") { if (!match(TokenType::String)) error("Expected string"); cfg.continuous_interval = previous().lexeme; }
        else error("Unknown continuous field: " + cf);
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
    }
    else error("Unknown validation field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after validation block");
  return cfg;
}

RollbackConfig Parser::parse_rollback_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for rollback block");
  RollbackConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "window") { if (!match(TokenType::String)) error("Expected string"); cfg.window = previous().lexeme; }
    else if (fname == "auto_trigger") { if (match(TokenType::True)) cfg.auto_trigger = true; else if (match(TokenType::False)) cfg.auto_trigger = false; else error("Expected bool"); }
    else if (fname == "trigger_conditions") { cfg.trigger_conditions = parse_string_list(); }
    else error("Unknown rollback field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after rollback block");
  return cfg;
}

CutoverConfig Parser::parse_cutover_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for cutover block");
  CutoverConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier() && !match(TokenType::Cutover)) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "strategy")
    {
      if (!match(TokenType::String)) error("Expected cutover strategy string");
      auto s = previous().lexeme;
      if (s == "big_bang") cfg.strategy = CutoverStrategy::BIG_BANG;
      else if (s == "blue_green") cfg.strategy = CutoverStrategy::BLUE_GREEN;
      else if (s == "canary") cfg.strategy = CutoverStrategy::CANARY;
      else if (s == "trickle") cfg.strategy = CutoverStrategy::TRICKLE;
      else error("Invalid cutover strategy: " + s);
    }
    else if (fname == "rollback") { cfg.rollback = parse_rollback_config(); }
    else error("Unknown cutover field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after cutover block");
  return cfg;
}

SelfHealGuardrails Parser::parse_guardrails_block()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for guardrails block");
  SelfHealGuardrails g;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    auto parse_b = [&]() -> bool {
      if (match(TokenType::True)) return true;
      if (match(TokenType::False)) return false;
      error("Expected bool"); return false;
    };
    if (fname == "max_auto_fix_rows") { if (!match(TokenType::Number)) error("Expected number"); g.max_auto_fix_rows = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else if (fname == "max_auto_fix_percentage") { if (!match(TokenType::Number)) error("Expected number"); g.max_auto_fix_percentage = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (fname == "max_retries_per_table") { if (!match(TokenType::Number)) error("Expected number"); g.max_retries_per_table = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else if (fname == "require_dry_run") g.require_dry_run = parse_b();
    else if (fname == "audit_all_remediations") g.audit_all_remediations = parse_b();
    else error("Unknown guardrails field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after guardrails block");
  return g;
}

SelfHealMigrationConfig Parser::parse_self_heal_migration_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for self_heal block");
  SelfHealMigrationConfig cfg;
  auto parse_b = [&]() -> bool {
    if (match(TokenType::True)) return true;
    if (match(TokenType::False)) return false;
    error("Expected bool"); return false;
  };
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "enabled") cfg.enabled = parse_b();
    else if (fname == "missing_rows") cfg.missing_rows = parse_b();
    else if (fname == "duplicate_rows") cfg.duplicate_rows = parse_b();
    else if (fname == "type_conversion_errors") cfg.type_conversion_errors = parse_b();
    else if (fname == "network_failures") cfg.network_failures = parse_b();
    else if (fname == "checkpoint_resume") cfg.checkpoint_resume = parse_b();
    else if (fname == "auto_remediate")
    {
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        auto af = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (af == "missing_rows") cfg.missing_rows = parse_b();
        else if (af == "duplicate_rows") cfg.duplicate_rows = parse_b();
        else if (af == "type_conversion_errors") cfg.type_conversion_errors = parse_b();
        else if (af == "network_failures") cfg.network_failures = parse_b();
        else if (af == "checkpoint_resume") cfg.checkpoint_resume = parse_b();
        else error("Unknown auto_remediate field: " + af);
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
    }
    else if (fname == "guardrails") { cfg.guardrails = parse_guardrails_block(); }
    else error("Unknown self_heal field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after self_heal block");
  return cfg;
}

AssessmentConfig Parser::parse_assessment_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for assessment block");
  AssessmentConfig cfg;
  auto parse_b = [&]() -> bool {
    if (match(TokenType::True)) return true;
    if (match(TokenType::False)) return false;
    error("Expected bool"); return false;
  };
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier() && !match(TokenType::Assessment)) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "auto_discover" || fname == "discover") cfg.auto_discover = parse_b();
    else if (fname == "profile_data" || fname == "profile") cfg.profile_data = parse_b();
    else if (fname == "risk_analysis") cfg.risk_analysis = parse_b();
    else if (fname == "report_format") { if (!match(TokenType::String)) error("Expected string"); cfg.report_format = previous().lexeme; }
    else error("Unknown assessment field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after assessment block");
  return cfg;
}

GovernanceMigrationConfig Parser::parse_governance_migration_block()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for governance block");
  GovernanceMigrationConfig cfg;
  auto parse_b = [&]() -> bool {
    if (match(TokenType::True)) return true;
    if (match(TokenType::False)) return false;
    error("Expected bool"); return false;
  };
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (fname == "preserve_classification") cfg.preserve_classification = parse_b();
    else if (fname == "pii_detection") cfg.pii_detection = parse_b();
    else if (fname == "staging_region") { if (!match(TokenType::String)) error("Expected string"); cfg.staging_region = previous().lexeme; }
    else if (fname == "target_region") { if (!match(TokenType::String)) error("Expected string"); cfg.target_region = previous().lexeme; }
    else if (fname == "log_all_sql") cfg.log_all_sql = parse_b();
    else if (fname == "log_all_data_movement") cfg.log_all_data_movement = parse_b();
    else if (fname == "audit_retention") { if (!match(TokenType::String)) error("Expected string"); cfg.audit_retention = previous().lexeme; }
    else if (fname == "classification")
    {
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        auto cf = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (cf == "preserve") cfg.preserve_classification = parse_b();
        else if (cf == "pii_detection") cfg.pii_detection = parse_b();
        else error("Unknown classification field: " + cf);
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
    }
    else if (fname == "residency")
    {
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        auto rf = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (rf == "staging") { if (!match(TokenType::String)) error("Expected string"); cfg.staging_region = previous().lexeme; }
        else if (rf == "target") { if (!match(TokenType::String)) error("Expected string"); cfg.target_region = previous().lexeme; }
        else error("Unknown residency field: " + rf);
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
    }
    else if (fname == "audit")
    {
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        auto af = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (af == "log_all_sql") cfg.log_all_sql = parse_b();
        else if (af == "log_all_data_movement") cfg.log_all_data_movement = parse_b();
        else if (af == "retention") { if (!match(TokenType::String)) error("Expected string"); cfg.audit_retention = previous().lexeme; }
        else error("Unknown audit field: " + af);
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
    }
    else error("Unknown governance field: " + fname);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after governance block");
  return cfg;
}

StmtPtr Parser::parse_migration_agent(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  // 'agent' already consumed by dispatch in parse_declaration()
  if (!match(TokenType::Identifier)) error("Expected migration agent name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  MigrationAgentDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  bool has_source = false, has_target = false;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);

    // Match field names — some are reserved keywords, some are identifiers
    if (match(TokenType::Provider) || match_identifier("provider"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected provider string");
      decl.provider = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Model) || match_identifier("model"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected model string");
      decl.model = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::System) || match_identifier("system"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected system prompt string");
      decl.system_prompt = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Temperature) || match_identifier("temperature"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Number)) error("Expected number for temperature");
      decl.temperature = std::strtod(previous().lexeme.c_str(), nullptr);
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Budget) || match_identifier("budget"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected budget name");
      decl.budget = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Skills) || match_identifier("skills"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected skill name");
        decl.skills.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("role"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.role = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("purpose"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.purpose = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("autonomy"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.autonomy = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("agent_md"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string path");
      decl.agent_md = previous().lexeme;
      consume_optional_comma(); continue;
    }
    // Migration-specific fields
    if (match(TokenType::Sources) || match_identifier("source"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected source name");
      decl.source = previous().lexeme;
      has_source = true;
      consume_optional_comma(); continue;
    }
    if (match_identifier("target"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected target name");
      decl.target = previous().lexeme;
      has_target = true;
      consume_optional_comma(); continue;
    }
    if (match_identifier("staging"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected staging name");
      decl.staging = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("strategy"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.strategy = parse_migration_strategy();
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Waves))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.waves = parse_wave_config();
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Movement))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.movement = parse_movement_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("schema_translation"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.schema_translation = parse_schema_translation_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("validation"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.validation = parse_validation_config();
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Cutover))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.cutover = parse_cutover_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("self_heal"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.self_heal = parse_self_heal_migration_config();
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Assessment))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.assessment = parse_assessment_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("governance"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.governance = parse_governance_migration_block();
      consume_optional_comma(); continue;
    }
    if (match_identifier("pipeline"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.pipeline = parse_pipeline_block();
      consume_optional_comma(); continue;
    }

    error("Unknown migration agent field: " + peek().lexeme);
  }

  if (!match(TokenType::RightBrace)) error("Expected '}' after migration agent body");

  if (!has_source) error("migration agent '" + name + "' missing 'source'");
  if (!has_target) error("migration agent '" + name + "' missing 'target'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

// ============================================================================
// v0.9.3: DataOps Agent parser functions
// ============================================================================

StmtPtr Parser::parse_scheduler_decl(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected scheduler name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  SchedulerDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("type") || match(TokenType::Type))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected scheduler type string");
      auto t = previous().lexeme;
      if (t == "airflow") decl.sched_type = SchedulerType::AIRFLOW;
      else if (t == "controlm") decl.sched_type = SchedulerType::CONTROLM;
      else if (t == "cron") decl.sched_type = SchedulerType::CRON;
      else if (t == "databricks") decl.sched_type = SchedulerType::DATABRICKS;
      else if (t == "snowflake") decl.sched_type = SchedulerType::SNOWFLAKE_TASKS;
      else if (t == "dbt") decl.sched_type = SchedulerType::DBT;
      else if (t == "glue") decl.sched_type = SchedulerType::GLUE;
      else if (t == "adf") decl.sched_type = SchedulerType::ADF;
      else if (t == "informatica") decl.sched_type = SchedulerType::INFORMATICA;
      else if (t == "luigi") decl.sched_type = SchedulerType::LUIGI;
      else error("Unknown scheduler type: " + t);
      consume_optional_comma(); continue;
    }
    if (match_identifier("connection"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected connection string or env()");
      decl.connection = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("credentials"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected credentials");
      decl.credentials = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("poll_interval"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected poll interval string");
      decl.poll_interval = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("dag_filter") || match_identifier("folder_filter") ||
        match_identifier("workspace_filter") || match_identifier("database_filter"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.filters = parse_string_list();
      consume_optional_comma(); continue;
    }
    if (match_identifier("timezone"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected timezone string");
      decl.timezone = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("datacenter"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected datacenter string");
      decl.datacenter = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("host"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected host string");
      decl.host = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("user_filter"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.filters = parse_string_list();
      consume_optional_comma(); continue;
    }
    error("Unknown scheduler field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after scheduler body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

AuditColumnMap Parser::parse_audit_column_map()
{
  AuditColumnMap map;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");

    if (f == "job_id") { if (!match(TokenType::String)) error("Expected string"); map.job_id = previous().lexeme; }
    else if (f == "timestamp") { if (!match(TokenType::String)) error("Expected string"); map.timestamp = previous().lexeme; }
    else if (f == "status") { if (!match(TokenType::String)) error("Expected string"); map.status = previous().lexeme; }
    else if (f == "status_values")
    {
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected status category");
        auto cat = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        auto vals = parse_string_list();
        if (cat == "success") map.status_values.success = vals;
        else if (cat == "failure") map.status_values.failure = vals;
        else if (cat == "running") map.status_values.running = vals;
        else if (cat == "skipped") map.status_values.skipped = vals;
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
    }
    else if (f == "end_time") { if (!match(TokenType::String)) error("Expected string"); map.end_time = previous().lexeme; }
    else if (f == "target_table") { if (!match(TokenType::String)) error("Expected string"); map.target_table = previous().lexeme; }
    else if (f == "load_type") { if (!match(TokenType::String)) error("Expected string"); map.load_type = previous().lexeme; }
    else if (f == "rows_in") { if (!match(TokenType::String)) error("Expected string"); map.rows_in = previous().lexeme; }
    else if (f == "rows_out") { if (!match(TokenType::String)) error("Expected string"); map.rows_out = previous().lexeme; }
    else if (f == "rows_inserted") { if (!match(TokenType::String)) error("Expected string"); map.rows_inserted = previous().lexeme; }
    else if (f == "rows_updated") { if (!match(TokenType::String)) error("Expected string"); map.rows_updated = previous().lexeme; }
    else if (f == "rows_deleted") { if (!match(TokenType::String)) error("Expected string"); map.rows_deleted = previous().lexeme; }
    else if (f == "rows_rejected") { if (!match(TokenType::String)) error("Expected string"); map.rows_rejected = previous().lexeme; }
    else if (f == "error") { if (!match(TokenType::String)) error("Expected string"); map.error = previous().lexeme; }
    else if (f == "duration") { if (!match(TokenType::String)) error("Expected string"); map.duration = previous().lexeme; }
    else if (f == "duration_unit") { if (!match(TokenType::String)) error("Expected string"); map.duration_unit = previous().lexeme; }
    else if (f == "severity") { if (!match(TokenType::String)) error("Expected string"); map.severity = previous().lexeme; }
    else if (f == "rule_name") { if (!match(TokenType::String)) error("Expected string"); map.rule_name = previous().lexeme; }
    else if (f == "expected") { if (!match(TokenType::String)) error("Expected string"); }
    else if (f == "actual") { if (!match(TokenType::String)) error("Expected string"); }
    else error("Unknown column_map field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return map;
}

AuditAnomalyConfig Parser::parse_audit_anomaly_config()
{
  AuditAnomalyConfig cfg;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "row_count_drop") { if (!match(TokenType::Number)) error("Expected number"); cfg.row_count_drop = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "row_count_spike") { if (!match(TokenType::Number)) error("Expected number"); cfg.row_count_spike = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "duration_spike") { if (!match(TokenType::Number)) error("Expected number"); cfg.duration_spike = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "failure_rate") { if (!match(TokenType::Number)) error("Expected number"); cfg.failure_rate = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "zero_rows_consecutive") { if (!match(TokenType::Number)) error("Expected number"); cfg.zero_rows_consecutive = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else error("Unknown anomaly field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

StmtPtr Parser::parse_audit_table_decl(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected audit_table name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  AuditTableDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match(TokenType::Sources) || match_identifier("source"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected source name");
      decl.source_ref = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("table"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected table name string");
      decl.table_name = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("column_map"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.column_map = parse_audit_column_map();
      consume_optional_comma(); continue;
    }
    if (match_identifier("poll_interval"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected poll interval string");
      decl.poll_interval = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("lookback_window"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected lookback window string");
      decl.lookback_window = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("retention_analysis"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected retention string");
      decl.retention_analysis = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("anomalies"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.anomalies = parse_audit_anomaly_config();
      consume_optional_comma(); continue;
    }
    error("Unknown audit_table field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after audit_table body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

LogAlertConfig Parser::parse_log_alert_config()
{
  LogAlertConfig cfg;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "query_timeout") { if (match(TokenType::True)) cfg.query_timeout = true; else if (match(TokenType::False)) cfg.query_timeout = false; else error("Expected bool"); }
    else if (f == "warehouse_credit_spike") { if (!match(TokenType::Number)) error("Expected number"); cfg.warehouse_credit_spike = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "failed_logins") { if (!match(TokenType::Number)) error("Expected number"); cfg.failed_logins = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else if (f == "long_running_queries") { if (!match(TokenType::String)) error("Expected string"); cfg.long_running_queries = previous().lexeme; }
    else if (f == "full_table_scans") { if (match(TokenType::True)) cfg.full_table_scans = true; else if (match(TokenType::False)) cfg.full_table_scans = false; else error("Expected bool"); }
    else if (f == "queued_queries") { if (!match(TokenType::Number)) error("Expected number"); cfg.queued_queries = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else if (f == "ora_errors") { cfg.ora_errors = parse_string_list(); }
    else if (f == "tablespace_usage") { if (!match(TokenType::Number)) error("Expected number"); cfg.tablespace_usage = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "redo_log_switches") { if (!match(TokenType::Number)) error("Expected number"); cfg.redo_log_switches = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else if (f == "dead_tuples_ratio") { if (!match(TokenType::Number)) error("Expected number"); cfg.dead_tuples_ratio = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "lock_waits") { if (!match(TokenType::String)) error("Expected string"); cfg.lock_waits = previous().lexeme; }
    else if (f == "connection_usage") { if (!match(TokenType::Number)) error("Expected number"); cfg.connection_usage = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "replication_lag") { if (!match(TokenType::String)) error("Expected string"); cfg.replication_lag = previous().lexeme; }
    else if (f == "slow_queries") { if (!match(TokenType::String)) error("Expected string"); cfg.slow_queries = previous().lexeme; }
    else if (f == "oom_errors") { if (match(TokenType::True)) cfg.oom_errors = true; else if (match(TokenType::False)) cfg.oom_errors = false; else error("Expected bool"); }
    else if (f == "shuffle_spill") { if (!match(TokenType::String)) error("Expected string"); cfg.shuffle_spill = previous().lexeme; }
    else if (f == "stage_failures") { if (!match(TokenType::Number)) error("Expected number"); cfg.stage_failures = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else if (f == "executor_lost") { if (match(TokenType::True)) cfg.executor_lost = true; else if (match(TokenType::False)) cfg.executor_lost = false; else error("Expected bool"); }
    else if (f == "skewed_partitions") { if (!match(TokenType::Number)) error("Expected number"); cfg.skewed_partitions = std::strtod(previous().lexeme.c_str(), nullptr); }
    else error("Unknown log alert field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

StmtPtr Parser::parse_log_source_decl(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected log_source name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  LogSourceDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("type") || match(TokenType::Type))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected log source type string");
      auto t = previous().lexeme;
      if (t == "snowflake") decl.log_type = LogSourceType::SNOWFLAKE;
      else if (t == "oracle") decl.log_type = LogSourceType::ORACLE;
      else if (t == "postgres") decl.log_type = LogSourceType::POSTGRES;
      else if (t == "mysql") decl.log_type = LogSourceType::MYSQL;
      else if (t == "sqlserver") decl.log_type = LogSourceType::SQLSERVER;
      else if (t == "spark") decl.log_type = LogSourceType::SPARK;
      else if (t == "redshift") decl.log_type = LogSourceType::REDSHIFT;
      else if (t == "bigquery") decl.log_type = LogSourceType::BIGQUERY;
      else if (t == "kafka") decl.log_type = LogSourceType::KAFKA;
      else error("Unknown log source type: " + t);
      consume_optional_comma(); continue;
    }
    if (match_identifier("connection")) { if (!match(TokenType::Colon)) error("Expected ':'"); if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected connection"); decl.connection = previous().lexeme; consume_optional_comma(); continue; }
    if (match_identifier("credentials")) { if (!match(TokenType::Colon)) error("Expected ':'"); if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected credentials"); decl.credentials = previous().lexeme; consume_optional_comma(); continue; }
    if (match_identifier("views")) { if (!match(TokenType::Colon)) error("Expected ':'"); decl.views = parse_string_list(); consume_optional_comma(); continue; }
    if (match_identifier("log_file")) { if (!match(TokenType::Colon)) error("Expected ':'"); if (!match(TokenType::String)) error("Expected string"); decl.log_file = previous().lexeme; consume_optional_comma(); continue; }
    if (match_identifier("log_format")) { if (!match(TokenType::Colon)) error("Expected ':'"); if (!match(TokenType::String)) error("Expected string"); decl.log_format = previous().lexeme; consume_optional_comma(); continue; }
    if (match_identifier("poll_interval")) { if (!match(TokenType::Colon)) error("Expected ':'"); if (!match(TokenType::String)) error("Expected string"); decl.poll_interval = previous().lexeme; consume_optional_comma(); continue; }
    if (match_identifier("lookback_window")) { if (!match(TokenType::Colon)) error("Expected ':'"); if (!match(TokenType::String)) error("Expected string"); decl.lookback_window = previous().lexeme; consume_optional_comma(); continue; }
    if (match_identifier("alerts")) { if (!match(TokenType::Colon)) error("Expected ':'"); decl.alerts = parse_log_alert_config(); consume_optional_comma(); continue; }
    error("Unknown log_source field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after log_source body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

PlatformHealthConfig Parser::parse_platform_health_config()
{
  PlatformHealthConfig cfg;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "storage_growth") { if (match(TokenType::True)) cfg.storage_growth = true; else { match(TokenType::False); } }
    else if (f == "partition_health") { if (match(TokenType::True)) cfg.partition_health = true; else { match(TokenType::False); } }
    else if (f == "file_format_consistency") { if (match(TokenType::True)) cfg.file_format_consistency = true; else { match(TokenType::False); } }
    else if (f == "warehouse_utilization") { if (match(TokenType::True)) cfg.warehouse_utilization = true; else { match(TokenType::False); } }
    else if (f == "clustering_health") { if (match(TokenType::True)) cfg.clustering_health = true; else { match(TokenType::False); } }
    else if (f == "time_travel_usage") { if (match(TokenType::True)) cfg.time_travel_usage = true; else { match(TokenType::False); } }
    else if (f == "row_count_baseline") { if (match(TokenType::True)) cfg.row_count_baseline = true; else { match(TokenType::False); } }
    else if (f == "schema_drift") { if (match(TokenType::True)) cfg.schema_drift = true; else { match(TokenType::False); } }
    else if (f == "consumer_query_patterns") { if (match(TokenType::True)) cfg.consumer_query_patterns = true; else { match(TokenType::False); } }
    else if (f == "stale_data") { if (!match(TokenType::String)) error("Expected string"); cfg.stale_data = previous().lexeme; }
    else if (f == "query_performance")
    {
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier)) error("Expected field name");
        auto pf = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::String)) error("Expected string");
        if (pf == "p50_threshold") cfg.query_perf_p50 = previous().lexeme;
        else if (pf == "p95_threshold") cfg.query_perf_p95 = previous().lexeme;
        else if (pf == "p99_threshold") cfg.query_perf_p99 = previous().lexeme;
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
    }
    else if (f == "freshness")
    {
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::String)) error("Expected pattern string");
        auto pattern = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::String)) error("Expected threshold string");
        cfg.freshness[pattern] = previous().lexeme;
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
    }
    else error("Unknown health_checks field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

PlatformFinOpsConfig Parser::parse_platform_finops_config()
{
  PlatformFinOpsConfig cfg;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "daily_budget") { if (!match(TokenType::Number)) error("Expected number"); cfg.daily_budget = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "auto_suspend_idle") { if (!match(TokenType::String)) error("Expected string"); cfg.auto_suspend_idle = previous().lexeme; }
    else if (f == "auto_kill_queries") { if (!match(TokenType::String)) error("Expected string"); cfg.auto_kill_queries = previous().lexeme; }
    else if (f == "cost_anomaly_threshold") { if (!match(TokenType::Number)) error("Expected number"); cfg.cost_anomaly_threshold = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "warehouse_budgets")
    {
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::String)) error("Expected warehouse name string");
        auto wh = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::Number)) error("Expected budget number");
        cfg.warehouse_budgets[wh] = std::strtod(previous().lexeme.c_str(), nullptr);
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
    }
    else error("Unknown finops field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

StmtPtr Parser::parse_platform_decl(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected platform name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  PlatformDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("type") || match(TokenType::Type))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected platform type string");
      auto t = previous().lexeme;
      if (t == "snowflake") decl.plat_type = PlatformType::SNOWFLAKE;
      else if (t == "s3") decl.plat_type = PlatformType::S3;
      else if (t == "adls") decl.plat_type = PlatformType::ADLS;
      else if (t == "gcs") decl.plat_type = PlatformType::GCS;
      else if (t == "hdfs") decl.plat_type = PlatformType::HDFS;
      else if (t == "redshift") decl.plat_type = PlatformType::REDSHIFT;
      else if (t == "bigquery") decl.plat_type = PlatformType::BIGQUERY;
      else if (t == "databricks") decl.plat_type = PlatformType::DATABRICKS;
      else error("Unknown platform type: " + t);
      consume_optional_comma(); continue;
    }
    if (match_identifier("connection")) { if (!match(TokenType::Colon)) error("Expected ':'"); if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected connection"); decl.connection = previous().lexeme; consume_optional_comma(); continue; }
    if (match_identifier("credentials")) { if (!match(TokenType::Colon)) error("Expected ':'"); if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected credentials"); decl.credentials = previous().lexeme; consume_optional_comma(); continue; }
    if (match_identifier("database")) { if (!match(TokenType::Colon)) error("Expected ':'"); if (!match(TokenType::String)) error("Expected string"); decl.database = previous().lexeme; consume_optional_comma(); continue; }
    if (match_identifier("health_checks")) { if (!match(TokenType::Colon)) error("Expected ':'"); decl.health_checks = parse_platform_health_config(); consume_optional_comma(); continue; }
    if (match_identifier("finops")) { if (!match(TokenType::Colon)) error("Expected ':'"); decl.finops = parse_platform_finops_config(); consume_optional_comma(); continue; }
    error("Unknown platform field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after platform body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

std::vector<SeverityLevel> Parser::parse_severity_levels()
{
  std::vector<SeverityLevel> levels;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected severity level name");
    SeverityLevel level;
    level.level_name = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (!match(TokenType::LeftBrace)) error("Expected '{'");
    while (!check(TokenType::RightBrace) && !is_at_end())
    {
      match(TokenType::Comma);
      if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
      auto f = previous().lexeme;
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (f == "conditions") level.conditions = parse_string_list();
      else if (f == "response") { if (!match(TokenType::String)) error("Expected string"); level.response = previous().lexeme; }
      else if (f == "escalation") { if (!match(TokenType::String)) error("Expected string"); level.escalation = previous().lexeme; }
      else if (f == "channels") level.channels = parse_string_list();
      else error("Unknown severity field: " + f);
      consume_optional_comma();
    }
    if (!match(TokenType::RightBrace)) error("Expected '}'");
    levels.push_back(std::move(level));
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return levels;
}

AutoHealConfig Parser::parse_auto_heal_config()
{
  AutoHealConfig cfg;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else { match(TokenType::False); cfg.enabled = false; } }
    else if (f == "max_auto_retries") { if (!match(TokenType::Number)) error("Expected number"); cfg.max_auto_retries = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
    else if (f == "retry_backoff") { if (!match(TokenType::String)) error("Expected string"); cfg.retry_backoff = previous().lexeme; }
    else if (f == "retry_initial_wait") { if (!match(TokenType::String)) error("Expected string"); cfg.retry_initial_wait = previous().lexeme; }
    else if (f == "allowed_actions") cfg.allowed_actions = parse_string_list();
    else if (f == "requires_approval") cfg.requires_approval = parse_string_list();
    else if (f == "guardrails")
    {
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier)) error("Expected field name");
        auto gf = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (gf == "max_cost_per_action") { if (!match(TokenType::Number)) error("Expected number"); cfg.guardrails.max_cost_per_action = std::strtod(previous().lexeme.c_str(), nullptr); }
        else if (gf == "max_retries_per_hour") { if (!match(TokenType::Number)) error("Expected number"); cfg.guardrails.max_retries_per_hour = static_cast<int>(std::strtod(previous().lexeme.c_str(), nullptr)); }
        else if (gf == "no_actions_during") cfg.guardrails.no_actions_during = parse_string_list();
        else if (gf == "require_dry_run") { if (match(TokenType::True)) cfg.guardrails.require_dry_run = true; else { match(TokenType::False); } }
        else error("Unknown guardrail field: " + gf);
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
    }
    else error("Unknown auto_heal field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

StmtPtr Parser::parse_incident_policy_decl(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected incident_policy name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  IncidentPolicyDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("severity"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.severity_levels = parse_severity_levels();
      consume_optional_comma(); continue;
    }
    if (match_identifier("auto_heal"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.auto_heal = parse_auto_heal_config();
      consume_optional_comma(); continue;
    }
    error("Unknown incident_policy field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after incident_policy body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

CorrelationScope Parser::parse_correlation_scope()
{
  CorrelationScope scope;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "schedulers") { if (!match(TokenType::LeftBracket)) error("Expected '['"); while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); scope.schedulers.push_back(previous().lexeme); match(TokenType::Comma); } if (!match(TokenType::RightBracket)) error("Expected ']'"); }
    else if (f == "audit_tables") { if (!match(TokenType::LeftBracket)) error("Expected '['"); while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); scope.audit_tables.push_back(previous().lexeme); match(TokenType::Comma); } if (!match(TokenType::RightBracket)) error("Expected ']'"); }
    else if (f == "log_sources") { if (!match(TokenType::LeftBracket)) error("Expected '['"); while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); scope.log_sources.push_back(previous().lexeme); match(TokenType::Comma); } if (!match(TokenType::RightBracket)) error("Expected ']'"); }
    else if (f == "job_pattern") scope.job_pattern = parse_string_list();
    else if (f == "table_pattern") scope.table_pattern = parse_string_list();
    else error("Unknown scope field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return scope;
}

CorrelationSLAConfig Parser::parse_correlation_sla_config()
{
  CorrelationSLAConfig cfg;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "deadline") { if (!match(TokenType::String)) error("Expected string"); cfg.deadline = previous().lexeme; }
    else if (f == "timezone") { if (!match(TokenType::String)) error("Expected string"); cfg.timezone = previous().lexeme; }
    else if (f == "business_days_only") { if (match(TokenType::True)) cfg.business_days_only = true; else { match(TokenType::False); } }
    else if (f == "escalation") { if (!match(TokenType::String)) error("Expected string"); cfg.escalation = previous().lexeme; }
    else error("Unknown SLA field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

StmtPtr Parser::parse_correlation_decl(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected correlation name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  CorrelationDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("scope")) { if (!match(TokenType::Colon)) error("Expected ':'"); decl.scope = parse_correlation_scope(); consume_optional_comma(); continue; }
    if (match_identifier("time_window")) { if (!match(TokenType::Colon)) error("Expected ':'"); if (!match(TokenType::String)) error("Expected string"); decl.time_window = previous().lexeme; consume_optional_comma(); continue; }
    if (match_identifier("sla")) { if (!match(TokenType::Colon)) error("Expected ':'"); decl.sla = parse_correlation_sla_config(); consume_optional_comma(); continue; }
    if (match_identifier("dependencies"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::String)) error("Expected job name string");
        auto job = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        decl.dependencies[job] = parse_string_list();
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      consume_optional_comma(); continue;
    }
    error("Unknown correlation field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after correlation body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_dataops_agent(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  // 'agent' already consumed by dispatch in parse_declaration()
  if (!match(TokenType::Identifier)) error("Expected dataops agent name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  DataOpsAgentDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);

    // Common agent fields
    if (match(TokenType::Provider) || match_identifier("provider"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected provider string");
      decl.provider = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Model) || match_identifier("model"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected model string");
      decl.model = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::System) || match_identifier("system"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected system prompt string");
      decl.system_prompt = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Temperature) || match_identifier("temperature"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Number)) error("Expected number for temperature");
      decl.temperature = std::strtod(previous().lexeme.c_str(), nullptr);
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Budget) || match_identifier("budget"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected budget name");
      decl.budget = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Endpoint) || match_identifier("endpoint"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected endpoint string");
      decl.endpoint = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("api_key_env"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.api_key_env = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Skills) || match_identifier("skills"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected skill name");
        decl.skills.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("guardchains"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected guardchain name");
        decl.guardchains.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("agent_md"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string path");
      decl.agent_md = previous().lexeme;
      consume_optional_comma(); continue;
    }

    // DataOps-specific reference lists
    if (match_identifier("platforms"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected platform name");
        decl.platforms.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("schedulers"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected scheduler name");
        decl.schedulers.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("audit_tables"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected audit_table name");
        decl.audit_tables.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("log_sources"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected log_source name");
        decl.log_sources.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("correlations"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected correlation name");
        decl.correlations.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("incident_policy"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected incident_policy name");
      decl.incident_policy = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("mode"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected mode string");
      auto m = previous().lexeme;
      if (m == "continuous") decl.mode = DataOpsMode::CONTINUOUS;
      else if (m == "scheduled") decl.mode = DataOpsMode::SCHEDULED;
      else if (m == "on_demand") decl.mode = DataOpsMode::ON_DEMAND;
      else error("Unknown mode: " + m + " (expected continuous, scheduled, or on_demand)");
      consume_optional_comma(); continue;
    }
    if (match_identifier("reports"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier)) error("Expected report type");
        auto rt = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::LeftBrace)) error("Expected '{'");
        DataOpsReportConfig rcfg;
        while (!check(TokenType::RightBrace) && !is_at_end())
        {
          match(TokenType::Comma);
          if (!match(TokenType::Identifier) && !match(TokenType::Channel) && !match_keyword_as_identifier()) error("Expected field");
          auto rf = previous().lexeme;
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (!match(TokenType::String)) error("Expected string");
          if (rf == "time") rcfg.time = previous().lexeme;
          else if (rf == "day") rcfg.day = previous().lexeme;
          else if (rf == "frequency") rcfg.frequency = previous().lexeme;
          else if (rf == "channel") rcfg.channel = previous().lexeme;
          consume_optional_comma();
        }
        if (!match(TokenType::RightBrace)) error("Expected '}'");
        if (rt == "daily_digest") decl.daily_digest = rcfg;
        else if (rt == "weekly_summary") decl.weekly_summary = rcfg;
        else if (rt == "cost_report") decl.cost_report = rcfg;
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      consume_optional_comma(); continue;
    }

    // Reject fields from other agent types
    auto next_field = peek().lexeme;
    if (next_field == "warehouse" || next_field == "model_type" || next_field == "layers" ||
        next_field == "semantic" || next_field == "incremental" || next_field == "auto_model" ||
        next_field == "source" || next_field == "target" || next_field == "staging" ||
        next_field == "strategy" || next_field == "waves" || next_field == "movement" ||
        next_field == "schema_translation" || next_field == "cutover" ||
        next_field == "forge_type" || next_field == "claw_type" ||
        next_field == "sinks" || next_field == "schema_ref")
    {
      error("Field '" + next_field + "' is not valid in 'dataops agent' context.");
    }

    error("Unknown dataops agent field: " + peek().lexeme);
  }

  if (!match(TokenType::RightBrace)) error("Expected '}' after dataops agent body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

// ═══════════════════════════════════════════════════════════════
// v0.9.4 Governance Agent Parser Functions
// ═══════════════════════════════════════════════════════════════

// Helper: consume a nested brace block as raw JSON string
std::string Parser::consume_nested_block_as_json()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{' for nested block");
  int depth = 1;
  std::string json_str = "{";
  while (depth > 0 && !is_at_end())
  {
    if (check(TokenType::LeftBrace))
    {
      depth++;
      json_str += "{";
      advance();
    }
    else if (check(TokenType::RightBrace))
    {
      depth--;
      if (depth > 0) json_str += "}";
      advance();
    }
    else
    {
      auto& t = peek();
      if (t.type == TokenType::String)
      {
        json_str += "\"" + t.lexeme + "\"";
      }
      else if (t.type == TokenType::Number || t.type == TokenType::True || t.type == TokenType::False)
      {
        json_str += t.lexeme;
      }
      else if (t.type == TokenType::Colon)
      {
        json_str += ":";
      }
      else if (t.type == TokenType::Comma)
      {
        json_str += ",";
      }
      else if (t.type == TokenType::LeftBracket)
      {
        json_str += "[";
      }
      else if (t.type == TokenType::RightBracket)
      {
        json_str += "]";
      }
      else if (t.type == TokenType::Identifier)
      {
        json_str += "\"" + t.lexeme + "\"";
      }
      else if (t.type == TokenType::Nil)
      {
        json_str += "null";
      }
      else
      {
        json_str += "\"" + t.lexeme + "\"";
      }
      advance();
    }
  }
  json_str += "}";
  return json_str;
}

// --- catalog_source ---
StmtPtr Parser::parse_gov_catalog_source(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected catalog source name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  GovCatalogSourceDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("type") || match(TokenType::Type))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected catalog source type string");
      auto t = previous().lexeme;
      if (t == "snowflake") decl.source_type = CatalogSourceType::SNOWFLAKE;
      else if (t == "oracle") decl.source_type = CatalogSourceType::ORACLE;
      else if (t == "postgres") decl.source_type = CatalogSourceType::POSTGRES;
      else if (t == "mysql") decl.source_type = CatalogSourceType::MYSQL;
      else if (t == "sqlserver") decl.source_type = CatalogSourceType::SQLSERVER;
      else if (t == "redshift") decl.source_type = CatalogSourceType::REDSHIFT;
      else if (t == "bigquery") decl.source_type = CatalogSourceType::BIGQUERY;
      else if (t == "databricks") decl.source_type = CatalogSourceType::DATABRICKS;
      else if (t == "s3") decl.source_type = CatalogSourceType::S3;
      else if (t == "adls") decl.source_type = CatalogSourceType::ADLS;
      else if (t == "gcs") decl.source_type = CatalogSourceType::GCS;
      else if (t == "hdfs") decl.source_type = CatalogSourceType::HDFS;
      else if (t == "collibra") decl.source_type = CatalogSourceType::COLLIBRA;
      else if (t == "atlas") decl.source_type = CatalogSourceType::ATLAS;
      else if (t == "alation") decl.source_type = CatalogSourceType::ALATION;
      else if (t == "purview") decl.source_type = CatalogSourceType::PURVIEW;
      else if (t == "informatica") decl.source_type = CatalogSourceType::INFORMATICA;
      else if (t == "atlan") decl.source_type = CatalogSourceType::ATLAN;
      else error("Unknown catalog source type: " + t);
      consume_optional_comma(); continue;
    }
    if (match_identifier("connection"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected connection string");
      decl.connection = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("credentials"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected credentials");
      decl.credentials = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("databases"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.databases = parse_string_list();
      consume_optional_comma(); continue;
    }
    if (match_identifier("prefixes"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.prefixes = parse_string_list();
      consume_optional_comma(); continue;
    }
    if (match_identifier("scan_interval"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected scan interval string");
      decl.scan_interval = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("include_views"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (match(TokenType::True)) decl.include_views = true;
      else if (match(TokenType::False)) decl.include_views = false;
      else error("Expected true or false");
      consume_optional_comma(); continue;
    }
    if (match_identifier("include_stages"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (match(TokenType::True)) decl.include_stages = true;
      else if (match(TokenType::False)) decl.include_stages = false;
      else error("Expected true or false");
      consume_optional_comma(); continue;
    }
    if (match_identifier("detect_formats"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (match(TokenType::True)) decl.detect_formats = true;
      else if (match(TokenType::False)) decl.detect_formats = false;
      else error("Expected true or false");
      consume_optional_comma(); continue;
    }
    if (match_identifier("exclude_patterns"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.exclude_patterns = parse_string_list();
      consume_optional_comma(); continue;
    }
    if (match_identifier("sync_mode"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected sync mode string");
      auto m = previous().lexeme;
      if (m == "push") decl.sync_mode = SyncMode::PUSH;
      else if (m == "pull") decl.sync_mode = SyncMode::PULL;
      else if (m == "bidirectional") decl.sync_mode = SyncMode::BIDIRECTIONAL;
      else error("sync_mode must be 'push', 'pull', or 'bidirectional'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("sync_interval"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected sync interval string");
      decl.sync_interval = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("conflict_resolution"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected conflict resolution string");
      auto cr = previous().lexeme;
      if (cr == "agent_wins") decl.conflict_resolution = ConflictResolution::AGENT_WINS;
      else if (cr == "external_wins") decl.conflict_resolution = ConflictResolution::EXTERNAL_WINS;
      else if (cr == "manual") decl.conflict_resolution = ConflictResolution::MANUAL;
      else error("conflict_resolution must be 'agent_wins', 'external_wins', or 'manual'");
      consume_optional_comma(); continue;
    }
    error("Unknown field in catalog_source: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after catalog_source body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

// --- Sub-block parsers ---

AutoDocumentConfig Parser::parse_auto_document_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  AutoDocumentConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match(TokenType::Provider) && !match(TokenType::Model) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "provider") { if (!match(TokenType::String)) error("Expected string"); cfg.provider = previous().lexeme; }
    else if (f == "model") { if (!match(TokenType::String)) error("Expected string"); cfg.model = previous().lexeme; }
    else if (f == "require_review") { if (match(TokenType::True)) cfg.require_review = true; else if (match(TokenType::False)) cfg.require_review = false; else error("Expected bool"); }
    else if (f == "review_channel") { if (!match(TokenType::String)) error("Expected string"); cfg.review_channel = previous().lexeme; }
    else error("Unknown auto_document field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

OwnershipConfig Parser::parse_ownership_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  OwnershipConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "auto_assign") { if (match(TokenType::True)) cfg.auto_assign = true; else if (match(TokenType::False)) cfg.auto_assign = false; else error("Expected bool"); }
    else if (f == "default_domain") { if (!match(TokenType::String)) error("Expected string"); cfg.default_domain = previous().lexeme; }
    else if (f == "require_owner") { if (match(TokenType::True)) cfg.require_owner = true; else if (match(TokenType::False)) cfg.require_owner = false; else error("Expected bool"); }
    else error("Unknown ownership field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

AutoClassifyConfig Parser::parse_auto_classify_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  AutoClassifyConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match(TokenType::Provider) && !match(TokenType::Model) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "provider") { if (!match(TokenType::String)) error("Expected string"); cfg.provider = previous().lexeme; }
    else if (f == "model") { if (!match(TokenType::String)) error("Expected string"); cfg.model = previous().lexeme; }
    else if (f == "patterns") { cfg.patterns_json = consume_nested_block_as_json(); }
    else if (f == "semantic") { cfg.semantic = parse_semantic_classify_config(); }
    else if (f == "drift_detection") { cfg.drift_detection = parse_drift_detection_config(); }
    else error("Unknown auto_classify field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

SemanticClassifyConfig Parser::parse_semantic_classify_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  SemanticClassifyConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "column_name_analysis") { if (match(TokenType::True)) cfg.column_name_analysis = true; else if (match(TokenType::False)) cfg.column_name_analysis = false; else error("Expected bool"); }
    else if (f == "sample_value_analysis") { if (match(TokenType::True)) cfg.sample_value_analysis = true; else if (match(TokenType::False)) cfg.sample_value_analysis = false; else error("Expected bool"); }
    else if (f == "cross_column_inference") { if (match(TokenType::True)) cfg.cross_column_inference = true; else if (match(TokenType::False)) cfg.cross_column_inference = false; else error("Expected bool"); }
    else if (f == "confidence_threshold") { if (!match(TokenType::Number)) error("Expected number"); cfg.confidence_threshold = std::strtod(previous().lexeme.c_str(), nullptr); }
    else error("Unknown semantic classify field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

DriftDetectionConfig Parser::parse_drift_detection_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  DriftDetectionConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "scan_interval") { if (!match(TokenType::String)) error("Expected string"); cfg.scan_interval = previous().lexeme; }
    else if (f == "alert_on_new_pii") { if (match(TokenType::True)) cfg.alert_on_new_pii = true; else if (match(TokenType::False)) cfg.alert_on_new_pii = false; else error("Expected bool"); }
    else if (f == "alert_on_reclassification") { if (match(TokenType::True)) cfg.alert_on_reclassification = true; else if (match(TokenType::False)) cfg.alert_on_reclassification = false; else error("Expected bool"); }
    else error("Unknown drift_detection field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

TagPropagationConfig Parser::parse_tag_propagation_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  TagPropagationConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "lineage_based") { if (match(TokenType::True)) cfg.lineage_based = true; else if (match(TokenType::False)) cfg.lineage_based = false; else error("Expected bool"); }
    else if (f == "inheritance") { if (!match(TokenType::String)) error("Expected string"); cfg.inheritance = previous().lexeme; }
    else error("Unknown propagation field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

AutoSuggestConfig Parser::parse_auto_suggest_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  AutoSuggestConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match(TokenType::Provider) && !match(TokenType::Model) && !match(TokenType::Sources) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "provider") { if (!match(TokenType::String)) error("Expected string"); cfg.provider = previous().lexeme; }
    else if (f == "model") { if (!match(TokenType::String)) error("Expected string"); cfg.model = previous().lexeme; }
    else if (f == "sources") { cfg.sources = parse_string_list(); }
    else if (f == "require_approval") { if (match(TokenType::True)) cfg.require_approval = true; else if (match(TokenType::False)) cfg.require_approval = false; else error("Expected bool"); }
    else if (f == "approval_channel") { if (!match(TokenType::String)) error("Expected string"); cfg.approval_channel = previous().lexeme; }
    else error("Unknown auto_suggest field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

SynonymDetectionConfig Parser::parse_synonym_detection_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  SynonymDetectionConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "confidence_threshold") { if (!match(TokenType::Number)) error("Expected number"); cfg.confidence_threshold = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "cross_database") { if (match(TokenType::True)) cfg.cross_database = true; else if (match(TokenType::False)) cfg.cross_database = false; else error("Expected bool"); }
    else error("Unknown synonym_detection field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

AccessReviewConfig Parser::parse_access_review_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  AccessReviewConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "mode") { if (!match(TokenType::String)) error("Expected string"); cfg.mode = previous().lexeme; }
    else if (f == "unused_access_threshold") { if (!match(TokenType::String)) error("Expected string"); cfg.unused_access_threshold = previous().lexeme; }
    else if (f == "excessive_access_detection") { if (match(TokenType::True)) cfg.excessive_access_detection = true; else if (match(TokenType::False)) cfg.excessive_access_detection = false; else error("Expected bool"); }
    else if (f == "service_account_review") { if (!match(TokenType::String)) error("Expected string"); cfg.service_account_review = previous().lexeme; }
    else if (f == "risk_based_prioritization") { if (match(TokenType::True)) cfg.risk_based_prioritization = true; else if (match(TokenType::False)) cfg.risk_based_prioritization = false; else error("Expected bool"); }
    else if (f == "review_channel") { if (!match(TokenType::String)) error("Expected string"); cfg.review_channel = previous().lexeme; }
    else if (f == "auto_revoke_unused") { if (match(TokenType::True)) cfg.auto_revoke_unused = true; else if (match(TokenType::False)) cfg.auto_revoke_unused = false; else error("Expected bool"); }
    else error("Unknown access_review field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

GovProfilingConfig Parser::parse_gov_profiling_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  GovProfilingConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "scan_interval") { if (!match(TokenType::String)) error("Expected string"); cfg.scan_interval = previous().lexeme; }
    else if (f == "sample_size") { if (!match(TokenType::Number)) error("Expected number"); cfg.sample_size = std::stoi(previous().lexeme); }
    else if (f == "full_scan_interval") { if (!match(TokenType::String)) error("Expected string"); cfg.full_scan_interval = previous().lexeme; }
    else if (f == "targets") { cfg.targets = parse_string_list(); }
    else error("Unknown profiling field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

QualityScoringConfig Parser::parse_quality_scoring_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  QualityScoringConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "accuracy_weight") { if (!match(TokenType::Number)) error("Expected number"); cfg.accuracy_weight = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "completeness_weight") { if (!match(TokenType::Number)) error("Expected number"); cfg.completeness_weight = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "consistency_weight") { if (!match(TokenType::Number)) error("Expected number"); cfg.consistency_weight = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "timeliness_weight") { if (!match(TokenType::Number)) error("Expected number"); cfg.timeliness_weight = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "validity_weight") { if (!match(TokenType::Number)) error("Expected number"); cfg.validity_weight = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "uniqueness_weight") { if (!match(TokenType::Number)) error("Expected number"); cfg.uniqueness_weight = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "minimum_score") { if (!match(TokenType::Number)) error("Expected number"); cfg.minimum_score = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "trend_analysis") { if (!match(TokenType::String)) error("Expected string"); cfg.trend_analysis = previous().lexeme; }
    else if (f == "report_channel") { if (!match(TokenType::String)) error("Expected string"); cfg.report_channel = previous().lexeme; }
    else error("Unknown scoring field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

LineageDiscoveryConfig Parser::parse_lineage_discovery_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  LineageDiscoveryConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match(TokenType::Sources) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "sources") { cfg.sources = parse_string_list(); }
    else if (f == "methods") { cfg.methods = parse_string_list(); }
    else if (f == "scan_interval") { if (!match(TokenType::String)) error("Expected string"); cfg.scan_interval = previous().lexeme; }
    else if (f == "depth") {
      if (!match(TokenType::String)) error("Expected string");
      auto d = previous().lexeme;
      if (d == "table") cfg.depth = LineageDepth::TABLE;
      else if (d == "column") cfg.depth = LineageDepth::COLUMN;
      else if (d == "transformation") cfg.depth = LineageDepth::TRANSFORMATION;
      else error("Unknown lineage depth: " + d);
    }
    else error("Unknown lineage_discovery field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

ImpactAnalysisConfig Parser::parse_impact_analysis_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  ImpactAnalysisConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "downstream_depth") { if (!match(TokenType::Number)) error("Expected number"); cfg.downstream_depth = std::stoi(previous().lexeme); }
    else if (f == "include_reports") { if (match(TokenType::True)) cfg.include_reports = true; else if (match(TokenType::False)) cfg.include_reports = false; else error("Expected bool"); }
    else if (f == "include_apis") { if (match(TokenType::True)) cfg.include_apis = true; else if (match(TokenType::False)) cfg.include_apis = false; else error("Expected bool"); }
    else if (f == "include_ml_models") { if (match(TokenType::True)) cfg.include_ml_models = true; else if (match(TokenType::False)) cfg.include_ml_models = false; else error("Expected bool"); }
    else if (f == "notification_on_breaking_change") { if (match(TokenType::True)) cfg.notification_on_breaking_change = true; else if (match(TokenType::False)) cfg.notification_on_breaking_change = false; else error("Expected bool"); }
    else error("Unknown impact_analysis field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

LineageTagPropagationConfig Parser::parse_lineage_tag_propagation_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  LineageTagPropagationConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "direction") { if (!match(TokenType::String)) error("Expected string"); cfg.direction = previous().lexeme; }
    else if (f == "inherit_sensitivity") { if (!match(TokenType::String)) error("Expected string"); cfg.inherit_sensitivity = previous().lexeme; }
    else if (f == "inherit_pii_tags") { if (match(TokenType::True)) cfg.inherit_pii_tags = true; else if (match(TokenType::False)) cfg.inherit_pii_tags = false; else error("Expected bool"); }
    else error("Unknown tag_propagation field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

ComplianceMonitoringConfig Parser::parse_compliance_monitoring_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  ComplianceMonitoringConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "scan_interval") { if (!match(TokenType::String)) error("Expected string"); cfg.scan_interval = previous().lexeme; }
    else if (f == "scoring") { if (match(TokenType::True)) cfg.scoring = true; else if (match(TokenType::False)) cfg.scoring = false; else error("Expected bool"); }
    else if (f == "alert_on_non_compliance") { if (match(TokenType::True)) cfg.alert_on_non_compliance = true; else if (match(TokenType::False)) cfg.alert_on_non_compliance = false; else error("Expected bool"); }
    else if (f == "report_channel") { if (!match(TokenType::String)) error("Expected string"); cfg.report_channel = previous().lexeme; }
    else if (f == "audit_report_schedule") { if (!match(TokenType::String)) error("Expected string"); cfg.audit_report_schedule = previous().lexeme; }
    else error("Unknown compliance monitoring field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

DSARConfig Parser::parse_dsar_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  DSARConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "search_scope") { cfg.search_scope = parse_string_list(); }
    else if (f == "response_format") { if (!match(TokenType::String)) error("Expected string"); cfg.response_format = previous().lexeme; }
    else if (f == "anonymization_on_export") { if (match(TokenType::True)) cfg.anonymization_on_export = true; else if (match(TokenType::False)) cfg.anonymization_on_export = false; else error("Expected bool"); }
    else error("Unknown dsar field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

GovTieringConfig Parser::parse_gov_tiering_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  GovTieringConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "hot_to_warm") { if (!match(TokenType::String)) error("Expected string"); cfg.hot_to_warm = previous().lexeme; }
    else if (f == "warm_to_cold") { if (!match(TokenType::String)) error("Expected string"); cfg.warm_to_cold = previous().lexeme; }
    else if (f == "cold_to_archive") { if (!match(TokenType::String)) error("Expected string"); cfg.cold_to_archive = previous().lexeme; }
    else if (f == "targets") { cfg.targets_json = consume_nested_block_as_json(); }
    else error("Unknown tiering field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

LegalHoldConfig Parser::parse_legal_hold_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  LegalHoldConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "enabled") { if (match(TokenType::True)) cfg.enabled = true; else if (match(TokenType::False)) cfg.enabled = false; else error("Expected bool"); }
    else if (f == "hold_overrides_retention") { if (match(TokenType::True)) cfg.hold_overrides_retention = true; else if (match(TokenType::False)) cfg.hold_overrides_retention = false; else error("Expected bool"); }
    else if (f == "notification_channel") { if (!match(TokenType::String)) error("Expected string"); cfg.notification_channel = previous().lexeme; }
    else error("Unknown legal_hold field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

CostOptimizationConfig Parser::parse_cost_optimization_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  CostOptimizationConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "unused_table_detection") { if (!match(TokenType::String)) error("Expected string"); cfg.unused_table_detection = previous().lexeme; }
    else if (f == "redundant_copy_detection") { if (match(TokenType::True)) cfg.redundant_copy_detection = true; else if (match(TokenType::False)) cfg.redundant_copy_detection = false; else error("Expected bool"); }
    else if (f == "report_channel") { if (!match(TokenType::String)) error("Expected string"); cfg.report_channel = previous().lexeme; }
    else error("Unknown cost_optimization field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

MatchingConfig Parser::parse_matching_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  MatchingConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "strategy") { if (!match(TokenType::String)) error("Expected string"); cfg.strategy = previous().lexeme; }
    else if (f == "fields") { cfg.fields = parse_string_list(); }
    else if (f == "confidence_threshold") { if (!match(TokenType::Number)) error("Expected number"); cfg.confidence_threshold = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "manual_review_threshold") { if (!match(TokenType::Number)) error("Expected number"); cfg.manual_review_threshold = std::strtod(previous().lexeme.c_str(), nullptr); }
    else error("Unknown matching field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

StewardshipConfig Parser::parse_stewardship_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  StewardshipConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "review_queue") { if (!match(TokenType::String)) error("Expected string"); cfg.review_queue = previous().lexeme; }
    else if (f == "auto_merge_above") { if (!match(TokenType::Number)) error("Expected number"); cfg.auto_merge_above = std::strtod(previous().lexeme.c_str(), nullptr); }
    else if (f == "require_approval_below") { if (!match(TokenType::Number)) error("Expected number"); cfg.require_approval_below = std::strtod(previous().lexeme.c_str(), nullptr); }
    else error("Unknown stewardship field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

DataContractConfig Parser::parse_data_contract_config()
{
  if (!match(TokenType::LeftBrace)) error("Expected '{'");
  DataContractConfig cfg;
  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "schema_version") { if (!match(TokenType::String)) error("Expected string"); cfg.schema_version = previous().lexeme; }
    else if (f == "sla") {
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end()) {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
        auto sf = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (sf == "freshness") { if (!match(TokenType::String)) error("Expected string"); cfg.sla.freshness = previous().lexeme; }
        else if (sf == "availability") { if (!match(TokenType::String)) error("Expected string"); cfg.sla.availability = previous().lexeme; }
        else if (sf == "quality_score") { if (!match(TokenType::Number)) error("Expected number"); cfg.sla.quality_score = std::strtod(previous().lexeme.c_str(), nullptr); }
        else error("Unknown SLA field: " + sf);
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
    }
    else if (f == "schema") { cfg.schema_json = consume_nested_block_as_json(); }
    else if (f == "breaking_change_policy") { if (!match(TokenType::String)) error("Expected string"); cfg.breaking_change_policy = previous().lexeme; }
    else if (f == "consumers") { cfg.consumers = parse_string_list(); }
    else error("Unknown contract field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");
  return cfg;
}

GovernanceReportsConfig Parser::parse_governance_reports_config()
{
  GovernanceReportsConfig cfg;
  // Parse as JSON blob and decompose
  std::string json_str = consume_nested_block_as_json();
  cfg.governance_scorecard_json = json_str;  // Store full JSON; decomposition happens at emit time
  return cfg;
}

// --- Top-level governance declaration parsers ---

StmtPtr Parser::parse_gov_catalog(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected catalog name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  GovCatalogDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match(TokenType::Sources) || match_identifier("sources"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected source name");
        decl.sources.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("auto_document"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.auto_document = parse_auto_document_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("staleness_threshold"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.staleness_threshold = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("shadow_dataset_detection"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (match(TokenType::True)) decl.shadow_dataset_detection = true;
      else if (match(TokenType::False)) decl.shadow_dataset_detection = false;
      else error("Expected bool");
      consume_optional_comma(); continue;
    }
    if (match_identifier("ownership"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.ownership = parse_ownership_config();
      consume_optional_comma(); continue;
    }
    error("Unknown gov_catalog field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_glossary(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected glossary name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  GlossaryDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("domains"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.domains = parse_string_list();
      consume_optional_comma(); continue;
    }
    if (match_identifier("auto_suggest"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.auto_suggest = parse_auto_suggest_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("synonym_detection"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.synonym_detection = parse_synonym_detection_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("terms"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.terms_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("external_sync"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.external_sync_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    error("Unknown glossary field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_classification_policy(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected classification policy name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  ClassificationPolicyDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("levels"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected level name");
        auto level_name = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::LeftBrace)) error("Expected '{'");
        ClassificationLevel lvl;
        while (!check(TokenType::RightBrace) && !is_at_end())
        {
          match(TokenType::Comma);
          if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
          auto lf = previous().lexeme;
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (lf == "level") { if (!match(TokenType::Number)) error("Expected number"); lvl.level = std::stoi(previous().lexeme); }
          else if (lf == "controls") { lvl.controls = parse_string_list(); }
          else if (lf == "retention_max") { if (!match(TokenType::String)) error("Expected string"); lvl.retention_max = previous().lexeme; }
          else if (lf == "cross_border") { if (!match(TokenType::String)) error("Expected string"); lvl.cross_border = previous().lexeme; }
          else error("Unknown classification level field: " + lf);
          consume_optional_comma();
        }
        if (!match(TokenType::RightBrace)) error("Expected '}'");
        decl.levels[level_name] = lvl;
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("auto_classify"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.auto_classify = parse_auto_classify_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("propagation"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.propagation = parse_tag_propagation_config();
      consume_optional_comma(); continue;
    }
    error("Unknown classification_policy field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_access_policy(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected access policy name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  AccessPolicyDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match(TokenType::Model) || match_identifier("model"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      auto m = previous().lexeme;
      if (m == "rbac") decl.model = AccessModel::RBAC;
      else if (m == "abac") decl.model = AccessModel::ABAC;
      else if (m == "hybrid_rbac_abac") decl.model = AccessModel::HYBRID_RBAC_ABAC;
      else error("Unknown access model: " + m);
      consume_optional_comma(); continue;
    }
    if (match_identifier("roles"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected role name");
        auto role_name = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::LeftBrace)) error("Expected '{'");
        RoleDefinition role;
        while (!check(TokenType::RightBrace) && !is_at_end())
        {
          match(TokenType::Comma);
          if (!match(TokenType::Identifier) && !match(TokenType::Description) && !match_keyword_as_identifier()) error("Expected field");
          auto rf = previous().lexeme;
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (rf == "description") { if (!match(TokenType::String)) error("Expected string"); role.description = previous().lexeme; }
          else if (rf == "permissions") { role.permissions = parse_string_list(); }
          else if (rf == "databases") { role.databases = parse_string_list(); }
          else if (rf == "masking") { role.masking_json = consume_nested_block_as_json(); }
          else if (rf == "row_level_security") { role.row_level_security_json = consume_nested_block_as_json(); }
          else if (rf == "restrictions") { role.restrictions_json = consume_nested_block_as_json(); }
          else error("Unknown role field: " + rf);
          consume_optional_comma();
        }
        if (!match(TokenType::RightBrace)) error("Expected '}'");
        decl.roles[role_name] = role;
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("attributes"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.attributes_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("access_review"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.access_review = parse_access_review_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("masking_policies"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected masking policy name");
        auto mp_name = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::LeftBrace)) error("Expected '{'");
        MaskingPolicy mp;
        while (!check(TokenType::RightBrace) && !is_at_end())
        {
          match(TokenType::Comma);
          if (!match(TokenType::Identifier) && !match(TokenType::Type) && !match_keyword_as_identifier()) error("Expected field");
          auto mf = previous().lexeme;
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (mf == "type") { if (!match(TokenType::String)) error("Expected string"); mp.mask_type = previous().lexeme; }
          else if (mf == "pattern") { if (!match(TokenType::String)) error("Expected string"); mp.pattern = previous().lexeme; }
          else error("Unknown masking policy field: " + mf);
          consume_optional_comma();
        }
        if (!match(TokenType::RightBrace)) error("Expected '}'");
        decl.masking_policies[mp_name] = mp;
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      consume_optional_comma(); continue;
    }
    error("Unknown access_policy field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_quality_policy(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected quality policy name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  QualityPolicyDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("profiling"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.profiling = parse_gov_profiling_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("rules"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.rules_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("scoring"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.scoring = parse_quality_scoring_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("dataops_integration"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
        auto f = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (f == "create_incident_on_violation") { if (match(TokenType::True)) decl.dataops_integration.create_incident_on_violation = true; else if (match(TokenType::False)) decl.dataops_integration.create_incident_on_violation = false; else error("Expected bool"); }
        else if (f == "minimum_severity") { if (!match(TokenType::String)) error("Expected string"); decl.dataops_integration.minimum_severity = previous().lexeme; }
        else error("Unknown dataops_integration field: " + f);
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      consume_optional_comma(); continue;
    }
    error("Unknown quality_policy field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_lineage_policy(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected lineage policy name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  LineagePolicyDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("auto_discover"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.auto_discover = parse_lineage_discovery_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("impact_analysis"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.impact_analysis = parse_impact_analysis_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("tag_propagation"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.tag_propagation = parse_lineage_tag_propagation_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("external_sync"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.external_sync_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    error("Unknown lineage_policy field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_compliance_policy(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected compliance policy name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  CompliancePolicyDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("regulations"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.regulations = parse_string_list();
      consume_optional_comma(); continue;
    }
    if (match_identifier("gdpr"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.gdpr_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("ccpa"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.ccpa_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("hipaa"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.hipaa_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("bcbs_239"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.bcbs_239_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("monitoring"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.monitoring = parse_compliance_monitoring_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("dsar"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.dsar = parse_dsar_config();
      consume_optional_comma(); continue;
    }
    error("Unknown compliance_policy field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_lifecycle_policy(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected lifecycle policy name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  LifecyclePolicyDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("retention"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBrace)) error("Expected '{'");
      while (!check(TokenType::RightBrace) && !is_at_end())
      {
        match(TokenType::Comma);
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected level name");
        auto level_name = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");
        if (!match(TokenType::LeftBrace)) error("Expected '{'");
        RetentionRule rule;
        while (!check(TokenType::RightBrace) && !is_at_end())
        {
          match(TokenType::Comma);
          if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
          auto rf = previous().lexeme;
          if (!match(TokenType::Colon)) error("Expected ':'");
          if (rf == "max_retention") { if (!match(TokenType::String)) error("Expected string"); rule.max_retention = previous().lexeme; }
          else if (rf == "min_retention") { if (!match(TokenType::String)) error("Expected string"); rule.min_retention = previous().lexeme; }
          else if (rf == "action_on_expiry") { if (!match(TokenType::String)) error("Expected string"); rule.action_on_expiry = previous().lexeme; }
          else if (rf == "archive_tier") { if (!match(TokenType::String)) error("Expected string"); rule.archive_tier = previous().lexeme; }
          else if (rf == "requires_approval") { if (match(TokenType::True)) rule.requires_approval = true; else if (match(TokenType::False)) rule.requires_approval = false; else error("Expected bool"); }
          else error("Unknown retention field: " + rf);
          consume_optional_comma();
        }
        if (!match(TokenType::RightBrace)) error("Expected '}'");
        decl.retention[level_name] = rule;
        consume_optional_comma();
      }
      if (!match(TokenType::RightBrace)) error("Expected '}'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("regulatory_retention"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.regulatory_retention_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("tiering"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.tiering = parse_gov_tiering_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("legal_hold"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.legal_hold = parse_legal_hold_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("cost_optimization"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.cost_optimization = parse_cost_optimization_config();
      consume_optional_comma(); continue;
    }
    error("Unknown lifecycle_policy field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_data_product(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected data product name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  DataProductDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("domain"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.domain = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("owner"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.owner = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Description) || match_identifier("description"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.description = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("contract"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.contract = parse_data_contract_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("quality"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.quality_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("access"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.access_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    error("Unknown data_product field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_contract_policy(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected contract policy name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  ContractPolicyDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field");
    auto f = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':'");
    if (f == "schema_validation_on_deploy") { if (match(TokenType::True)) decl.schema_validation_on_deploy = true; else if (match(TokenType::False)) decl.schema_validation_on_deploy = false; else error("Expected bool"); }
    else if (f == "schema_validation_on_change") { if (match(TokenType::True)) decl.schema_validation_on_change = true; else if (match(TokenType::False)) decl.schema_validation_on_change = false; else error("Expected bool"); }
    else if (f == "breaking_change_detection") { if (match(TokenType::True)) decl.breaking_change_detection = true; else if (match(TokenType::False)) decl.breaking_change_detection = false; else error("Expected bool"); }
    else if (f == "notify_consumers") { if (match(TokenType::True)) decl.notify_consumers = true; else if (match(TokenType::False)) decl.notify_consumers = false; else error("Expected bool"); }
    else if (f == "freshness_check_interval") { if (!match(TokenType::String)) error("Expected string"); decl.freshness_check_interval = previous().lexeme; }
    else if (f == "availability_check_interval") { if (!match(TokenType::String)) error("Expected string"); decl.availability_check_interval = previous().lexeme; }
    else if (f == "quality_check_interval") { if (!match(TokenType::String)) error("Expected string"); decl.quality_check_interval = previous().lexeme; }
    else if (f == "versioning_strategy") { if (!match(TokenType::String)) error("Expected string"); decl.versioning_strategy = previous().lexeme; }
    else if (f == "auto_version_on_schema_change") { if (match(TokenType::True)) decl.auto_version_on_schema_change = true; else if (match(TokenType::False)) decl.auto_version_on_schema_change = false; else error("Expected bool"); }
    else if (f == "require_changelog") { if (match(TokenType::True)) decl.require_changelog = true; else if (match(TokenType::False)) decl.require_changelog = false; else error("Expected bool"); }
    else error("Unknown contract_policy field: " + f);
    consume_optional_comma();
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_master_data(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected master data name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  MasterDataDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("entity"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.entity = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("golden_source"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier) && !match(TokenType::String)) error("Expected source name");
      decl.golden_source = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("contributing_sources"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier) && !match(TokenType::String)) error("Expected source name");
        decl.contributing_sources.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("matching"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.matching = parse_matching_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("survivorship"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.survivorship_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("quality"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.quality_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("stewardship"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.stewardship = parse_stewardship_config();
      consume_optional_comma(); continue;
    }
    error("Unknown master_data field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_gov_external_tool(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected external tool name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  GovExternalToolDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);
    if (match_identifier("type") || match(TokenType::Type))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.tool_type = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("connection"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected string");
      decl.connection = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("credentials"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected string");
      decl.credentials = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("capabilities"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.capabilities_json = consume_nested_block_as_json();
      consume_optional_comma(); continue;
    }
    if (match_identifier("sync_interval"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.sync_interval = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("conflict_resolution"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      auto cr = previous().lexeme;
      if (cr == "agent_wins") decl.conflict_resolution = ConflictResolution::AGENT_WINS;
      else if (cr == "external_wins") decl.conflict_resolution = ConflictResolution::EXTERNAL_WINS;
      else if (cr == "manual") decl.conflict_resolution = ConflictResolution::MANUAL;
      else error("Unknown conflict_resolution: " + cr);
      consume_optional_comma(); continue;
    }
    error("Unknown external_tool field: " + peek().lexeme);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}'");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

// --- governance agent ---
StmtPtr Parser::parse_governance_agent(const Visibility& visibility)
{
  SourceSpan span = span_from_token(previous());
  if (!match(TokenType::Identifier)) error("Expected governance agent name");
  const auto name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{'");

  GovernanceAgentDecl decl;
  decl.visibility = visibility;
  decl.name = name;

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    match(TokenType::Comma);

    // Common agent fields
    if (match(TokenType::Provider) || match_identifier("provider"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected provider string");
      decl.provider = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Model) || match_identifier("model"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected model string");
      decl.model = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::System) || match_identifier("system"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected system prompt string");
      decl.system_prompt = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Temperature) || match_identifier("temperature"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Number)) error("Expected number");
      decl.temperature = std::strtod(previous().lexeme.c_str(), nullptr);
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Budget) || match_identifier("budget"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected budget name");
      decl.budget = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Endpoint) || match_identifier("endpoint"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected endpoint string");
      decl.endpoint = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("api_key_env"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string");
      decl.api_key_env = previous().lexeme;
      consume_optional_comma(); continue;
    }

    // Governance pillar references
    if (match_identifier("catalog"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected catalog name");
      decl.catalog = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("glossary"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected glossary name");
      decl.glossary = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("classification"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected classification policy name");
      decl.classification = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("access_control"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected access policy name");
      decl.access_control = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("quality"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected quality policy name");
      decl.quality = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("lineage"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected lineage policy name");
      decl.lineage = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("compliance"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected compliance policy name");
      decl.compliance = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("lifecycle"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected lifecycle policy name");
      decl.lifecycle = previous().lexeme;
      consume_optional_comma(); continue;
    }

    // Ref lists
    if (match_identifier("external_tools"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected tool name");
        decl.external_tools.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("coordinates_with"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected agent name");
        decl.coordinates_with.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match(TokenType::Skills) || match_identifier("skills"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected skill name");
        decl.skills.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("guardchains"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected guardchain name");
        decl.guardchains.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
      consume_optional_comma(); continue;
    }
    if (match_identifier("reports"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      decl.reports = parse_governance_reports_config();
      consume_optional_comma(); continue;
    }
    if (match_identifier("policy"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::Identifier)) error("Expected policy name");
      decl.policy = previous().lexeme;
      consume_optional_comma(); continue;
    }
    if (match_identifier("agent_md"))
    {
      if (!match(TokenType::Colon)) error("Expected ':'");
      if (!match(TokenType::String)) error("Expected string path");
      decl.agent_md = previous().lexeme;
      consume_optional_comma(); continue;
    }

    // Field rejection for other agent types
    auto next_field = peek().lexeme;
    if (next_field == "warehouse" || next_field == "marts" || next_field == "layers" ||
        next_field == "semantic" || next_field == "auto_model" || next_field == "incremental")
    {
      error("Field '" + next_field + "' is not valid in 'governance agent' context. It belongs to 'etl agent' type.");
    }
    if (next_field == "source" || next_field == "target" || next_field == "staging" ||
        next_field == "waves" || next_field == "movement" || next_field == "schema_translation")
    {
      error("Field '" + next_field + "' is not valid in 'governance agent' context. It belongs to 'migration agent' type.");
    }
    if (next_field == "schedulers" || next_field == "audit_tables" || next_field == "log_sources" ||
        next_field == "platforms" || next_field == "incident_policy" || next_field == "correlations")
    {
      error("Field '" + next_field + "' is not valid in 'governance agent' context. It belongs to 'dataops agent' type.");
    }

    error("Unknown governance agent field: " + peek().lexeme);
  }

  if (!match(TokenType::RightBrace)) error("Expected '}' after governance agent body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = std::move(decl);
  return stmt;
}

// ═══════════════════════════════════════════════════════════════════════
// v0.9.5: Modeling Agent parse functions
// ═══════════════════════════════════════════════════════════════════════

StmtPtr Parser::parse_schema_source_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected schema_source name");
  SchemaSourceDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after schema_source name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in schema_source");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "type") { if (!match(TokenType::String)) error("Expected string"); decl.type_str = previous().lexeme; }
    else if (fname == "connection") { if (!match(TokenType::String)) error("Expected string"); decl.connection = previous().lexeme; }
    else if (fname == "credentials") { if (!match(TokenType::String)) error("Expected string"); decl.credentials = previous().lexeme; }
    else if (fname == "databases") { decl.databases = parse_string_list(); }
    else if (fname == "scan_interval") { if (!match(TokenType::String)) error("Expected string"); decl.scan_interval = previous().lexeme; }
    else if (fname == "include_views") { if (match(TokenType::True)) decl.include_views = true; else if (match(TokenType::False)) decl.include_views = false; else error("Expected bool"); }
    else if (fname == "include_procedures") { if (match(TokenType::True)) decl.include_procedures = true; else if (match(TokenType::False)) decl.include_procedures = false; else error("Expected bool"); }
    else if (fname == "read_constraints") { if (match(TokenType::True)) decl.read_constraints = true; else if (match(TokenType::False)) decl.read_constraints = false; else error("Expected bool"); }
    else if (fname == "read_indexes") { if (match(TokenType::True)) decl.read_indexes = true; else if (match(TokenType::False)) decl.read_indexes = false; else error("Expected bool"); }
    else if (fname == "read_statistics") { if (match(TokenType::True)) decl.read_statistics = true; else if (match(TokenType::False)) decl.read_statistics = false; else error("Expected bool"); }
    else if (fname == "path") { if (!match(TokenType::String)) error("Expected string"); decl.path = previous().lexeme; }
    else if (fname == "format") { if (!match(TokenType::String)) error("Expected string"); decl.format = previous().lexeme; }
    else if (fname == "model_type") { if (!match(TokenType::String)) error("Expected string"); decl.model_type = previous().lexeme; }
    else if (fname == "watch") { if (match(TokenType::True)) decl.watch = true; else if (match(TokenType::False)) decl.watch = false; else error("Expected bool"); }
    else if (fname == "sync_direction") { if (!match(TokenType::String)) error("Expected string"); decl.sync_direction = previous().lexeme; }
    else if (fname == "api_connection") { if (!match(TokenType::String)) error("Expected string"); decl.api_connection = previous().lexeme; }
    else if (fname == "prefixes") { decl.prefixes = parse_string_list(); }
    else if (fname == "sample_size") { if (!match(TokenType::Number)) error("Expected number"); decl.sample_size = static_cast<int>(std::stod(previous().lexeme)); }
    else if (fname == "detect_formats") { if (match(TokenType::True)) decl.detect_formats = true; else if (match(TokenType::False)) decl.detect_formats = false; else error("Expected bool"); }
    else if (fname == "dialect") { if (!match(TokenType::String)) error("Expected string"); decl.dialect = previous().lexeme; }
    else if (fname == "apply_migrations") { if (match(TokenType::True)) decl.apply_migrations = true; else if (match(TokenType::False)) decl.apply_migrations = false; else error("Expected bool"); }
    else if (fname == "project_path") { if (!match(TokenType::String)) error("Expected string"); decl.project_path = previous().lexeme; }
    else if (fname == "manifest_path") { if (!match(TokenType::String)) error("Expected string"); decl.manifest_path = previous().lexeme; }
    else if (fname == "read_sources") { if (match(TokenType::True)) decl.read_sources = true; else if (match(TokenType::False)) decl.read_sources = false; else error("Expected bool"); }
    else if (fname == "read_models") { if (match(TokenType::True)) decl.read_models = true; else if (match(TokenType::False)) decl.read_models = false; else error("Expected bool"); }
    else if (fname == "read_tests") { if (match(TokenType::True)) decl.read_tests = true; else if (match(TokenType::False)) decl.read_tests = false; else error("Expected bool"); }
    else if (fname == "submodel_filter") { decl.submodel_filter = parse_string_list(); }
    else if (fname == "infer_relationships") { decl.infer_relationships_json = consume_nested_block_as_json(); }
    else if (fname == "schema_evolution") { decl.schema_evolution_json = consume_nested_block_as_json(); }
    else { error("Unknown schema_source field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after schema_source body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_er_model_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected er_model name");
  ERModelDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after er_model name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in er_model");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "version") { if (!match(TokenType::String)) error("Expected string"); decl.version = previous().lexeme; }
    else if (fname == "levels") { decl.levels = parse_string_list(); }
    else if (fname == "source") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.source = previous().lexeme; }
    else if (fname == "notation") { decl.notation_json = consume_nested_block_as_json(); }
    else if (fname == "domains") { decl.domains_json = consume_nested_block_as_json(); }
    else if (fname == "relationship_inference") { decl.relationship_inference_json = consume_nested_block_as_json(); }
    else if (fname == "sync") { decl.sync_json = consume_nested_block_as_json(); }
    else { error("Unknown er_model field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after er_model body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_modeling_entity_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected entity name");
  ModelingEntityDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after entity name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in entity");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "domain") { if (!match(TokenType::String)) error("Expected string"); decl.domain = previous().lexeme; }
    else if (fname == "attributes") { decl.attributes_json = consume_nested_block_as_json(); }
    else if (fname == "relationships") { decl.relationships_json = consume_nested_block_as_json(); }
    else if (fname == "glossary_term") { if (!match(TokenType::String)) error("Expected string"); decl.glossary_term = previous().lexeme; }
    else if (fname == "owner") { if (!match(TokenType::String)) error("Expected string"); decl.owner = previous().lexeme; }
    else if (fname == "description") { if (!match(TokenType::String)) error("Expected string"); decl.description = previous().lexeme; }
    else { error("Unknown entity field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after entity body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_dimensional_model_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected dimensional_model name");
  DimensionalModelDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after dimensional_model name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in dimensional_model");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "methodology") { if (!match(TokenType::String)) error("Expected string"); decl.methodology = previous().lexeme; }
    else if (fname == "source") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.source = previous().lexeme; }
    else if (fname == "facts") { decl.facts_json = consume_nested_block_as_json(); }
    else if (fname == "dimensions") { decl.dimensions_json = consume_nested_block_as_json(); }
    else if (fname == "conformed")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected identifier in conformed list");
        decl.conformed.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "target_platform") { if (!match(TokenType::String)) error("Expected string"); decl.target_platform = previous().lexeme; }
    else if (fname == "target_schema") { if (!match(TokenType::String)) error("Expected string"); decl.target_schema = previous().lexeme; }
    else if (fname == "output") { decl.output_json = consume_nested_block_as_json(); }
    else { error("Unknown dimensional_model field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after dimensional_model body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_datamart_v095_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected datamart name");
  DataMartDecl_v095 decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after datamart name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in datamart");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "dimensional_model") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.dimensional_model = previous().lexeme; }
    else if (fname == "purpose") { if (!match(TokenType::String)) error("Expected string"); decl.purpose = previous().lexeme; }
    else if (fname == "owner") { if (!match(TokenType::String)) error("Expected string"); decl.owner = previous().lexeme; }
    else if (fname == "facts")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected identifier in facts list");
        decl.facts.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "dimensions")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected identifier in dimensions list");
        decl.dimensions.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "additional_dimensions") { decl.additional_dimensions_json = consume_nested_block_as_json(); }
    else if (fname == "aggregate_tables") { decl.aggregate_tables_json = consume_nested_block_as_json(); }
    else if (fname == "materialization") { decl.materialization_json = consume_nested_block_as_json(); }
    else if (fname == "row_level_security") { decl.row_level_security_json = consume_nested_block_as_json(); }
    else if (fname == "column_masking") { decl.column_masking_json = consume_nested_block_as_json(); }
    else if (fname == "quality") { decl.quality_json = consume_nested_block_as_json(); }
    else { error("Unknown datamart field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after datamart body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_normalization_analysis_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected normalization_analysis name");
  NormalizationAnalysisDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after normalization_analysis name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in normalization_analysis");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "scope") { decl.scope_json = consume_nested_block_as_json(); }
    else if (fname == "target_nf") { if (!match(TokenType::String)) error("Expected string"); decl.target_nf = previous().lexeme; }
    else if (fname == "fd_discovery") { decl.fd_discovery_json = consume_nested_block_as_json(); }
    else if (fname == "report") { decl.report_json = consume_nested_block_as_json(); }
    else if (fname == "governance") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.governance = previous().lexeme; }
    else if (fname == "on_violation") { if (!match(TokenType::String)) error("Expected string"); decl.on_violation = previous().lexeme; }
    else { error("Unknown normalization_analysis field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after normalization_analysis body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_amendment_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected amendment_config name");
  AmendmentConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after amendment_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in amendment_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "monitor") { decl.monitor_json = consume_nested_block_as_json(); }
    else if (fname == "change_types") { decl.change_types_json = consume_nested_block_as_json(); }
    else if (fname == "impact_scope") { decl.impact_scope_json = consume_nested_block_as_json(); }
    else if (fname == "approval") { decl.approval_json = consume_nested_block_as_json(); }
    else if (fname == "document") { decl.document_json = consume_nested_block_as_json(); }
    else { error("Unknown amendment_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after amendment_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_amendment_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected amendment name");
  AmendmentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after amendment name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in amendment");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "model") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.model = previous().lexeme; }
    else if (fname == "type") { if (!match(TokenType::String)) error("Expected string"); decl.type_str = previous().lexeme; }
    else if (fname == "description") { if (!match(TokenType::String)) error("Expected string"); decl.description = previous().lexeme; }
    else if (fname == "changes") { decl.changes_json = consume_nested_block_as_json(); }
    else if (fname == "auto_analyze") { if (match(TokenType::True)) decl.auto_analyze = true; else if (match(TokenType::False)) decl.auto_analyze = false; else error("Expected bool"); }
    else if (fname == "require_approval") { if (match(TokenType::True)) decl.require_approval = true; else if (match(TokenType::False)) decl.require_approval = false; else error("Expected bool"); }
    else { error("Unknown amendment field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after amendment body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_data_profile_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected data_profile name");
  DataProfileDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after data_profile name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in data_profile");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "sources") { decl.sources_json = consume_nested_block_as_json(); }
    else if (fname == "profiling") { decl.profiling_json = consume_nested_block_as_json(); }
    else if (fname == "output") { decl.output_json = consume_nested_block_as_json(); }
    else { error("Unknown data_profile field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after data_profile body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_modeling_tool_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected modeling_tool name");
  ModelingToolDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after modeling_tool name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in modeling_tool");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "type") { if (!match(TokenType::String)) error("Expected string"); decl.type_str = previous().lexeme; }
    else if (fname == "path") { if (!match(TokenType::String)) error("Expected string"); decl.path = previous().lexeme; }
    else if (fname == "api_url") { if (!match(TokenType::String)) error("Expected string"); decl.api_url = previous().lexeme; }
    else if (fname == "credentials") { if (!match(TokenType::String)) error("Expected string"); decl.credentials = previous().lexeme; }
    else if (fname == "repository") { if (!match(TokenType::String)) error("Expected string"); decl.repository = previous().lexeme; }
    else if (fname == "submodels") { decl.submodels = parse_string_list(); }
    else if (fname == "sync") { decl.sync_json = consume_nested_block_as_json(); }
    else if (fname == "mapping") { decl.mapping_json = consume_nested_block_as_json(); }
    else if (fname == "on_conflict") { decl.on_conflict_json = consume_nested_block_as_json(); }
    else { error("Unknown modeling_tool field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after modeling_tool body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_modeling_agent_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected modeling agent name");
  ModelingAgentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after modeling agent name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in modeling agent");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
    else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
    else if (fname == "endpoint") { if (!match(TokenType::String)) error("Expected string"); decl.endpoint = previous().lexeme; }
    else if (fname == "api_key_env") { if (!match(TokenType::String)) error("Expected string"); decl.api_key_env = previous().lexeme; }
    else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.budget = previous().lexeme; }
    else if (fname == "sources")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected identifier in sources list");
        decl.sources.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "catalog") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.catalog = previous().lexeme; }
    else if (fname == "governance") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.governance = previous().lexeme; }
    else if (fname == "modeling_tools")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected identifier in modeling_tools list");
        decl.modeling_tools.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "capabilities") { decl.capabilities_json = consume_nested_block_as_json(); }
    else if (fname == "coordinates_with")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected identifier in coordinates_with list");
        decl.coordinates_with.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "enrich_from_governance") { if (match(TokenType::True)) decl.enrich_from_governance = true; else if (match(TokenType::False)) decl.enrich_from_governance = false; else error("Expected bool"); }
    else if (fname == "role") { if (!match(TokenType::String)) error("Expected string"); decl.role = previous().lexeme; }
    else if (fname == "purpose") { if (!match(TokenType::String)) error("Expected string"); decl.purpose = previous().lexeme; }
    else if (fname == "jurisdiction") { if (!match(TokenType::String)) error("Expected string"); decl.jurisdiction = previous().lexeme; }
    else if (fname == "autonomy") { if (!match(TokenType::String)) error("Expected string"); decl.autonomy = previous().lexeme; }
    else if (fname == "approval_required") { decl.approval_required = parse_string_list(); }
    else if (fname == "handoffs")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end())
      {
        if (!match(TokenType::Identifier)) error("Expected identifier in handoffs list");
        decl.handoffs.push_back(previous().lexeme);
        match(TokenType::Comma);
      }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else { error("Unknown modeling agent field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after modeling agent body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

// ═══════════════════════════════════════════════════════════════
// v0.9.6 Analyst Agent Parser Functions
// ═══════════════════════════════════════════════════════════════

StmtPtr Parser::parse_sql_connection_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected sql_connection name");
  SQLConnectionDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after sql_connection name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in sql_connection");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "platform") { if (!match(TokenType::String)) error("Expected string"); decl.platform_str = previous().lexeme; }
    else if (fname == "connection") { if (!match(TokenType::String)) error("Expected string"); decl.connection = previous().lexeme; }
    else if (fname == "credentials") { if (!match(TokenType::String)) error("Expected string"); decl.credentials = previous().lexeme; }
    else if (fname == "warehouse") { if (!match(TokenType::String)) error("Expected string"); decl.warehouse = previous().lexeme; }
    else if (fname == "database") { if (!match(TokenType::String)) error("Expected string"); decl.database = previous().lexeme; }
    else if (fname == "schema") { if (!match(TokenType::String)) error("Expected string"); decl.schema = previous().lexeme; }
    else if (fname == "project") { if (!match(TokenType::String)) error("Expected string"); decl.project = previous().lexeme; }
    else if (fname == "dataset") { if (!match(TokenType::String)) error("Expected string"); decl.dataset = previous().lexeme; }
    else if (fname == "catalog") { if (!match(TokenType::String)) error("Expected string"); decl.catalog = previous().lexeme; }
    else if (fname == "cluster") { if (!match(TokenType::String)) error("Expected string"); decl.cluster = previous().lexeme; }
    else if (fname == "timeout") { if (!match(TokenType::Number)) error("Expected number"); decl.timeout = static_cast<int>(std::stod(previous().lexeme)); }
    else if (fname == "max_rows") { if (!match(TokenType::Number)) error("Expected number"); decl.max_rows = static_cast<int>(std::stod(previous().lexeme)); }
    else if (fname == "cost_limit") { if (!match(TokenType::Number)) error("Expected number"); decl.cost_limit = std::stod(previous().lexeme); }
    else if (fname == "queue") { if (!match(TokenType::String)) error("Expected string"); decl.queue = previous().lexeme; }
    else if (fname == "prefer_materialized_views") { if (match(TokenType::True)) decl.prefer_materialized_views = true; else if (match(TokenType::False)) decl.prefer_materialized_views = false; else error("Expected bool"); }
    else if (fname == "use_result_cache") { if (match(TokenType::True)) decl.use_result_cache = true; else if (match(TokenType::False)) decl.use_result_cache = false; else error("Expected bool"); }
    else if (fname == "partition_pruning") { if (match(TokenType::True)) decl.partition_pruning = true; else if (match(TokenType::False)) decl.partition_pruning = false; else error("Expected bool"); }
    else if (fname == "schema_source") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.schema_source = previous().lexeme; }
    else if (fname == "semantic_layer") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.semantic_layer = previous().lexeme; }
    else { error("Unknown sql_connection field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after sql_connection body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_domain_context_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected domain_context name");
  DomainContextDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after domain_context name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in domain_context");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "models")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.models.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "dimensional_models")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.dimensional_models.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "marts")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.marts.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "schema_sources")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.schema_sources.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "glossary") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.glossary = previous().lexeme; }
    else if (fname == "data_products")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.data_products.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "classification") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.classification = previous().lexeme; }
    else if (fname == "access_policy") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.access_policy = previous().lexeme; }
    else if (fname == "semantic_layers")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.semantic_layers.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "query_history") { if (match(TokenType::True)) decl.query_history = true; else if (match(TokenType::False)) decl.query_history = false; else error("Expected bool"); }
    else if (fname == "feedback_loop") { if (match(TokenType::True)) decl.feedback_loop = true; else if (match(TokenType::False)) decl.feedback_loop = false; else error("Expected bool"); }
    else { error("Unknown domain_context field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after domain_context body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_query_template_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected query_template name");
  QueryTemplateDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after query_template name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in query_template");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "description") { if (!match(TokenType::String)) error("Expected string"); decl.description = previous().lexeme; }
    else if (fname == "category") { if (!match(TokenType::String)) error("Expected string"); decl.category = previous().lexeme; }
    else if (fname == "params") { decl.params_json = consume_nested_block_as_json(); }
    else if (fname == "sql") { if (!match(TokenType::String)) error("Expected string"); decl.sql = previous().lexeme; }
    else if (fname == "default_format") { if (!match(TokenType::String)) error("Expected string"); decl.default_format = previous().lexeme; }
    else if (fname == "chart") { decl.chart_json = consume_nested_block_as_json(); }
    else if (fname == "classification") { if (!match(TokenType::String)) error("Expected string"); decl.classification = previous().lexeme; }
    else if (fname == "audit") { if (match(TokenType::True)) decl.audit = true; else if (match(TokenType::False)) decl.audit = false; else error("Expected bool"); }
    else { error("Unknown query_template field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after query_template body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_query_optimizer_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected query_optimizer name");
  QueryOptimizerDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after query_optimizer name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in query_optimizer");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "cost_model") { if (!match(TokenType::String)) error("Expected string"); decl.cost_model = previous().lexeme; }
    else if (fname == "max_cost_per_query") { if (!match(TokenType::Number)) error("Expected number"); decl.max_cost_per_query = std::stod(previous().lexeme); }
    else if (fname == "max_scan_gb") { if (!match(TokenType::Number)) error("Expected number"); decl.max_scan_gb = std::stod(previous().lexeme); }
    else if (fname == "max_execution_time") { if (!match(TokenType::Number)) error("Expected number"); decl.max_execution_time = static_cast<int>(std::stod(previous().lexeme)); }
    else if (fname == "rules") { decl.rules_json = consume_nested_block_as_json(); }
    else if (fname == "explain_optimizations") { if (match(TokenType::True)) decl.explain_optimizations = true; else if (match(TokenType::False)) decl.explain_optimizations = false; else error("Expected bool"); }
    else if (fname == "show_cost_comparison") { if (match(TokenType::True)) decl.show_cost_comparison = true; else if (match(TokenType::False)) decl.show_cost_comparison = false; else error("Expected bool"); }
    else { error("Unknown query_optimizer field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after query_optimizer body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_execution_policy_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected execution_policy name");
  ExecutionPolicyDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after execution_policy name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in execution_policy");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "max_rows") { if (!match(TokenType::Number)) error("Expected number"); decl.max_rows = static_cast<int>(std::stod(previous().lexeme)); }
    else if (fname == "max_cost") { if (!match(TokenType::Number)) error("Expected number"); decl.max_cost = std::stod(previous().lexeme); }
    else if (fname == "timeout") { if (!match(TokenType::Number)) error("Expected number"); decl.timeout = static_cast<int>(std::stod(previous().lexeme)); }
    else if (fname == "read_only") { if (match(TokenType::True)) decl.read_only = true; else if (match(TokenType::False)) decl.read_only = false; else error("Expected bool"); }
    else if (fname == "apply_masking") { if (match(TokenType::True)) decl.apply_masking = true; else if (match(TokenType::False)) decl.apply_masking = false; else error("Expected bool"); }
    else if (fname == "apply_row_level_security") { if (match(TokenType::True)) decl.apply_row_level_security = true; else if (match(TokenType::False)) decl.apply_row_level_security = false; else error("Expected bool"); }
    else if (fname == "audit_all_queries") { if (match(TokenType::True)) decl.audit_all_queries = true; else if (match(TokenType::False)) decl.audit_all_queries = false; else error("Expected bool"); }
    else if (fname == "retry_on_timeout") { if (match(TokenType::True)) decl.retry_on_timeout = true; else if (match(TokenType::False)) decl.retry_on_timeout = false; else error("Expected bool"); }
    else if (fname == "retry_with_smaller_warehouse") { if (match(TokenType::True)) decl.retry_with_smaller_warehouse = true; else if (match(TokenType::False)) decl.retry_with_smaller_warehouse = false; else error("Expected bool"); }
    else if (fname == "cache_results") { if (match(TokenType::True)) decl.cache_results = true; else if (match(TokenType::False)) decl.cache_results = false; else error("Expected bool"); }
    else if (fname == "cache_ttl") { if (!match(TokenType::String)) error("Expected string"); decl.cache_ttl = previous().lexeme; }
    else if (fname == "cache_key") { if (!match(TokenType::String)) error("Expected string"); decl.cache_key = previous().lexeme; }
    else { error("Unknown execution_policy field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after execution_policy body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_output_format_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected output_format name");
  OutputFormatDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after output_format name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in output_format");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "type") { if (!match(TokenType::String)) error("Expected string"); decl.type_str = previous().lexeme; }
    else if (fname == "excel") { decl.excel_json = consume_nested_block_as_json(); }
    else if (fname == "pdf") { decl.pdf_json = consume_nested_block_as_json(); }
    else if (fname == "html") { decl.html_json = consume_nested_block_as_json(); }
    else if (fname == "csv") { decl.csv_json = consume_nested_block_as_json(); }
    else if (fname == "json_config") { decl.json_config_json = consume_nested_block_as_json(); }
    else if (fname == "slack") { decl.slack_json = consume_nested_block_as_json(); }
    else { error("Unknown output_format field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after output_format body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_query_library_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected query_library name");
  QueryLibraryDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after query_library name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in query_library");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "storage") { if (!match(TokenType::String)) error("Expected string"); decl.storage = previous().lexeme; }
    else if (fname == "path") { if (!match(TokenType::String)) error("Expected string"); decl.path = previous().lexeme; }
    else if (fname == "categories")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::String)) error("Expected string"); decl.categories.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "tags") { if (match(TokenType::True)) decl.tags = true; else if (match(TokenType::False)) decl.tags = false; else error("Expected bool"); }
    else if (fname == "visibility") { if (!match(TokenType::String)) error("Expected string"); decl.library_visibility = previous().lexeme; }
    else if (fname == "approval_required") { if (match(TokenType::True)) decl.approval_required = true; else if (match(TokenType::False)) decl.approval_required = false; else error("Expected bool"); }
    else if (fname == "track_usage") { if (match(TokenType::True)) decl.track_usage = true; else if (match(TokenType::False)) decl.track_usage = false; else error("Expected bool"); }
    else if (fname == "track_performance") { if (match(TokenType::True)) decl.track_performance = true; else if (match(TokenType::False)) decl.track_performance = false; else error("Expected bool"); }
    else if (fname == "suggest_similar") { if (match(TokenType::True)) decl.suggest_similar = true; else if (match(TokenType::False)) decl.suggest_similar = false; else error("Expected bool"); }
    else if (fname == "auto_optimize") { if (match(TokenType::True)) decl.auto_optimize = true; else if (match(TokenType::False)) decl.auto_optimize = false; else error("Expected bool"); }
    else { error("Unknown query_library field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after query_library body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_analysis_schedule_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected analysis_schedule name");
  AnalysisScheduleDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after analysis_schedule name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in analysis_schedule");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "query") { if (!match(TokenType::String)) error("Expected string"); decl.query = previous().lexeme; }
    else if (fname == "cron") { if (!match(TokenType::String)) error("Expected string"); decl.cron = previous().lexeme; }
    else if (fname == "connection") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.connection = previous().lexeme; }
    else if (fname == "format") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.format = previous().lexeme; }
    else if (fname == "output_path") { if (!match(TokenType::String)) error("Expected string"); decl.output_path = previous().lexeme; }
    else if (fname == "delivery") { decl.delivery_json = consume_nested_block_as_json(); }
    else if (fname == "audit") { if (match(TokenType::True)) decl.audit = true; else if (match(TokenType::False)) decl.audit = false; else error("Expected bool"); }
    else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.budget = previous().lexeme; }
    else { error("Unknown analysis_schedule field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after analysis_schedule body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_analyst_agent_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected analyst agent name");
  AnalystAgentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after analyst agent name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in analyst agent");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
    else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
    else if (fname == "system") { if (!match(TokenType::String)) error("Expected string"); decl.system_prompt = previous().lexeme; }
    else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
    else if (fname == "endpoint") { if (!match(TokenType::String)) error("Expected string"); decl.endpoint = previous().lexeme; }
    else if (fname == "api_key_env") { if (!match(TokenType::String)) error("Expected string"); decl.api_key_env = previous().lexeme; }
    else if (fname == "connections")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.connections.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "domain_context") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.domain_context = previous().lexeme; }
    else if (fname == "models")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.models.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "dimensional_models")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.dimensional_models.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "marts")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.marts.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "glossary") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.glossary = previous().lexeme; }
    else if (fname == "semantic_layers")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.semantic_layers.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "governance") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.governance = previous().lexeme; }
    else if (fname == "classification") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.classification = previous().lexeme; }
    else if (fname == "access_policy") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.access_policy = previous().lexeme; }
    else if (fname == "optimizer") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.optimizer = previous().lexeme; }
    else if (fname == "execution_policy") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.execution_policy = previous().lexeme; }
    else if (fname == "query_library") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.query_library = previous().lexeme; }
    else if (fname == "default_output") { if (!match(TokenType::String)) error("Expected string"); decl.default_output = previous().lexeme; }
    else if (fname == "output_formats")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.output_formats.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "skills")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.skills.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "extern_skills")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.extern_skills.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "coordinates_with")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.coordinates_with.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "handoffs")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.handoffs.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "role") { if (!match(TokenType::String)) error("Expected string"); decl.role = previous().lexeme; }
    else if (fname == "purpose") { if (!match(TokenType::String)) error("Expected string"); decl.purpose = previous().lexeme; }
    else if (fname == "autonomy") { if (!match(TokenType::String)) error("Expected string"); decl.autonomy = previous().lexeme; }
    else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.budget = previous().lexeme; }
    else { error("Unknown analyst agent field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after analyst agent body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

// ═══════════════════════════════════════════════════════════════
// v0.9.7: Data Pipeline Deployment Parse Functions
// ═══════════════════════════════════════════════════════════════

StmtPtr Parser::parse_deploy_target_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected deploy_target name");
  DeployTargetDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after deploy_target name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in deploy_target");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "environment") { if (!match(TokenType::String)) error("Expected string"); decl.environment = previous().lexeme; }
    else if (fname == "connection") { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected string or identifier"); decl.connection = previous().lexeme; }
    else if (fname == "namespace_name" || fname == "namespace") { if (!match(TokenType::String)) error("Expected string"); decl.namespace_name = previous().lexeme; }
    else if (fname == "region") { if (!match(TokenType::String)) error("Expected string"); decl.region = previous().lexeme; }
    else if (fname == "tags") { decl.tags_json = consume_nested_block_as_json(); }
    else if (fname == "variables") { decl.variables_json = consume_nested_block_as_json(); }
    else if (fname == "frozen") { if (match(TokenType::True)) decl.frozen = true; else if (match(TokenType::False)) decl.frozen = false; else error("Expected bool"); }
    else if (fname == "freeze_reason") { if (!match(TokenType::String)) error("Expected string"); decl.freeze_reason = previous().lexeme; }
    else { error("Unknown deploy_target field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after deploy_target body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_promotion_rule_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected promotion_rule name");
  PromotionRuleDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after promotion_rule name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in promotion_rule");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "from_env") { if (!match(TokenType::String)) error("Expected string"); decl.from_env = previous().lexeme; }
    else if (fname == "to_env") { if (!match(TokenType::String)) error("Expected string"); decl.to_env = previous().lexeme; }
    else if (fname == "require_tests") { if (match(TokenType::True)) decl.require_tests = true; else if (match(TokenType::False)) decl.require_tests = false; else error("Expected bool"); }
    else if (fname == "require_approval") { if (match(TokenType::True)) decl.require_approval = true; else if (match(TokenType::False)) decl.require_approval = false; else error("Expected bool"); }
    else if (fname == "approvers")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected approver"); decl.approvers.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "auto_promote") { if (match(TokenType::True)) decl.auto_promote = true; else if (match(TokenType::False)) decl.auto_promote = false; else error("Expected bool"); }
    else if (fname == "cooldown") { if (!match(TokenType::String)) error("Expected string"); decl.cooldown = previous().lexeme; }
    else if (fname == "gate_checks") { decl.gate_checks_json = consume_nested_block_as_json(); }
    else { error("Unknown promotion_rule field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after promotion_rule body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_rollback_policy_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected rollback_policy name");
  RollbackPolicyDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after rollback_policy name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in rollback_policy");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "strategy") { if (!match(TokenType::String)) error("Expected string"); decl.strategy = previous().lexeme; }
    else if (fname == "keep_data") { if (match(TokenType::True)) decl.keep_data = true; else if (match(TokenType::False)) decl.keep_data = false; else error("Expected bool"); }
    else if (fname == "notify_dataops") { if (match(TokenType::True)) decl.notify_dataops = true; else if (match(TokenType::False)) decl.notify_dataops = false; else error("Expected bool"); }
    else if (fname == "max_rollback_window") { if (!match(TokenType::String)) error("Expected string"); decl.max_rollback_window = previous().lexeme; }
    else if (fname == "reconciliation") { if (match(TokenType::True)) decl.reconciliation = true; else if (match(TokenType::False)) decl.reconciliation = false; else error("Expected bool"); }
    else if (fname == "pre_rollback_checks") { decl.pre_rollback_json = consume_nested_block_as_json(); }
    else if (fname == "notifications") { decl.notifications_json = consume_nested_block_as_json(); }
    else { error("Unknown rollback_policy field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after rollback_policy body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_artifact_registry_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected artifact_registry name");
  ArtifactRegistryDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after artifact_registry name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in artifact_registry");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "storage") { if (!match(TokenType::String)) error("Expected string"); decl.storage = previous().lexeme; }
    else if (fname == "path") { if (!match(TokenType::String)) error("Expected string"); decl.path = previous().lexeme; }
    else if (fname == "versioning") { if (!match(TokenType::String)) error("Expected string"); decl.versioning = previous().lexeme; }
    else if (fname == "retention") { if (!match(TokenType::String)) error("Expected string"); decl.retention = previous().lexeme; }
    else if (fname == "sign_artifacts") { if (match(TokenType::True)) decl.sign_artifacts = true; else if (match(TokenType::False)) decl.sign_artifacts = false; else error("Expected bool"); }
    else if (fname == "checksum") { if (!match(TokenType::String)) error("Expected string"); decl.checksum = previous().lexeme; }
    else if (fname == "immutable") { if (match(TokenType::True)) decl.immutable = true; else if (match(TokenType::False)) decl.immutable = false; else error("Expected bool"); }
    else { error("Unknown artifact_registry field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after artifact_registry body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_deploy_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected deploy_config name");
  DeployConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after deploy_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in deploy_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "target") { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected target reference"); decl.target = previous().lexeme; }
    else if (fname == "strategy") { if (!match(TokenType::String)) error("Expected string"); decl.strategy = previous().lexeme; }
    else if (fname == "approval_gate") { if (match(TokenType::True)) decl.approval_gate = true; else if (match(TokenType::False)) decl.approval_gate = false; else error("Expected bool"); }
    else if (fname == "pipeline_ref") { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected pipeline reference"); decl.pipeline_ref = previous().lexeme; }
    else if (fname == "pre_deploy_checks") { decl.pre_deploy_json = consume_nested_block_as_json(); }
    else if (fname == "post_deploy_checks") { decl.post_deploy_json = consume_nested_block_as_json(); }
    else if (fname == "notifications") { decl.notifications_json = consume_nested_block_as_json(); }
    else if (fname == "schedule") { if (!match(TokenType::String)) error("Expected string"); decl.schedule = previous().lexeme; }
    else if (fname == "auto_rollback") { if (match(TokenType::True)) decl.auto_rollback = true; else if (match(TokenType::False)) decl.auto_rollback = false; else error("Expected bool"); }
    else if (fname == "rollback_policy") { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected rollback_policy reference"); decl.rollback_policy = previous().lexeme; }
    else if (fname == "artifact_registry") { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected artifact_registry reference"); decl.artifact_registry = previous().lexeme; }
    else { error("Unknown deploy_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after deploy_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

// ═══════════════════════════════════════════════════════════════
// v0.9.8: Data Scientist Agent Parse Functions
// ═══════════════════════════════════════════════════════════════

StmtPtr Parser::parse_problem_statement_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected problem_statement name");
  ProblemStatementDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after problem_statement name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in problem_statement");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "statement") { if (!match(TokenType::String)) error("Expected string"); decl.statement = previous().lexeme; }
    else if (fname == "business_context") { decl.business_context_json = consume_nested_block_as_json(); }
    else if (fname == "constraints") { decl.constraints_json = consume_nested_block_as_json(); }
    else if (fname == "deliverables") { decl.deliverables_json = consume_nested_block_as_json(); }
    else { error("Unknown problem_statement field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after problem_statement body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_ml_experiment_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected ml_experiment name");
  MLExperimentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after ml_experiment name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in ml_experiment");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "problem_type") { if (!match(TokenType::String)) error("Expected string"); decl.problem_type = previous().lexeme; }
    else if (fname == "target") { if (!match(TokenType::String)) error("Expected string"); decl.target = previous().lexeme; }
    else if (fname == "positive_class") { if (!match(TokenType::String)) error("Expected string"); decl.positive_class = previous().lexeme; }
    else if (fname == "dataset") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.dataset = previous().lexeme; }
    else if (fname == "train_test_split") { if (!match(TokenType::Number)) error("Expected number"); decl.train_test_split = std::stod(previous().lexeme); }
    else if (fname == "stratify") { if (match(TokenType::True)) decl.stratify = true; else if (match(TokenType::False)) decl.stratify = false; else error("Expected bool"); }
    else if (fname == "cross_validation") { decl.cross_validation_json = consume_nested_block_as_json(); }
    else if (fname == "algorithms") { if (!match(TokenType::String)) error("Expected string"); decl.algorithms = previous().lexeme; }
    else if (fname == "metrics") { decl.metrics_json = consume_nested_block_as_json(); }
    else if (fname == "interpretability") { if (!match(TokenType::String)) error("Expected string"); decl.interpretability = previous().lexeme; }
    else if (fname == "latency_requirement") { if (!match(TokenType::String)) error("Expected string"); decl.latency_requirement = previous().lexeme; }
    else if (fname == "max_training_time") { if (!match(TokenType::String)) error("Expected string"); decl.max_training_time = previous().lexeme; }
    else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.budget = previous().lexeme; }
    else { error("Unknown ml_experiment field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after ml_experiment body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_eda_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected eda_config name");
  EDAConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after eda_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in eda_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "structural") { decl.structural_json = consume_nested_block_as_json(); }
    else if (fname == "univariate") { decl.univariate_json = consume_nested_block_as_json(); }
    else if (fname == "bivariate") { decl.bivariate_json = consume_nested_block_as_json(); }
    else if (fname == "multivariate") { decl.multivariate_json = consume_nested_block_as_json(); }
    else if (fname == "temporal") { decl.temporal_json = consume_nested_block_as_json(); }
    else if (fname == "performance") { decl.performance_json = consume_nested_block_as_json(); }
    else if (fname == "output") { decl.output_json = consume_nested_block_as_json(); }
    else { error("Unknown eda_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after eda_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_volume_router_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected volume_router name");
  VolumeRouterDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after volume_router name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in volume_router");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "volume_probe") { decl.volume_probe_json = consume_nested_block_as_json(); }
    else if (fname == "routing_rules") { decl.routing_rules_json = consume_nested_block_as_json(); }
    else if (fname == "auto_escalate") { decl.auto_escalate_json = consume_nested_block_as_json(); }
    else if (fname == "sampling_strategy") { decl.sampling_strategy_json = consume_nested_block_as_json(); }
    else { error("Unknown volume_router field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after volume_router body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_code_interpreter_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected code_interpreter name");
  CodeInterpreterDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after code_interpreter name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in code_interpreter");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "runtime") { if (!match(TokenType::String)) error("Expected string"); decl.runtime = previous().lexeme; }
    else if (fname == "version") { if (!match(TokenType::String)) error("Expected string"); decl.version = previous().lexeme; }
    else if (fname == "venv_manager") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.venv_manager = previous().lexeme; }
    else if (fname == "profiles") { decl.profiles_json = consume_nested_block_as_json(); }
    else if (fname == "profile_selection") { if (!match(TokenType::String)) error("Expected string"); decl.profile_selection = previous().lexeme; }
    else if (fname == "sandbox") { decl.sandbox_json = consume_nested_block_as_json(); }
    else if (fname == "auto_test") { decl.auto_test_json = consume_nested_block_as_json(); }
    else if (fname == "data_bridge") { decl.data_bridge_json = consume_nested_block_as_json(); }
    else { error("Unknown code_interpreter field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after code_interpreter body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_datascientist_agent_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected datascientist agent name");
  DataScientistAgentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after datascientist agent name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in datascientist agent");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
    else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
    else if (fname == "system") { if (!match(TokenType::String)) error("Expected string"); decl.system = previous().lexeme; }
    else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
    else if (fname == "endpoint") { if (!match(TokenType::String)) error("Expected string"); decl.endpoint = previous().lexeme; }
    else if (fname == "api_key_env") { if (!match(TokenType::String)) error("Expected string"); decl.api_key_env = previous().lexeme; }
    else if (fname == "agent_md") { if (!match(TokenType::String)) error("Expected string"); decl.agent_md = previous().lexeme; }
    else if (fname == "problem") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.problem = previous().lexeme; }
    else if (fname == "problem_types") { if (!match(TokenType::String)) error("Expected string"); decl.problem_types = previous().lexeme; }
    else if (fname == "sub_agents") { decl.sub_agents_json = consume_nested_block_as_json(); }
    else if (fname == "forge") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.forge = previous().lexeme; }
    else if (fname == "data_sources") { decl.data_sources_json = consume_nested_block_as_json(); }
    else if (fname == "eda_config") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.eda_config = previous().lexeme; }
    else if (fname == "feature_config") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.feature_config = previous().lexeme; }
    else if (fname == "experiment") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.experiment = previous().lexeme; }
    else if (fname == "automl") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.automl = previous().lexeme; }
    else if (fname == "ensemble") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.ensemble = previous().lexeme; }
    else if (fname == "hypotheses")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.hypotheses.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "evaluation") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.evaluation = previous().lexeme; }
    else if (fname == "explainability") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.explainability = previous().lexeme; }
    else if (fname == "code_interpreter") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.code_interpreter = previous().lexeme; }
    else if (fname == "model_registry") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.model_registry = previous().lexeme; }
    else if (fname == "churn") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.churn = previous().lexeme; }
    else if (fname == "clv") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.clv = previous().lexeme; }
    else if (fname == "propensity") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.propensity = previous().lexeme; }
    else if (fname == "recommendation") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.recommendation = previous().lexeme; }
    else if (fname == "experiment_engine") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.experiment_engine = previous().lexeme; }
    else if (fname == "decision_framework") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.decision_framework = previous().lexeme; }
    else if (fname == "volume_router") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.volume_router = previous().lexeme; }
    else if (fname == "distributed_compute") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.distributed_compute = previous().lexeme; }
    else if (fname == "performance") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.performance = previous().lexeme; }
    else if (fname == "data_quality") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.data_quality = previous().lexeme; }
    else if (fname == "self_correction") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.self_correction = previous().lexeme; }
    else if (fname == "self_assessment") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.self_assessment = previous().lexeme; }
    else if (fname == "adaptive_knowledge") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.adaptive_knowledge = previous().lexeme; }
    else if (fname == "deployment") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.deployment = previous().lexeme; }
    else if (fname == "coordinates_with")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.coordinates_with.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "handoffs")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.handoffs.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "role") { if (!match(TokenType::String)) error("Expected string"); decl.role = previous().lexeme; }
    else if (fname == "purpose") { if (!match(TokenType::String)) error("Expected string"); decl.purpose = previous().lexeme; }
    else if (fname == "autonomy") { if (!match(TokenType::String)) error("Expected string"); decl.autonomy = previous().lexeme; }
    else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.budget = previous().lexeme; }
    else { error("Unknown datascientist agent field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after datascientist agent body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

// ═══════════════════════════════════════════════════════════════
// v0.9.8: Data Scientist Agent Stub Parse Functions
// ═══════════════════════════════════════════════════════════════

StmtPtr Parser::parse_hypothesis_test_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected hypothesis_test name");
  HypothesisTestDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after hypothesis_test name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in hypothesis_test");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "null_hypothesis") { if (!match(TokenType::String)) error("Expected string"); decl.null_hypothesis = previous().lexeme; }
    else if (fname == "alternative") { if (!match(TokenType::String)) error("Expected string"); decl.alternative = previous().lexeme; }
    else if (fname == "test_type") { if (!match(TokenType::String)) error("Expected string"); decl.test_type = previous().lexeme; }
    else if (fname == "significance_level") { if (!match(TokenType::Number)) error("Expected number"); decl.significance_level = std::stod(previous().lexeme); }
    else if (fname == "power") { if (!match(TokenType::Number)) error("Expected number"); decl.power = std::stod(previous().lexeme); }
    else if (fname == "effect_size") { if (!match(TokenType::String)) error("Expected string"); decl.effect_size = previous().lexeme; }
    else if (fname == "data_source") { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected string or identifier"); decl.data_source = previous().lexeme; }
    else if (fname == "group_a") { if (!match(TokenType::String)) error("Expected string"); decl.group_a = previous().lexeme; }
    else if (fname == "group_b") { if (!match(TokenType::String)) error("Expected string"); decl.group_b = previous().lexeme; }
    else if (fname == "assumptions") { decl.assumptions_json = consume_nested_block_as_json(); }
    else if (fname == "if_significant") { decl.if_significant_json = consume_nested_block_as_json(); }
    else if (fname == "if_not_significant") { decl.if_not_significant_json = consume_nested_block_as_json(); }
    else { error("Unknown hypothesis_test field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after hypothesis_test body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_feature_engineering_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected feature_engineering name");
  FeatureEngineeringDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after feature_engineering name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in feature_engineering");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "source_tables") { decl.source_tables_json = consume_nested_block_as_json(); }
    else if (fname == "strategies") { decl.strategies_json = consume_nested_block_as_json(); }
    else if (fname == "selection") { decl.selection_json = consume_nested_block_as_json(); }
    else if (fname == "output") { decl.output_json = consume_nested_block_as_json(); }
    else { error("Unknown feature_engineering field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after feature_engineering body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_automl_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected automl_config name");
  AutoMLConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after automl_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in automl_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "algorithms") { if (!match(TokenType::String)) error("Expected string"); decl.algorithms = previous().lexeme; }
    else if (fname == "preprocessing_search") { decl.preprocessing_search_json = consume_nested_block_as_json(); }
    else if (fname == "optimization") { decl.optimization_json = consume_nested_block_as_json(); }
    else if (fname == "cv_folds") { if (!match(TokenType::Number)) error("Expected number"); decl.cv_folds = std::stoi(previous().lexeme); }
    else if (fname == "primary_metric") { if (!match(TokenType::String)) error("Expected string"); decl.primary_metric = previous().lexeme; }
    else if (fname == "holdout_validation") { if (match(TokenType::True)) decl.holdout_validation = true; else if (match(TokenType::False)) decl.holdout_validation = false; else error("Expected bool"); }
    else if (fname == "selection_criteria") { decl.selection_criteria_json = consume_nested_block_as_json(); }
    else if (fname == "leaderboard") { if (match(TokenType::True)) decl.leaderboard = true; else if (match(TokenType::False)) decl.leaderboard = false; else error("Expected bool"); }
    else { error("Unknown automl_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after automl_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_hyperparameter_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected hyperparameter_config name");
  HyperparameterConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after hyperparameter_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in hyperparameter_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "algorithm") { if (!match(TokenType::String)) error("Expected string"); decl.algorithm = previous().lexeme; }
    else if (fname == "search_space") { decl.search_space_json = consume_nested_block_as_json(); }
    else if (fname == "optimizer") { decl.optimizer_json = consume_nested_block_as_json(); }
    else if (fname == "early_stopping") { decl.early_stopping_json = consume_nested_block_as_json(); }
    else { error("Unknown hyperparameter_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after hyperparameter_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_stacked_model_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected stacked_model name");
  StackedModelDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after stacked_model name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in stacked_model");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "base_learners") { decl.base_learners_json = consume_nested_block_as_json(); }
    else if (fname == "meta_learner") { decl.meta_learner_json = consume_nested_block_as_json(); }
    else if (fname == "strategy") { decl.strategy_json = consume_nested_block_as_json(); }
    else if (fname == "compare_against") { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected string or identifier"); decl.compare_against = previous().lexeme; }
    else if (fname == "improvement_threshold") { if (!match(TokenType::Number)) error("Expected number"); decl.improvement_threshold = std::stod(previous().lexeme); }
    else { error("Unknown stacked_model field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after stacked_model body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_evaluation_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected evaluation_config name");
  EvaluationConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after evaluation_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in evaluation_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "classification") { decl.classification_json = consume_nested_block_as_json(); }
    else if (fname == "regression") { decl.regression_json = consume_nested_block_as_json(); }
    else if (fname == "clustering") { decl.clustering_json = consume_nested_block_as_json(); }
    else if (fname == "business") { decl.business_json = consume_nested_block_as_json(); }
    else if (fname == "cv_strategy") { if (!match(TokenType::String)) error("Expected string"); decl.cv_strategy = previous().lexeme; }
    else if (fname == "outer_folds") { if (!match(TokenType::Number)) error("Expected number"); decl.outer_folds = std::stoi(previous().lexeme); }
    else if (fname == "inner_folds") { if (!match(TokenType::Number)) error("Expected number"); decl.inner_folds = std::stoi(previous().lexeme); }
    else if (fname == "model_comparison") { decl.model_comparison_json = consume_nested_block_as_json(); }
    else { error("Unknown evaluation_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after evaluation_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_model_registry_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected model_registry name");
  ModelRegistryDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after model_registry name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in model_registry");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "storage") { decl.storage_json = consume_nested_block_as_json(); }
    else if (fname == "tracking") { decl.tracking_json = consume_nested_block_as_json(); }
    else if (fname == "model_card") { decl.model_card_json = consume_nested_block_as_json(); }
    else if (fname == "lifecycle") { decl.lifecycle_json = consume_nested_block_as_json(); }
    else { error("Unknown model_registry field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after model_registry body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_explainability_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected explainability_config name");
  ExplainabilityConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after explainability_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in explainability_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "global") { decl.global_json = consume_nested_block_as_json(); }
    else if (fname == "local") { decl.local_json = consume_nested_block_as_json(); }
    else if (fname == "fairness") { decl.fairness_json = consume_nested_block_as_json(); }
    else { error("Unknown explainability_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after explainability_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_venv_manager_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected venv_manager name");
  VenvManagerDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after venv_manager name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in venv_manager");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "lifecycle") { decl.lifecycle_json = consume_nested_block_as_json(); }
    else if (fname == "pool") { decl.pool_json = consume_nested_block_as_json(); }
    else if (fname == "dependency_resolver") { decl.dependency_resolver_json = consume_nested_block_as_json(); }
    else { error("Unknown venv_manager field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after venv_manager body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_nlp_pipeline_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected nlp_pipeline name");
  NLPPipelineDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after nlp_pipeline name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in nlp_pipeline");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "preprocessing") { decl.preprocessing_json = consume_nested_block_as_json(); }
    else if (fname == "tasks") { decl.tasks_json = consume_nested_block_as_json(); }
    else if (fname == "embedding_model") { if (!match(TokenType::String)) error("Expected string"); decl.embedding_model = previous().lexeme; }
    else if (fname == "vector_store") { if (!match(TokenType::String)) error("Expected string"); decl.vector_store = previous().lexeme; }
    else { error("Unknown nlp_pipeline field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after nlp_pipeline body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_churn_analysis_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected churn_analysis name");
  ChurnAnalysisDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after churn_analysis name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in churn_analysis");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "churn_definition") { decl.churn_definition_json = consume_nested_block_as_json(); }
    else if (fname == "features") { decl.features_json = consume_nested_block_as_json(); }
    else if (fname == "primary_model") { if (!match(TokenType::String)) error("Expected string"); decl.primary_model = previous().lexeme; }
    else if (fname == "calibration") { if (!match(TokenType::String)) error("Expected string"); decl.calibration = previous().lexeme; }
    else if (fname == "threshold_optimization") { if (!match(TokenType::String)) error("Expected string"); decl.threshold_optimization = previous().lexeme; }
    else if (fname == "outputs") { decl.outputs_json = consume_nested_block_as_json(); }
    else { error("Unknown churn_analysis field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after churn_analysis body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_clv_model_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected clv_model name");
  CLVModelDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after clv_model name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in clv_model");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "model_type") { if (!match(TokenType::String)) error("Expected string"); decl.model_type = previous().lexeme; }
    else if (fname == "frequency") { if (!match(TokenType::String)) error("Expected string"); decl.frequency = previous().lexeme; }
    else if (fname == "recency") { if (!match(TokenType::String)) error("Expected string"); decl.recency = previous().lexeme; }
    else if (fname == "monetary") { if (!match(TokenType::String)) error("Expected string"); decl.monetary = previous().lexeme; }
    else if (fname == "T") { if (!match(TokenType::String)) error("Expected string"); decl.T = previous().lexeme; }
    else if (fname == "prediction_periods") { decl.prediction_periods_json = consume_nested_block_as_json(); }
    else if (fname == "discount_rate") { if (!match(TokenType::Number)) error("Expected number"); decl.discount_rate = std::stod(previous().lexeme); }
    else if (fname == "segments") { decl.segments_json = consume_nested_block_as_json(); }
    else { error("Unknown clv_model field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after clv_model body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_propensity_model_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected propensity_model name");
  PropensityModelDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after propensity_model name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in propensity_model");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "target_action") { if (!match(TokenType::String)) error("Expected string"); decl.target_action = previous().lexeme; }
    else if (fname == "training_window") { if (!match(TokenType::String)) error("Expected string"); decl.training_window = previous().lexeme; }
    else if (fname == "features") { decl.features_json = consume_nested_block_as_json(); }
    else if (fname == "algorithm") { if (!match(TokenType::String)) error("Expected string"); decl.algorithm = previous().lexeme; }
    else if (fname == "calibration_method") { if (!match(TokenType::String)) error("Expected string"); decl.calibration_method = previous().lexeme; }
    else if (fname == "score_output") { decl.score_output_json = consume_nested_block_as_json(); }
    else if (fname == "actions") { decl.actions_json = consume_nested_block_as_json(); }
    else { error("Unknown propensity_model field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after propensity_model body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_recommendation_engine_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected recommendation_engine name");
  RecommendationEngineDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after recommendation_engine name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in recommendation_engine");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "strategy") { if (!match(TokenType::String)) error("Expected string"); decl.strategy = previous().lexeme; }
    else if (fname == "collaborative") { decl.collaborative_json = consume_nested_block_as_json(); }
    else if (fname == "content_based") { decl.content_based_json = consume_nested_block_as_json(); }
    else if (fname == "blending") { decl.blending_json = consume_nested_block_as_json(); }
    else if (fname == "rules") { decl.rules_json = consume_nested_block_as_json(); }
    else if (fname == "metrics") { if (!match(TokenType::String)) error("Expected string"); decl.metrics = previous().lexeme; }
    else if (fname == "serving") { decl.serving_json = consume_nested_block_as_json(); }
    else { error("Unknown recommendation_engine field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after recommendation_engine body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_experiment_design_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected experiment_design name");
  ExperimentDesignDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after experiment_design name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in experiment_design");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "experiment_type") { if (!match(TokenType::String)) error("Expected string"); decl.experiment_type = previous().lexeme; }
    else if (fname == "control") { decl.control_json = consume_nested_block_as_json(); }
    else if (fname == "treatments") { decl.treatments_json = consume_nested_block_as_json(); }
    else if (fname == "unit") { if (!match(TokenType::String)) error("Expected string"); decl.unit = previous().lexeme; }
    else if (fname == "stratify_by") { if (!match(TokenType::String)) error("Expected string"); decl.stratify_by = previous().lexeme; }
    else if (fname == "power_analysis") { decl.power_analysis_json = consume_nested_block_as_json(); }
    else if (fname == "primary_metric") { decl.primary_metric_json = consume_nested_block_as_json(); }
    else if (fname == "guardrails") { decl.guardrails_json = consume_nested_block_as_json(); }
    else if (fname == "analysis") { decl.analysis_json = consume_nested_block_as_json(); }
    else { error("Unknown experiment_design field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after experiment_design body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_scenario_analysis_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected scenario_analysis name");
  ScenarioAnalysisDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after scenario_analysis name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in scenario_analysis");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "base_model") { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected string or identifier"); decl.base_model = previous().lexeme; }
    else if (fname == "scenarios") { decl.scenarios_json = consume_nested_block_as_json(); }
    else if (fname == "simulation") { decl.simulation_json = consume_nested_block_as_json(); }
    else { error("Unknown scenario_analysis field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after scenario_analysis body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_decision_support_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected decision_support name");
  DecisionSupportDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after decision_support name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in decision_support");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "deliverables") { decl.deliverables_json = consume_nested_block_as_json(); }
    else if (fname == "confidence") { decl.confidence_json = consume_nested_block_as_json(); }
    else { error("Unknown decision_support field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after decision_support body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_eda_technique_selector_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected eda_technique_selector name");
  EDATechniqueSelectorDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after eda_technique_selector name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in eda_technique_selector");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "rules") { decl.rules_json = consume_nested_block_as_json(); }
    else if (fname == "output") { decl.output_json = consume_nested_block_as_json(); }
    else { error("Unknown eda_technique_selector field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after eda_technique_selector body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_smart_connector_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected smart_connector name");
  SmartConnectorDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after smart_connector name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in smart_connector");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "discovery") { decl.discovery_json = consume_nested_block_as_json(); }
    else if (fname == "metadata_cache") { decl.metadata_cache_json = consume_nested_block_as_json(); }
    else { error("Unknown smart_connector field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after smart_connector body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_compute_connector_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected compute_connector name");
  ComputeConnectorDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after compute_connector name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in compute_connector");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "engine") { if (!match(TokenType::String)) error("Expected string"); decl.engine = previous().lexeme; }
    else if (fname == "connection") { if (!match(TokenType::String)) error("Expected string"); decl.connection = previous().lexeme; }
    else if (fname == "token") { if (!match(TokenType::String)) error("Expected string"); decl.token = previous().lexeme; }
    else if (fname == "cluster_config") { decl.cluster_config_json = consume_nested_block_as_json(); }
    else if (fname == "idle_timeout") { if (!match(TokenType::String)) error("Expected string"); decl.idle_timeout = previous().lexeme; }
    else if (fname == "cost_tracking") { if (match(TokenType::True)) decl.cost_tracking = true; else if (match(TokenType::False)) decl.cost_tracking = false; else error("Expected bool"); }
    else { error("Unknown compute_connector field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after compute_connector body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_file_connector_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected file_connector name");
  FileConnectorDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after file_connector name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in file_connector");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "base_path") { if (!match(TokenType::String)) error("Expected string"); decl.base_path = previous().lexeme; }
    else if (fname == "auto_detect_schema") { if (match(TokenType::True)) decl.auto_detect_schema = true; else if (match(TokenType::False)) decl.auto_detect_schema = false; else error("Expected bool"); }
    else if (fname == "auto_detect_delimiter") { if (match(TokenType::True)) decl.auto_detect_delimiter = true; else if (match(TokenType::False)) decl.auto_detect_delimiter = false; else error("Expected bool"); }
    else if (fname == "auto_detect_encoding") { if (match(TokenType::True)) decl.auto_detect_encoding = true; else if (match(TokenType::False)) decl.auto_detect_encoding = false; else error("Expected bool"); }
    else if (fname == "supported_formats") { decl.supported_formats_json = consume_nested_block_as_json(); }
    else if (fname == "large_file_strategy") { decl.large_file_strategy_json = consume_nested_block_as_json(); }
    else { error("Unknown file_connector field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after file_connector body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_distributed_compute_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected distributed_compute_config name");
  DistributedComputeConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after distributed_compute_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in distributed_compute_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "spark") { decl.spark_json = consume_nested_block_as_json(); }
    else if (fname == "databricks") { decl.databricks_json = consume_nested_block_as_json(); }
    else if (fname == "snowflake") { decl.snowflake_json = consume_nested_block_as_json(); }
    else if (fname == "hadoop") { decl.hadoop_json = consume_nested_block_as_json(); }
    else if (fname == "gpu") { decl.gpu_json = consume_nested_block_as_json(); }
    else if (fname == "selection_logic") { decl.selection_logic_json = consume_nested_block_as_json(); }
    else { error("Unknown distributed_compute_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after distributed_compute_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_performance_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected performance_config name");
  PerformanceConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after performance_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in performance_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "phase_slas") { decl.phase_slas_json = consume_nested_block_as_json(); }
    else if (fname == "total_analysis_sla") { decl.total_analysis_sla_json = consume_nested_block_as_json(); }
    else if (fname == "cache") { decl.cache_json = consume_nested_block_as_json(); }
    else if (fname == "parallelism") { decl.parallelism_json = consume_nested_block_as_json(); }
    else if (fname == "lazy_eval") { decl.lazy_eval_json = consume_nested_block_as_json(); }
    else if (fname == "memory") { decl.memory_json = consume_nested_block_as_json(); }
    else { error("Unknown performance_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after performance_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_data_quality_pipeline_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected data_quality_pipeline name");
  DataQualityPipelineDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after data_quality_pipeline name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in data_quality_pipeline");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "profiling") { decl.profiling_json = consume_nested_block_as_json(); }
    else if (fname == "scoring") { decl.scoring_json = consume_nested_block_as_json(); }
    else if (fname == "remediation") { decl.remediation_json = consume_nested_block_as_json(); }
    else if (fname == "report") { decl.report_json = consume_nested_block_as_json(); }
    else if (fname == "governance_ref") { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected string or identifier"); decl.governance_ref = previous().lexeme; }
    else { error("Unknown data_quality_pipeline field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after data_quality_pipeline body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_self_correction_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected self_correction_config name");
  SelfCorrectionConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after self_correction_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in self_correction_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "code_errors") { decl.code_errors_json = consume_nested_block_as_json(); }
    else if (fname == "statistical_errors") { decl.statistical_errors_json = consume_nested_block_as_json(); }
    else if (fname == "model_errors") { decl.model_errors_json = consume_nested_block_as_json(); }
    else if (fname == "reasoning_errors") { decl.reasoning_errors_json = consume_nested_block_as_json(); }
    else { error("Unknown self_correction_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after self_correction_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_self_assessment_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected self_assessment name");
  SelfAssessmentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after self_assessment name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in self_assessment");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "planning") { decl.planning_json = consume_nested_block_as_json(); }
    else if (fname == "execution") { decl.execution_json = consume_nested_block_as_json(); }
    else if (fname == "interpretation") { decl.interpretation_json = consume_nested_block_as_json(); }
    else if (fname == "communication") { decl.communication_json = consume_nested_block_as_json(); }
    else if (fname == "gate") { decl.gate_json = consume_nested_block_as_json(); }
    else { error("Unknown self_assessment field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after self_assessment body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_adaptive_knowledge_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected adaptive_knowledge_config name");
  AdaptiveKnowledgeConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after adaptive_knowledge_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in adaptive_knowledge_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "knowledge_sources") { decl.knowledge_sources_json = consume_nested_block_as_json(); }
    else if (fname == "adaptation") { decl.adaptation_json = consume_nested_block_as_json(); }
    else if (fname == "learning") { decl.learning_json = consume_nested_block_as_json(); }
    else { error("Unknown adaptive_knowledge_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after adaptive_knowledge_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_analysis_history_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected analysis_history name");
  AnalysisHistoryDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after analysis_history name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in analysis_history");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "knowledge_base") { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected string or identifier"); decl.knowledge_base = previous().lexeme; }
    else if (fname == "vector_store") { if (!match(TokenType::String)) error("Expected string"); decl.vector_store = previous().lexeme; }
    else if (fname == "embedding_model") { if (!match(TokenType::String)) error("Expected string"); decl.embedding_model = previous().lexeme; }
    else if (fname == "retrieval_strategy") { if (!match(TokenType::String)) error("Expected string"); decl.retrieval_strategy = previous().lexeme; }
    else if (fname == "record_fields") { decl.record_fields_json = consume_nested_block_as_json(); }
    else if (fname == "retention") { if (!match(TokenType::String)) error("Expected string"); decl.retention = previous().lexeme; }
    else if (fname == "max_records") { if (!match(TokenType::Number)) error("Expected number"); decl.max_records = std::stoi(previous().lexeme); }
    else { error("Unknown analysis_history field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after analysis_history body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_observability_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected observability_config name");
  ObservabilityConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after observability_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in observability_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "feature_monitoring") { decl.feature_monitoring_json = consume_nested_block_as_json(); }
    else if (fname == "prediction_monitoring") { decl.prediction_monitoring_json = consume_nested_block_as_json(); }
    else if (fname == "alerts") { decl.alerts_json = consume_nested_block_as_json(); }
    else if (fname == "auto_remediation") { decl.auto_remediation_json = consume_nested_block_as_json(); }
    else if (fname == "dataops_ref") { if (!match(TokenType::String) && !match(TokenType::Identifier)) error("Expected string or identifier"); decl.dataops_ref = previous().lexeme; }
    else { error("Unknown observability_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after observability_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

// ═══════════════════════════════════════════════════════════════
// v0.9.8.1: Causal Agent Parse Functions
// ═══════════════════════════════════════════════════════════════

StmtPtr Parser::parse_causal_discovery_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected causal_discovery name");
  CausalDiscoveryDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after causal_discovery name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in causal_discovery");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "llm_discovery") { decl.llm_discovery_json = consume_nested_block_as_json(); }
    else if (fname == "algorithmic_discovery") { decl.algorithmic_discovery_json = consume_nested_block_as_json(); }
    else if (fname == "merge_strategy") { decl.merge_strategy_json = consume_nested_block_as_json(); }
    else if (fname == "validation") { decl.validation_json = consume_nested_block_as_json(); }
    else { error("Unknown causal_discovery field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after causal_discovery body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_scm_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected scm name");
  SCMDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after scm name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in scm");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "variables") { decl.variables_json = consume_nested_block_as_json(); }
    else if (fname == "exogenous") { decl.exogenous_json = consume_nested_block_as_json(); }
    else if (fname == "latent_confounders") { decl.latent_confounders_json = consume_nested_block_as_json(); }
    else if (fname == "dag") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.dag = previous().lexeme; }
    else { error("Unknown scm field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after scm body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_intervention_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected intervention name");
  InterventionDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after intervention name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in intervention");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "scm") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.scm = previous().lexeme; }
    else if (fname == "do") { decl.do_json = consume_nested_block_as_json(); }
    else if (fname == "outcome") { if (!match(TokenType::String)) error("Expected string"); decl.outcome = previous().lexeme; }
    else if (fname == "identification") { decl.identification_json = consume_nested_block_as_json(); }
    else if (fname == "estimation") { decl.estimation_json = consume_nested_block_as_json(); }
    else if (fname == "compare_with_naive") { if (!match(TokenType::True) && !match(TokenType::False)) error("Expected bool"); decl.compare_with_naive = previous().lexeme == "true"; }
    else { error("Unknown intervention field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after intervention body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_counterfactual_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected counterfactual name");
  CounterfactualDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after counterfactual name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in counterfactual");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "scm") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.scm = previous().lexeme; }
    else if (fname == "evidence") { decl.evidence_json = consume_nested_block_as_json(); }
    else if (fname == "question") { if (!match(TokenType::String)) error("Expected string"); decl.question = previous().lexeme; }
    else if (fname == "abduction") { decl.abduction_json = consume_nested_block_as_json(); }
    else if (fname == "action") { decl.action_json = consume_nested_block_as_json(); }
    else if (fname == "prediction") { decl.prediction_json = consume_nested_block_as_json(); }
    else if (fname == "attribution") { decl.attribution_json = consume_nested_block_as_json(); }
    else { error("Unknown counterfactual field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after counterfactual body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_bayesian_model_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected bayesian_model name");
  BayesianModelDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after bayesian_model name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in bayesian_model");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "framework") { if (!match(TokenType::String)) error("Expected string"); decl.framework = previous().lexeme; }
    else if (fname == "version") { if (!match(TokenType::String)) error("Expected string"); decl.version = previous().lexeme; }
    else if (fname == "priors") { decl.priors_json = consume_nested_block_as_json(); }
    else if (fname == "likelihood") { decl.likelihood_json = consume_nested_block_as_json(); }
    else if (fname == "sampling") { decl.sampling_json = consume_nested_block_as_json(); }
    else if (fname == "posterior") { decl.posterior_json = consume_nested_block_as_json(); }
    else if (fname == "comparison") { decl.comparison_json = consume_nested_block_as_json(); }
    else { error("Unknown bayesian_model field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after bayesian_model body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_causal_estimator_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected causal_estimator name");
  CausalEstimatorDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after causal_estimator name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in causal_estimator");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "scm") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.scm = previous().lexeme; }
    else if (fname == "treatment") { if (!match(TokenType::String)) error("Expected string"); decl.treatment = previous().lexeme; }
    else if (fname == "outcome") { if (!match(TokenType::String)) error("Expected string"); decl.outcome = previous().lexeme; }
    else if (fname == "primary") { decl.primary_json = consume_nested_block_as_json(); }
    else if (fname == "secondary") { decl.secondary_json = consume_nested_block_as_json(); }
    else if (fname == "heterogeneous") { decl.heterogeneous_json = consume_nested_block_as_json(); }
    else if (fname == "compare_estimators") { if (!match(TokenType::True) && !match(TokenType::False)) error("Expected bool"); decl.compare_estimators = previous().lexeme == "true"; }
    else { error("Unknown causal_estimator field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after causal_estimator body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_quasi_experiment_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected quasi_experiment name");
  QuasiExperimentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after quasi_experiment name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in quasi_experiment");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "method") { if (!match(TokenType::String)) error("Expected string"); decl.method = previous().lexeme; }
    else if (fname == "treatment_time") { if (!match(TokenType::String)) error("Expected string"); decl.treatment_time = previous().lexeme; }
    else if (fname == "treatment_group") { if (!match(TokenType::String)) error("Expected string"); decl.treatment_group = previous().lexeme; }
    else if (fname == "control_group") { if (!match(TokenType::String)) error("Expected string"); decl.control_group = previous().lexeme; }
    else if (fname == "outcome") { if (!match(TokenType::String)) error("Expected string"); decl.outcome = previous().lexeme; }
    else if (fname == "covariates") { if (!match(TokenType::String)) error("Expected string"); decl.covariates = previous().lexeme; }
    else if (fname == "parallel_trends_test") { if (!match(TokenType::True) && !match(TokenType::False)) error("Expected bool"); decl.parallel_trends_test = previous().lexeme == "true"; }
    else if (fname == "bayesian") { if (!match(TokenType::True) && !match(TokenType::False)) error("Expected bool"); decl.bayesian = previous().lexeme == "true"; }
    else if (fname == "mcmc") { decl.mcmc_json = consume_nested_block_as_json(); }
    else if (fname == "running_variable") { if (!match(TokenType::String)) error("Expected string"); decl.running_variable = previous().lexeme; }
    else if (fname == "cutoff") { if (!match(TokenType::Number)) error("Expected number"); decl.cutoff = std::stod(previous().lexeme); }
    else if (fname == "bandwidth") { if (!match(TokenType::String)) error("Expected string"); decl.bandwidth = previous().lexeme; }
    else { error("Unknown quasi_experiment field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after quasi_experiment body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_sensitivity_analysis_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected sensitivity_analysis name");
  CausalSensitivityDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after sensitivity_analysis name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in sensitivity_analysis");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "estimator") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.estimator = previous().lexeme; }
    else if (fname == "rosenbaum") { decl.rosenbaum_json = consume_nested_block_as_json(); }
    else if (fname == "e_value") { if (!match(TokenType::True) && !match(TokenType::False)) error("Expected bool"); decl.e_value = previous().lexeme == "true"; }
    else if (fname == "refutations") { decl.refutations_json = consume_nested_block_as_json(); }
    else if (fname == "assumptions") { decl.assumptions_json = consume_nested_block_as_json(); }
    else if (fname == "output") { decl.output_json = consume_nested_block_as_json(); }
    else { error("Unknown sensitivity_analysis field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after sensitivity_analysis body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_causal_data_requirements_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected causal_data_requirements name");
  CausalDataRequirementsDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after causal_data_requirements name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in causal_data_requirements");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "temporal") { decl.temporal_json = consume_nested_block_as_json(); }
    else if (fname == "required_confounders") { if (!match(TokenType::String)) error("Expected string"); decl.required_confounders = previous().lexeme; }
    else if (fname == "instruments") { decl.instruments_json = consume_nested_block_as_json(); }
    else if (fname == "natural_experiments") { if (!match(TokenType::String)) error("Expected string"); decl.natural_experiments = previous().lexeme; }
    else if (fname == "quality") { decl.quality_json = consume_nested_block_as_json(); }
    else { error("Unknown causal_data_requirements field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after causal_data_requirements body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_causal_agent_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected causal agent name");
  CausalAgentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after causal agent name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in causal agent");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
    else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
    else if (fname == "system") { if (!match(TokenType::String)) error("Expected string"); decl.system = previous().lexeme; }
    else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
    else if (fname == "endpoint") { if (!match(TokenType::String)) error("Expected string"); decl.endpoint = previous().lexeme; }
    else if (fname == "api_key_env") { if (!match(TokenType::String)) error("Expected string"); decl.api_key_env = previous().lexeme; }
    else if (fname == "agent_md") { if (!match(TokenType::String)) error("Expected string"); decl.agent_md = previous().lexeme; }
    else if (fname == "sub_agents") { decl.sub_agents_json = consume_nested_block_as_json(); }
    else if (fname == "forge") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.forge = previous().lexeme; }
    else if (fname == "peer_agent") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.peer_agent = previous().lexeme; }
    else if (fname == "discovery") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.discovery = previous().lexeme; }
    else if (fname == "scm") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.scm = previous().lexeme; }
    else if (fname == "intervention") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.intervention = previous().lexeme; }
    else if (fname == "counterfactual") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.counterfactual = previous().lexeme; }
    else if (fname == "bayesian_model") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.bayesian_model = previous().lexeme; }
    else if (fname == "estimator") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.estimator = previous().lexeme; }
    else if (fname == "sensitivity") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.sensitivity = previous().lexeme; }
    else if (fname == "data_requirements") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.data_requirements = previous().lexeme; }
    else if (fname == "code_interpreter") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.code_interpreter = previous().lexeme; }
    else if (fname == "coordinates_with")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.coordinates_with.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "handoffs")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.handoffs.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "role") { if (!match(TokenType::String)) error("Expected string"); decl.role = previous().lexeme; }
    else if (fname == "purpose") { if (!match(TokenType::String)) error("Expected string"); decl.purpose = previous().lexeme; }
    else if (fname == "autonomy") { if (!match(TokenType::String)) error("Expected string"); decl.autonomy = previous().lexeme; }
    else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.budget = previous().lexeme; }
    else { error("Unknown causal agent field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after causal agent body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

// ═══════════════════════════════════════════════════════════════
// v0.9.8.2: MLOps Agent Parse Functions
// ═══════════════════════════════════════════════════════════════

StmtPtr Parser::parse_drift_monitor_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected drift_monitor name");
  DriftMonitorDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after drift_monitor name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in drift_monitor");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "model") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.model = previous().lexeme; }
    else if (fname == "reference_dataset") { if (!match(TokenType::String)) error("Expected string"); decl.reference_dataset = previous().lexeme; }
    else if (fname == "data_drift") { decl.data_drift_json = consume_nested_block_as_json(); }
    else if (fname == "concept_drift") { decl.concept_drift_json = consume_nested_block_as_json(); }
    else if (fname == "prediction_drift") { decl.prediction_drift_json = consume_nested_block_as_json(); }
    else if (fname == "alerts") { decl.alerts_json = consume_nested_block_as_json(); }
    else if (fname == "root_cause_analysis") { decl.root_cause_analysis_json = consume_nested_block_as_json(); }
    else { error("Unknown drift_monitor field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after drift_monitor body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_retraining_pipeline_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected retraining_pipeline name");
  RetrainingPipelineDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after retraining_pipeline name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in retraining_pipeline");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "triggers") { decl.triggers_json = consume_nested_block_as_json(); }
    else if (fname == "data") { decl.data_json = consume_nested_block_as_json(); }
    else if (fname == "training") { decl.training_json = consume_nested_block_as_json(); }
    else if (fname == "validation") { decl.validation_json = consume_nested_block_as_json(); }
    else if (fname == "deployment") { decl.deployment_json = consume_nested_block_as_json(); }
    else if (fname == "notifications") { decl.notifications_json = consume_nested_block_as_json(); }
    else { error("Unknown retraining_pipeline field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after retraining_pipeline body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_deployment_strategy_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected deployment_strategy name");
  MLDeployStrategyDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after deployment_strategy name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in deployment_strategy");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "strategy") { if (!match(TokenType::String)) error("Expected string"); decl.strategy = previous().lexeme; }
    else if (fname == "config") { decl.config_json = consume_nested_block_as_json(); }
    else { error("Unknown deployment_strategy field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after deployment_strategy body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_champion_challenger_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected champion_challenger name");
  ChampionChallengerDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after champion_challenger name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in champion_challenger");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "champion") { decl.champion_json = consume_nested_block_as_json(); }
    else if (fname == "challenger") { decl.challenger_json = consume_nested_block_as_json(); }
    else if (fname == "evaluation") { decl.evaluation_json = consume_nested_block_as_json(); }
    else if (fname == "promotion") { decl.promotion_json = consume_nested_block_as_json(); }
    else if (fname == "rollback") { decl.rollback_json = consume_nested_block_as_json(); }
    else { error("Unknown champion_challenger field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after champion_challenger body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_serving_infra_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected serving_infra name");
  ServingInfraDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after serving_infra name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in serving_infra");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "mode") { if (!match(TokenType::String)) error("Expected string"); decl.mode = previous().lexeme; }
    else if (fname == "platform") { decl.platform_json = consume_nested_block_as_json(); }
    else if (fname == "sla") { decl.sla_json = consume_nested_block_as_json(); }
    else if (fname == "cost") { decl.cost_json = consume_nested_block_as_json(); }
    else if (fname == "health") { decl.health_json = consume_nested_block_as_json(); }
    else { error("Unknown serving_infra field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after serving_infra body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_training_infra_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected training_infra name");
  TrainingInfraDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after training_infra name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in training_infra");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "compute_tiers") { decl.compute_tiers_json = consume_nested_block_as_json(); }
    else if (fname == "selection") { decl.selection_json = consume_nested_block_as_json(); }
    else if (fname == "cost_tracking") { decl.cost_tracking_json = consume_nested_block_as_json(); }
    else { error("Unknown training_infra field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after training_infra body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_mlops_rollback_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected mlops_rollback name");
  MLOpsRollbackDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after mlops_rollback name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in mlops_rollback");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "auto_triggers") { decl.auto_triggers_json = consume_nested_block_as_json(); }
    else if (fname == "strategy") { decl.strategy_json = consume_nested_block_as_json(); }
    else if (fname == "post_rollback") { decl.post_rollback_json = consume_nested_block_as_json(); }
    else if (fname == "recovery") { decl.recovery_json = consume_nested_block_as_json(); }
    else { error("Unknown mlops_rollback field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after mlops_rollback body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_monitoring_stack_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected monitoring_stack name");
  MonitoringStackDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after monitoring_stack name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in monitoring_stack");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "evidently") { decl.evidently_json = consume_nested_block_as_json(); }
    else if (fname == "prometheus") { decl.prometheus_json = consume_nested_block_as_json(); }
    else if (fname == "whylabs") { decl.whylabs_json = consume_nested_block_as_json(); }
    else { error("Unknown monitoring_stack field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after monitoring_stack body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_mlflow_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected mlflow_config name");
  MLflowConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after mlflow_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in mlflow_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "mcp_server") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.mcp_server = previous().lexeme; }
    else if (fname == "tracking") { decl.tracking_json = consume_nested_block_as_json(); }
    else if (fname == "registry") { decl.registry_json = consume_nested_block_as_json(); }
    else if (fname == "lifecycle") { decl.lifecycle_json = consume_nested_block_as_json(); }
    else { error("Unknown mlflow_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after mlflow_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_business_kpi_tracker_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected business_kpi_tracker name");
  BusinessKPITrackerDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after business_kpi_tracker name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in business_kpi_tracker");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "model") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.model = previous().lexeme; }
    else if (fname == "kpis") { decl.kpis_json = consume_nested_block_as_json(); }
    else if (fname == "report_frequency") { if (!match(TokenType::String)) error("Expected string"); decl.report_frequency = previous().lexeme; }
    else if (fname == "compare_with") { if (!match(TokenType::String)) error("Expected string"); decl.compare_with = previous().lexeme; }
    else { error("Unknown business_kpi_tracker field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after business_kpi_tracker body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_dataset_version_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected dataset_version name");
  DatasetVersionDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after dataset_version name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in dataset_version");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "versioning_tool") { if (!match(TokenType::String)) error("Expected string"); decl.versioning_tool = previous().lexeme; }
    else if (fname == "source") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.source = previous().lexeme; }
    else if (fname == "query") { if (!match(TokenType::String)) error("Expected string"); decl.query = previous().lexeme; }
    else if (fname == "hash_method") { if (!match(TokenType::String)) error("Expected string"); decl.hash_method = previous().lexeme; }
    else if (fname == "storage") { if (!match(TokenType::String)) error("Expected string"); decl.storage = previous().lexeme; }
    else if (fname == "lineage") { decl.lineage_json = consume_nested_block_as_json(); }
    else if (fname == "schema_validation") { decl.schema_validation_json = consume_nested_block_as_json(); }
    else { error("Unknown dataset_version field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after dataset_version body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_feedback_loop_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected feedback_loop name");
  FeedbackLoopDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after feedback_loop name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in feedback_loop");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "production_metrics") { decl.production_metrics_json = consume_nested_block_as_json(); }
    else if (fname == "recommendations") { decl.recommendations_json = consume_nested_block_as_json(); }
    else if (fname == "trigger_ds_agent") { decl.trigger_ds_agent_json = consume_nested_block_as_json(); }
    else { error("Unknown feedback_loop field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after feedback_loop body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_decision_engine_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected decision_engine name");
  DecisionEngineDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after decision_engine name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in decision_engine");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "retrain_policy") { decl.retrain_policy_json = consume_nested_block_as_json(); }
    else if (fname == "rollback_policy") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.rollback_policy = previous().lexeme; }
    else if (fname == "scaling_policy") { decl.scaling_policy_json = consume_nested_block_as_json(); }
    else if (fname == "human_in_the_loop") { decl.human_in_the_loop_json = consume_nested_block_as_json(); }
    else { error("Unknown decision_engine field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after decision_engine body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_event_bus_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected event_bus name");
  EventBusDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after event_bus name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in event_bus");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "emits") { decl.emits_json = consume_nested_block_as_json(); }
    else if (fname == "listens") { decl.listens_json = consume_nested_block_as_json(); }
    else if (fname == "routing") { decl.routing_json = consume_nested_block_as_json(); }
    else { error("Unknown event_bus field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after event_bus body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_drift_rca_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected drift_rca name");
  DriftRCADecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after drift_rca name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in drift_rca");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "causal_agent") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.causal_agent = previous().lexeme; }
    else if (fname == "investigation") { decl.investigation_json = consume_nested_block_as_json(); }
    else if (fname == "actions") { decl.actions_json = consume_nested_block_as_json(); }
    else { error("Unknown drift_rca field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after drift_rca body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_mlops_agent_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected mlops agent name");
  MLOpsAgentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after mlops agent name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in mlops agent");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
    else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
    else if (fname == "system") { if (!match(TokenType::String)) error("Expected string"); decl.system = previous().lexeme; }
    else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
    else if (fname == "endpoint") { if (!match(TokenType::String)) error("Expected string"); decl.endpoint = previous().lexeme; }
    else if (fname == "api_key_env") { if (!match(TokenType::String)) error("Expected string"); decl.api_key_env = previous().lexeme; }
    else if (fname == "agent_md") { if (!match(TokenType::String)) error("Expected string"); decl.agent_md = previous().lexeme; }
    else if (fname == "sub_agents") { decl.sub_agents_json = consume_nested_block_as_json(); }
    else if (fname == "drift_monitor") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.drift_monitor = previous().lexeme; }
    else if (fname == "retraining_pipeline") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.retraining_pipeline = previous().lexeme; }
    else if (fname == "deployment_strategy") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.deployment_strategy = previous().lexeme; }
    else if (fname == "champion_challenger") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.champion_challenger = previous().lexeme; }
    else if (fname == "serving_infra") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.serving_infra = previous().lexeme; }
    else if (fname == "training_infra") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.training_infra = previous().lexeme; }
    else if (fname == "rollback_policy") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.rollback_policy = previous().lexeme; }
    else if (fname == "monitoring_stack") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.monitoring_stack = previous().lexeme; }
    else if (fname == "mlflow") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.mlflow = previous().lexeme; }
    else if (fname == "business_kpi_tracker") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.business_kpi_tracker = previous().lexeme; }
    else if (fname == "feedback_loop") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.feedback_loop = previous().lexeme; }
    else if (fname == "decision_engine") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.decision_engine = previous().lexeme; }
    else if (fname == "event_bus") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.event_bus = previous().lexeme; }
    else if (fname == "coordinates_with")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.coordinates_with.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "handoffs")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.handoffs.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "role") { if (!match(TokenType::String)) error("Expected string"); decl.role = previous().lexeme; }
    else if (fname == "purpose") { if (!match(TokenType::String)) error("Expected string"); decl.purpose = previous().lexeme; }
    else if (fname == "autonomy") { if (!match(TokenType::String)) error("Expected string"); decl.autonomy = previous().lexeme; }
    else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.budget = previous().lexeme; }
    else { error("Unknown mlops agent field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after mlops agent body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_requirements_elicitation_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected requirements_elicitation name");
  RequirementsElicitationDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after requirements_elicitation name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in requirements_elicitation");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "stakeholders") { decl.stakeholders_json = consume_nested_block_as_json(); }
    else if (fname == "methods") { decl.methods_json = consume_nested_block_as_json(); }
    else if (fname == "output") { decl.output_json = consume_nested_block_as_json(); }
    else { error("Unknown requirements_elicitation field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after requirements_elicitation body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_brd_generator_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected brd_generator name");
  BRDGeneratorDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after brd_generator name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in brd_generator");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "project") { decl.project_json = consume_nested_block_as_json(); }
    else if (fname == "objectives") { decl.objectives_json = consume_nested_block_as_json(); }
    else if (fname == "scope") { decl.scope_json = consume_nested_block_as_json(); }
    else if (fname == "benefits") { decl.benefits_json = consume_nested_block_as_json(); }
    else if (fname == "constraints") { decl.constraints_json = consume_nested_block_as_json(); }
    else if (fname == "assumptions") { decl.assumptions_json = consume_nested_block_as_json(); }
    else if (fname == "risks") { decl.risks_json = consume_nested_block_as_json(); }
    else if (fname == "output") { decl.output_json = consume_nested_block_as_json(); }
    else { error("Unknown brd_generator field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after brd_generator body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_functional_spec_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected functional_spec name");
  FunctionalSpecDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after functional_spec name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in functional_spec");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "data_requirements") { decl.data_requirements_json = consume_nested_block_as_json(); }
    else if (fname == "etl_requirements") { decl.etl_requirements_json = consume_nested_block_as_json(); }
    else if (fname == "ml_requirements") { decl.ml_requirements_json = consume_nested_block_as_json(); }
    else if (fname == "analytics_requirements") { decl.analytics_requirements_json = consume_nested_block_as_json(); }
    else { error("Unknown functional_spec field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after functional_spec body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_nonfunctional_spec_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected nonfunctional_spec name");
  NonfunctionalSpecDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after nonfunctional_spec name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in nonfunctional_spec");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "performance") { decl.performance_json = consume_nested_block_as_json(); }
    else if (fname == "reliability") { decl.reliability_json = consume_nested_block_as_json(); }
    else if (fname == "security") { decl.security_json = consume_nested_block_as_json(); }
    else if (fname == "scalability") { decl.scalability_json = consume_nested_block_as_json(); }
    else if (fname == "data_quality") { decl.data_quality_json = consume_nested_block_as_json(); }
    else if (fname == "maintainability") { decl.maintainability_json = consume_nested_block_as_json(); }
    else { error("Unknown nonfunctional_spec field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after nonfunctional_spec body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_acceptance_criteria_gen_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected acceptance_criteria_generator name");
  AcceptanceCriteriaGenDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after acceptance_criteria_generator name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in acceptance_criteria_generator");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "patterns") { decl.patterns_json = consume_nested_block_as_json(); }
    else if (fname == "auto_generate") { if (match(TokenType::True)) decl.auto_generate = true; else if (match(TokenType::False)) decl.auto_generate = false; else error("Expected 'true' or 'false' for auto_generate"); }
    else if (fname == "review_required") { if (match(TokenType::True)) decl.review_required = true; else if (match(TokenType::False)) decl.review_required = false; else error("Expected 'true' or 'false' for review_required"); }
    else if (fname == "quality") { decl.quality_json = consume_nested_block_as_json(); }
    else { error("Unknown acceptance_criteria_generator field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after acceptance_criteria_generator body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_data_requirements_ba_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected data_requirements_ba name");
  DataRequirementsBADecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after data_requirements_ba name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in data_requirements_ba");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "source_to_target") { decl.source_to_target_json = consume_nested_block_as_json(); }
    else if (fname == "target_schema") { decl.target_schema_json = consume_nested_block_as_json(); }
    else if (fname == "quality_rules") { decl.quality_rules_json = consume_nested_block_as_json(); }
    else { error("Unknown data_requirements_ba field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after data_requirements_ba body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_impact_analysis_ba_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected impact_analysis_ba name");
  ImpactAnalysisBADecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after impact_analysis_ba name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in impact_analysis_ba");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "upstream") { decl.upstream_json = consume_nested_block_as_json(); }
    else if (fname == "downstream") { decl.downstream_json = consume_nested_block_as_json(); }
    else if (fname == "change_scenarios") { decl.change_scenarios_json = consume_nested_block_as_json(); }
    else if (fname == "lineage") { decl.lineage_json = consume_nested_block_as_json(); }
    else { error("Unknown impact_analysis_ba field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after impact_analysis_ba body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_traceability_matrix_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected traceability_matrix name");
  TraceabilityMatrixDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after traceability_matrix name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in traceability_matrix");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "entries") { decl.entries_json = consume_nested_block_as_json(); }
    else if (fname == "auto_trace") { decl.auto_trace_json = consume_nested_block_as_json(); }
    else if (fname == "reports") { decl.reports_json = consume_nested_block_as_json(); }
    else { error("Unknown traceability_matrix field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after traceability_matrix body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_etl_requirement_spec_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected etl_requirement_spec name");
  ETLRequirementSpecDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after etl_requirement_spec name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in etl_requirement_spec");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "pipeline") { decl.pipeline_json = consume_nested_block_as_json(); }
    else if (fname == "feature_groups") { decl.feature_groups_json = consume_nested_block_as_json(); }
    else if (fname == "quality_gates") { decl.quality_gates_json = consume_nested_block_as_json(); }
    else if (fname == "upstream_dependencies") { if (!match(TokenType::String)) error("Expected string"); decl.upstream_dependencies = previous().lexeme; }
    else if (fname == "downstream_consumers") { if (!match(TokenType::String)) error("Expected string"); decl.downstream_consumers = previous().lexeme; }
    else { error("Unknown etl_requirement_spec field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after etl_requirement_spec body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_ml_requirement_spec_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected ml_requirement_spec name");
  MLRequirementSpecDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after ml_requirement_spec name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in ml_requirement_spec");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "problem") { decl.problem_json = consume_nested_block_as_json(); }
    else if (fname == "success_criteria") { decl.success_criteria_json = consume_nested_block_as_json(); }
    else if (fname == "feature_requirements") { decl.feature_requirements_json = consume_nested_block_as_json(); }
    else if (fname == "serving") { decl.serving_json = consume_nested_block_as_json(); }
    else if (fname == "explainability") { decl.explainability_json = consume_nested_block_as_json(); }
    else if (fname == "monitoring") { decl.monitoring_json = consume_nested_block_as_json(); }
    else { error("Unknown ml_requirement_spec field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after ml_requirement_spec body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_governance_requirement_spec_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected governance_requirement_spec name");
  GovernanceRequirementSpecDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after governance_requirement_spec name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in governance_requirement_spec");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "data_classification") { decl.data_classification_json = consume_nested_block_as_json(); }
    else if (fname == "access_requirements") { decl.access_requirements_json = consume_nested_block_as_json(); }
    else if (fname == "compliance") { decl.compliance_json = consume_nested_block_as_json(); }
    else if (fname == "quality_sla") { decl.quality_sla_json = consume_nested_block_as_json(); }
    else { error("Unknown governance_requirement_spec field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after governance_requirement_spec body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_analytics_requirement_spec_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected analytics_requirement_spec name");
  AnalyticsRequirementSpecDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after analytics_requirement_spec name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in analytics_requirement_spec");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "reports") { decl.reports_json = consume_nested_block_as_json(); }
    else if (fname == "kpi_definitions") { decl.kpi_definitions_json = consume_nested_block_as_json(); }
    else { error("Unknown analytics_requirement_spec field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after analytics_requirement_spec body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_stakeholder_analysis_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected stakeholder_analysis name");
  StakeholderAnalysisDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after stakeholder_analysis name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in stakeholder_analysis");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "stakeholders") { decl.stakeholders_json = consume_nested_block_as_json(); }
    else if (fname == "raci_matrix") { decl.raci_matrix_json = consume_nested_block_as_json(); }
    else { error("Unknown stakeholder_analysis field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after stakeholder_analysis body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_user_story_generator_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected user_story_generator name");
  UserStoryGeneratorDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after user_story_generator name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in user_story_generator");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "source") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.source = previous().lexeme; }
    else if (fname == "epics") { decl.epics_json = consume_nested_block_as_json(); }
    else if (fname == "stories") { decl.stories_json = consume_nested_block_as_json(); }
    else if (fname == "generation") { decl.generation_json = consume_nested_block_as_json(); }
    else { error("Unknown user_story_generator field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after user_story_generator body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_scope_management_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected scope_management name");
  ScopeManagementDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after scope_management name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in scope_management");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "bcar") { decl.bcar_json = consume_nested_block_as_json(); }
    else if (fname == "change_management") { decl.change_management_json = consume_nested_block_as_json(); }
    else { error("Unknown scope_management field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after scope_management body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_change_impact_analyzer_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected change_impact_analyzer name");
  ChangeImpactAnalyzerDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after change_impact_analyzer name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in change_impact_analyzer");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "analysis") { decl.analysis_json = consume_nested_block_as_json(); }
    else if (fname == "output") { decl.output_json = consume_nested_block_as_json(); }
    else { error("Unknown change_impact_analyzer field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after change_impact_analyzer body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_databa_agent_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected databa agent name");
  DataBAAgentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after databa agent name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in databa agent");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
    else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
    else if (fname == "system") { if (!match(TokenType::String)) error("Expected string"); decl.system = previous().lexeme; }
    else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
    else if (fname == "endpoint") { if (!match(TokenType::String)) error("Expected string"); decl.endpoint = previous().lexeme; }
    else if (fname == "api_key_env") { if (!match(TokenType::String)) error("Expected string"); decl.api_key_env = previous().lexeme; }
    else if (fname == "agent_md") { if (!match(TokenType::String)) error("Expected string"); decl.agent_md = previous().lexeme; }
    else if (fname == "downstream_agents") { decl.downstream_agents_json = consume_nested_block_as_json(); }
    else if (fname == "elicitation") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.elicitation = previous().lexeme; }
    else if (fname == "brd") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.brd = previous().lexeme; }
    else if (fname == "functional_spec") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.functional_spec = previous().lexeme; }
    else if (fname == "nfr_spec") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.nfr_spec = previous().lexeme; }
    else if (fname == "data_requirements") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.data_requirements = previous().lexeme; }
    else if (fname == "impact_analysis") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.impact_analysis = previous().lexeme; }
    else if (fname == "traceability") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.traceability = previous().lexeme; }
    else if (fname == "etl_spec") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.etl_spec = previous().lexeme; }
    else if (fname == "ml_spec") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.ml_spec = previous().lexeme; }
    else if (fname == "governance_spec") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.governance_spec = previous().lexeme; }
    else if (fname == "analytics_spec") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.analytics_spec = previous().lexeme; }
    else if (fname == "stakeholders") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.stakeholders = previous().lexeme; }
    else if (fname == "user_stories") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.user_stories = previous().lexeme; }
    else if (fname == "scope") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.scope = previous().lexeme; }
    else if (fname == "coordinates_with")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.coordinates_with.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "handoffs")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.handoffs.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "role") { if (!match(TokenType::String)) error("Expected string"); decl.role = previous().lexeme; }
    else if (fname == "purpose") { if (!match(TokenType::String)) error("Expected string"); decl.purpose = previous().lexeme; }
    else if (fname == "autonomy") { if (!match(TokenType::String)) error("Expected string"); decl.autonomy = previous().lexeme; }
    else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.budget = previous().lexeme; }
    else { error("Unknown databa agent field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after databa agent body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_test_strategy_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected test_strategy name");
  TestStrategyDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after test_strategy name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in test_strategy");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "source_spec") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.source_spec = previous().lexeme; }
    else if (fname == "source_nfr") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.source_nfr = previous().lexeme; }
    else if (fname == "levels") { decl.levels_json = consume_nested_block_as_json(); }
    else if (fname == "test_data") { decl.test_data_json = consume_nested_block_as_json(); }
    else if (fname == "gates") { decl.gates_json = consume_nested_block_as_json(); }
    else if (fname == "reporting") { decl.reporting_json = consume_nested_block_as_json(); }
    else { error("Unknown test_strategy field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after test_strategy body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_test_case_generator_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected test_case_generator name");
  TestCaseGeneratorDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after test_case_generator name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in test_case_generator");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "source") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.source = previous().lexeme; }
    else if (fname == "generation") { decl.generation_json = consume_nested_block_as_json(); }
    else if (fname == "output") { decl.output_json = consume_nested_block_as_json(); }
    else { error("Unknown test_case_generator field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after test_case_generator body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_test_case_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected test_case name");
  TestCaseDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after test_case name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in test_case");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "requirement") { if (!match(TokenType::String)) error("Expected string"); decl.requirement = previous().lexeme; }
    else if (fname == "acceptance_criteria") { if (!match(TokenType::String)) error("Expected string"); decl.acceptance_criteria = previous().lexeme; }
    else if (fname == "type") { if (!match(TokenType::String)) error("Expected string"); decl.type = previous().lexeme; }
    else if (fname == "priority") { if (!match(TokenType::String)) error("Expected string"); decl.priority = previous().lexeme; }
    else if (fname == "preconditions") { decl.preconditions_json = consume_nested_block_as_json(); }
    else if (fname == "steps") { decl.steps_json = consume_nested_block_as_json(); }
    else if (fname == "expected_result") { if (!match(TokenType::String)) error("Expected string"); decl.expected_result = previous().lexeme; }
    else if (fname == "automation") { decl.automation_json = consume_nested_block_as_json(); }
    else { error("Unknown test_case field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after test_case body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_etl_test_suite_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected etl_test_suite name");
  ETLTestSuiteDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after etl_test_suite name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in etl_test_suite");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "pipeline") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.pipeline = previous().lexeme; }
    else if (fname == "connection") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.connection = previous().lexeme; }
    else if (fname == "tests") { decl.tests_json = consume_nested_block_as_json(); }
    else { error("Unknown etl_test_suite field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after etl_test_suite body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_dw_test_suite_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected dw_test_suite name");
  DWTestSuiteDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after dw_test_suite name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in dw_test_suite");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "connection") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.connection = previous().lexeme; }
    else if (fname == "tests") { decl.tests_json = consume_nested_block_as_json(); }
    else { error("Unknown dw_test_suite field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after dw_test_suite body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_ml_test_suite_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected ml_test_suite name");
  MLTestSuiteDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after ml_test_suite name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in ml_test_suite");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "model") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.model = previous().lexeme; }
    else if (fname == "tests") { decl.tests_json = consume_nested_block_as_json(); }
    else { error("Unknown ml_test_suite field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after ml_test_suite body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_api_test_suite_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected api_test_suite name");
  APITestSuiteDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after api_test_suite name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in api_test_suite");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "endpoint") { if (!match(TokenType::String)) error("Expected string"); decl.endpoint = previous().lexeme; }
    else if (fname == "auth") { decl.auth_json = consume_nested_block_as_json(); }
    else if (fname == "tests") { decl.tests_json = consume_nested_block_as_json(); }
    else { error("Unknown api_test_suite field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after api_test_suite body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_performance_test_suite_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected performance_test_suite name");
  PerformanceTestSuiteDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after performance_test_suite name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in performance_test_suite");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "pipeline_performance") { decl.pipeline_performance_json = consume_nested_block_as_json(); }
    else if (fname == "query_performance") { decl.query_performance_json = consume_nested_block_as_json(); }
    else if (fname == "api_performance") { decl.api_performance_json = consume_nested_block_as_json(); }
    else { error("Unknown performance_test_suite field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after performance_test_suite body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_edge_case_tests_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected edge_case_tests name");
  EdgeCaseTestsDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after edge_case_tests name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in edge_case_tests");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "generation") { decl.generation_json = consume_nested_block_as_json(); }
    else if (fname == "tests") { decl.tests_json = consume_nested_block_as_json(); }
    else { error("Unknown edge_case_tests field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after edge_case_tests body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_sit_suite_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected sit_suite name");
  SITSuiteDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after sit_suite name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in sit_suite");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "scope") { if (!match(TokenType::String)) error("Expected string"); decl.scope = previous().lexeme; }
    else if (fname == "tests") { decl.tests_json = consume_nested_block_as_json(); }
    else { error("Unknown sit_suite field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after sit_suite body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_uat_suite_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected uat_suite name");
  UATSuiteDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after uat_suite name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in uat_suite");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "business_validation") { decl.business_validation_json = consume_nested_block_as_json(); }
    else if (fname == "data_quality_uat") { decl.data_quality_uat_json = consume_nested_block_as_json(); }
    else { error("Unknown uat_suite field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after uat_suite body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_regression_suite_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected regression_suite name");
  RegressionSuiteDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after regression_suite name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in regression_suite");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "baseline") { decl.baseline_json = consume_nested_block_as_json(); }
    else if (fname == "checks") { decl.checks_json = consume_nested_block_as_json(); }
    else if (fname == "trigger") { if (!match(TokenType::String)) error("Expected string"); decl.trigger = previous().lexeme; }
    else { error("Unknown regression_suite field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after regression_suite body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_quality_gate_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected quality_gate name");
  QualityGateDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after quality_gate name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in quality_gate");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "data_quality_gate") { decl.data_quality_gate_json = consume_nested_block_as_json(); }
    else if (fname == "model_quality_gate") { decl.model_quality_gate_json = consume_nested_block_as_json(); }
    else if (fname == "api_quality_gate") { decl.api_quality_gate_json = consume_nested_block_as_json(); }
    else if (fname == "performance_gate") { decl.performance_gate_json = consume_nested_block_as_json(); }
    else if (fname == "uat_gate") { decl.uat_gate_json = consume_nested_block_as_json(); }
    else if (fname == "deployment_decision") { decl.deployment_decision_json = consume_nested_block_as_json(); }
    else { error("Unknown quality_gate field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after quality_gate body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_test_report_config_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected test_report_config name");
  TestReportConfigDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after test_report_config name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in test_report_config");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "execution") { decl.execution_json = consume_nested_block_as_json(); }
    else if (fname == "report") { decl.report_json = consume_nested_block_as_json(); }
    else if (fname == "notify") { decl.notify_json = consume_nested_block_as_json(); }
    else { error("Unknown test_report_config field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after test_report_config body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_defect_management_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected defect_management name");
  DefectManagementDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after defect_management name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in defect_management");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "on_failure") { decl.on_failure_json = consume_nested_block_as_json(); }
    else if (fname == "rca") { decl.rca_json = consume_nested_block_as_json(); }
    else if (fname == "tracking") { decl.tracking_json = consume_nested_block_as_json(); }
    else { error("Unknown defect_management field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after defect_management body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_datatest_agent_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected datatest agent name");
  DataTestAgentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after datatest agent name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in datatest agent");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
    else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
    else if (fname == "system") { if (!match(TokenType::String)) error("Expected string"); decl.system = previous().lexeme; }
    else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
    else if (fname == "agent_md") { if (!match(TokenType::String)) error("Expected string"); decl.agent_md = previous().lexeme; }
    else if (fname == "sub_agents") { decl.sub_agents_json = consume_nested_block_as_json(); }
    else if (fname == "forge") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.forge = previous().lexeme; }
    else if (fname == "test_strategy") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.test_strategy = previous().lexeme; }
    else if (fname == "test_generator") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.test_generator = previous().lexeme; }
    else if (fname == "etl_tests") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.etl_tests = previous().lexeme; }
    else if (fname == "dw_tests") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.dw_tests = previous().lexeme; }
    else if (fname == "ml_tests") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.ml_tests = previous().lexeme; }
    else if (fname == "api_tests") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.api_tests = previous().lexeme; }
    else if (fname == "performance_tests") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.performance_tests = previous().lexeme; }
    else if (fname == "edge_tests") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.edge_tests = previous().lexeme; }
    else if (fname == "sit_suite") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.sit_suite = previous().lexeme; }
    else if (fname == "uat_suite") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.uat_suite = previous().lexeme; }
    else if (fname == "regression_suite") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.regression_suite = previous().lexeme; }
    else if (fname == "quality_gate") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.quality_gate = previous().lexeme; }
    else if (fname == "report_config") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.report_config = previous().lexeme; }
    else if (fname == "defect_mgmt") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.defect_mgmt = previous().lexeme; }
    else if (fname == "coordinates_with")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.coordinates_with.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "handoffs")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.handoffs.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "role") { if (!match(TokenType::String)) error("Expected string"); decl.role = previous().lexeme; }
    else if (fname == "purpose") { if (!match(TokenType::String)) error("Expected string"); decl.purpose = previous().lexeme; }
    else if (fname == "autonomy") { if (!match(TokenType::String)) error("Expected string"); decl.autonomy = previous().lexeme; }
    else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.budget = previous().lexeme; }
    else { error("Unknown datatest agent field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after datatest agent body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_agent_registry_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected agent_registry name");
  AgentRegistryDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after agent_registry name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in agent_registry");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "agents") { decl.agents_json = consume_nested_block_as_json(); }
    else { error("Unknown agent_registry field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after agent_registry body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_agent_contracts_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected agent_contracts name");
  AgentContractsDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after agent_contracts name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in agent_contracts");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "contracts") { decl.contracts_json = consume_nested_block_as_json(); }
    else { error("Unknown agent_contracts field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after agent_contracts body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_raci_matrix_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected raci_matrix name");
  RACIMatrixDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after raci_matrix name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in raci_matrix");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "tasks") { decl.tasks_json = consume_nested_block_as_json(); }
    else { error("Unknown raci_matrix field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after raci_matrix body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_task_understanding_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected task_understanding name");
  TaskUnderstandingDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after task_understanding name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in task_understanding");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "intent_classifier") { decl.intent_classifier_json = consume_nested_block_as_json(); }
    else if (fname == "detail_extraction") { decl.detail_extraction_json = consume_nested_block_as_json(); }
    else { error("Unknown task_understanding field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after task_understanding body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_task_decomposer_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected task_decomposer name");
  TaskDecomposerDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after task_decomposer name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in task_decomposer");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "strategy") { decl.strategy_json = consume_nested_block_as_json(); }
    else if (fname == "output") { decl.output_json = consume_nested_block_as_json(); }
    else { error("Unknown task_decomposer field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after task_decomposer body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_crew_formation_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected crew_formation name");
  CrewFormationDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after crew_formation name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in crew_formation");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "strategy") { decl.strategy_json = consume_nested_block_as_json(); }
    else { error("Unknown crew_formation field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after crew_formation body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_pattern_selector_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected pattern_selector name");
  PatternSelectorDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after pattern_selector name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in pattern_selector");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "patterns") { decl.patterns_json = consume_nested_block_as_json(); }
    else if (fname == "selection") { decl.selection_json = consume_nested_block_as_json(); }
    else { error("Unknown pattern_selector field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after pattern_selector body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_execution_manager_dio_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected execution_manager_dio name");
  ExecutionManagerDIODecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after execution_manager_dio name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in execution_manager_dio");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "modes") { decl.modes_json = consume_nested_block_as_json(); }
    else if (fname == "resources") { decl.resources_json = consume_nested_block_as_json(); }
    else if (fname == "monitoring") { decl.monitoring_json = consume_nested_block_as_json(); }
    else { error("Unknown execution_manager_dio field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after execution_manager_dio body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_dio_state_machine_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected dio_state_machine name");
  DIOStateMachineDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after dio_state_machine name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in dio_state_machine");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "states") { decl.states_json = consume_nested_block_as_json(); }
    else if (fname == "transitions") { decl.transitions_json = consume_nested_block_as_json(); }
    else if (fname == "persistence") { decl.persistence_json = consume_nested_block_as_json(); }
    else { error("Unknown dio_state_machine field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after dio_state_machine body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_dio_error_handling_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected dio_error_handling name");
  DIOErrorHandlingDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after dio_error_handling name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in dio_error_handling");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "strategies") { decl.strategies_json = consume_nested_block_as_json(); }
    else if (fname == "self_healing") { decl.self_healing_json = consume_nested_block_as_json(); }
    else { error("Unknown dio_error_handling field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after dio_error_handling body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_result_synthesizer_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected result_synthesizer name");
  ResultSynthesizerDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after result_synthesizer name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in result_synthesizer");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "synthesis") { decl.synthesis_json = consume_nested_block_as_json(); }
    else if (fname == "delivery") { decl.delivery_json = consume_nested_block_as_json(); }
    else { error("Unknown result_synthesizer field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after result_synthesizer body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_infrastructure_profile_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected infrastructure_profile name");
  InfrastructureProfileDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after infrastructure_profile name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in infrastructure_profile");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "data_warehouse") { decl.data_warehouse_json = consume_nested_block_as_json(); }
    else if (fname == "data_lake") { decl.data_lake_json = consume_nested_block_as_json(); }
    else if (fname == "databases") { decl.databases_json = consume_nested_block_as_json(); }
    else if (fname == "streaming") { decl.streaming_json = consume_nested_block_as_json(); }
    else if (fname == "data_science") { decl.data_science_json = consume_nested_block_as_json(); }
    else if (fname == "governance") { decl.governance_json = consume_nested_block_as_json(); }
    else if (fname == "cicd") { decl.cicd_json = consume_nested_block_as_json(); }
    else { error("Unknown infrastructure_profile field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after infrastructure_profile body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_role_framework_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected role_framework name");
  RoleFrameworkDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after role_framework name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in role_framework");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "roles") { decl.roles_json = consume_nested_block_as_json(); }
    else if (fname == "dio_role") { decl.dio_role_json = consume_nested_block_as_json(); }
    else { error("Unknown role_framework field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after role_framework body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_delegation_protocol_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected delegation_protocol name");
  DelegationProtocolDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after delegation_protocol name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in delegation_protocol");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "delegation") { decl.delegation_json = consume_nested_block_as_json(); }
    else { error("Unknown delegation_protocol field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after delegation_protocol body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_dio_accountability_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected dio_accountability name");
  DIOAccountabilityDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after dio_accountability name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in dio_accountability");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "escalation") { decl.escalation_json = consume_nested_block_as_json(); }
    else { error("Unknown dio_accountability field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after dio_accountability body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

StmtPtr Parser::parse_dio_agent_decl(const Visibility& visibility)
{
  auto span = previous().line;
  if (!match(TokenType::Identifier)) error("Expected dio agent name");
  DIOAgentDecl decl;
  decl.visibility = visibility;
  decl.name = previous().lexeme;
  if (!match(TokenType::LeftBrace)) error("Expected '{' after dio agent name");

  while (!check(TokenType::RightBrace) && !is_at_end())
  {
    if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name in dio agent");
    std::string fname = previous().lexeme;
    if (!match(TokenType::Colon)) error("Expected ':' after field name");

    if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
    else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
    else if (fname == "system") { if (!match(TokenType::String)) error("Expected string"); decl.system = previous().lexeme; }
    else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
    else if (fname == "mode") { if (!match(TokenType::String)) error("Expected string"); decl.mode = previous().lexeme; }
    else if (fname == "task") { if (!match(TokenType::String)) error("Expected string"); decl.task = previous().lexeme; }
    else if (fname == "agent_md") { if (!match(TokenType::String)) error("Expected string"); decl.agent_md = previous().lexeme; }
    else if (fname == "infrastructure") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.infrastructure = previous().lexeme; }
    else if (fname == "agent_registry") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.agent_registry = previous().lexeme; }
    else if (fname == "raci_matrix") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.raci_matrix = previous().lexeme; }
    else if (fname == "pattern_selector") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.pattern_selector = previous().lexeme; }
    else if (fname == "crew_formation") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.crew_formation = previous().lexeme; }
    else if (fname == "execution_manager") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.execution_manager = previous().lexeme; }
    else if (fname == "state_machine") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.state_machine = previous().lexeme; }
    else if (fname == "error_handling") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.error_handling = previous().lexeme; }
    else if (fname == "result_synthesizer") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.result_synthesizer = previous().lexeme; }
    else if (fname == "managed_agents") { decl.managed_agents_json = consume_nested_block_as_json(); }
    else if (fname == "guardrails") { decl.guardrails_json = consume_nested_block_as_json(); }
    else if (fname == "coordinates_with")
    {
      if (!match(TokenType::LeftBracket)) error("Expected '['");
      while (!check(TokenType::RightBracket) && !is_at_end()) { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.coordinates_with.push_back(previous().lexeme); match(TokenType::Comma); }
      if (!match(TokenType::RightBracket)) error("Expected ']'");
    }
    else if (fname == "role") { if (!match(TokenType::String)) error("Expected string"); decl.role = previous().lexeme; }
    else if (fname == "purpose") { if (!match(TokenType::String)) error("Expected string"); decl.purpose = previous().lexeme; }
    else if (fname == "autonomy") { if (!match(TokenType::String)) error("Expected string"); decl.autonomy = previous().lexeme; }
    else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected identifier"); decl.budget = previous().lexeme; }
    else { error("Unknown dio agent field: " + fname); }
    match(TokenType::Comma);
  }
  if (!match(TokenType::RightBrace)) error("Expected '}' after dio agent body");

  auto stmt = std::make_unique<Statement>();
  stmt->span = {0, 0, span, 0};
  stmt->node = std::move(decl);
  return stmt;
}

// ═══════════════════════════════════════════════════════════════════════════
// v1.0: consume_json_block — reads { ... } as a string, handling nesting
// ═══════════════════════════════════════════════════════════════════════════
std::string Parser::consume_json_block() {
    if (!match(TokenType::LeftBrace)) error("Expected '{' for JSON block");
    std::string result = "{";
    int depth = 1;
    while (depth > 0 && !is_at_end()) {
        if (check(TokenType::LeftBrace)) { result += "{"; depth++; advance(); continue; }
        if (check(TokenType::RightBrace)) { depth--; if (depth > 0) { result += "}"; advance(); continue; } else break; }
        if (check(TokenType::Comma)) { result += ","; advance(); continue; }
        if (check(TokenType::Colon)) { result += ":"; advance(); continue; }
        if (check(TokenType::LeftBracket)) { result += "["; advance(); continue; }
        if (check(TokenType::RightBracket)) { result += "]"; advance(); continue; }
        if (check(TokenType::String)) { result += "\"" + peek().lexeme + "\""; advance(); continue; }
        if (check(TokenType::Number)) { result += peek().lexeme; advance(); continue; }
        if (check(TokenType::True)) { result += "true"; advance(); continue; }
        if (check(TokenType::False)) { result += "false"; advance(); continue; }
        result += "\"" + peek().lexeme + "\""; advance();
    }
    if (!match(TokenType::RightBrace)) error("Expected '}' to close JSON block");
    result += "}";
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// v1.0: Generic declaration parser for security/cloud/eval constructs
// All share the pattern: keyword Name { field: value, ... }
// Fields are read as key-value pairs and stored as JSON
// ═══════════════════════════════════════════════════════════════════════════

StmtPtr Parser::parse_v10_generic_decl(const std::string& keyword, int type_id) {
    if (!match(TokenType::Identifier)) error("Expected name after '" + keyword + "'");
    std::string name = previous().lexeme;
    if (!match(TokenType::LeftBrace)) error("Expected '{' after " + keyword + " name");

    // Read all fields as JSON blob
    std::string fields_json = "{";
    bool first = true;
    int brace_depth = 1;
    while (brace_depth > 0 && !is_at_end()) {
        if (check(TokenType::LeftBrace)) { brace_depth++; fields_json += "{"; advance(); continue; }
        if (check(TokenType::RightBrace)) { brace_depth--; if (brace_depth > 0) { fields_json += "}"; advance(); continue; } else break; }
        if (!first && check(TokenType::Comma)) { fields_json += ","; advance(); continue; }
        if (check(TokenType::Colon)) { fields_json += ":"; advance(); continue; }
        if (check(TokenType::LeftBracket)) { fields_json += "["; advance(); continue; }
        if (check(TokenType::RightBracket)) { fields_json += "]"; advance(); continue; }
        if (check(TokenType::String)) { fields_json += "\"" + peek().lexeme + "\""; advance(); first = false; continue; }
        if (check(TokenType::Number)) { fields_json += peek().lexeme; advance(); first = false; continue; }
        if (check(TokenType::True) || check(TokenType::False)) { fields_json += peek().lexeme; advance(); first = false; continue; }
        // Identifier (field name or reference)
        fields_json += "\"" + peek().lexeme + "\"";
        advance();
        first = false;
    }
    if (!match(TokenType::RightBrace)) error("Expected '}' after " + keyword + " body");
    fields_json += "}";

    auto span = previous().line;

    // Create appropriate AST node based on type_id
    auto stmt = std::make_unique<Statement>();
    stmt->span = {0, 0, span, 0};

    switch (type_id) {
    case 1: { GoalIntegrityDecl d; d.name = name; d.verification_json = fields_json; stmt->node = std::move(d); break; }
    case 2: { ToolValidatorDecl d; d.name = name; d.rate_limits_json = fields_json; stmt->node = std::move(d); break; }
    case 3: { AgentIdentityDecl d; d.name = name; d.scope_json = fields_json; stmt->node = std::move(d); break; }
    case 4: { SupplyChainPolicyDecl d; d.name = name; d.agent_md_signing_json = fields_json; stmt->node = std::move(d); break; }
    case 5: { CodeSandboxDecl d; d.name = name; d.resources_json = fields_json; stmt->node = std::move(d); break; }
    case 6: { MemoryIntegrityDecl d; d.name = name; d.provenance_json = fields_json; stmt->node = std::move(d); break; }
    case 7: { MessageSecurityDecl d; d.name = name; d.signing_json = fields_json; stmt->node = std::move(d); break; }
    case 8: { CircuitBreakerV10Decl d; d.name = name; d.isolation_json = fields_json; stmt->node = std::move(d); break; }
    case 9: { HumanGateDecl d; d.name = name; d.workflow_json = fields_json; stmt->node = std::move(d); break; }
    case 10: { AgentAttestationDecl d; d.name = name; d.baseline_json = fields_json; stmt->node = std::move(d); break; }
    case 11: { MCPAllowlistDecl d; d.name = name; d.servers_json = fields_json; stmt->node = std::move(d); break; }
    case 12: { ToolPinningDecl d; d.name = name; d.method = "sha256"; stmt->node = std::move(d); break; }
    case 13: { ContextGuardDecl d; d.name = name; d.cross_task_sharing = fields_json; stmt->node = std::move(d); break; }
    case 14: { AIBOMConfigDecl d; d.name = name; d.components_json = fields_json; stmt->node = std::move(d); break; }
    case 15: { GymEvaluatorDecl d; d.name = name; d.graders_json = fields_json; stmt->node = std::move(d); break; }
    case 16: { GatewayDecl d; d.name = name; d.routes_json = fields_json; stmt->node = std::move(d); break; }
    case 17: { ModelRouterDecl d; d.name = name; d.routes_json = fields_json; stmt->node = std::move(d); break; }
    case 18: { MarketplaceV10Decl d; d.name = name; d.package_format_json = fields_json; stmt->node = std::move(d); break; }
    // v1.1: NeamOS Foundation
    case 19: { KnowledgeCardDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 20: { ContextAssemblyDecl d; d.name = name; d.target_agent_ref = fields_json; stmt->node = std::move(d); break; }
    case 21: { AgentPersonaDecl d; d.name = name; d.personality_json = fields_json; stmt->node = std::move(d); break; }
    case 22: { LocaleConfigDecl d; d.name = name; d.string_table = fields_json; stmt->node = std::move(d); break; }
    case 23: { GovernanceRuleDecl d; d.name = name; d.condition_json = fields_json; stmt->node = std::move(d); break; }
    case 24: { AgentAdapterDecl d; d.name = name; d.capabilities_json = fields_json; stmt->node = std::move(d); break; }
    case 25: { BlueprintDecl d; d.name = name; d.parameters_json = fields_json; stmt->node = std::move(d); break; }
    // v1.2: NeamProd
    case 26: { PluginDecl d; d.name = name; d.hooks_json = fields_json; stmt->node = std::move(d); break; }
    case 27: { SessionServiceDecl d; d.name = name; d.connection_json = fields_json; stmt->node = std::move(d); break; }
    case 28: { EvalTestDecl d; d.name = name; d.criteria_json = fields_json; stmt->node = std::move(d); break; }
    case 29: { EvalSetDecl d; d.name = name; d.thresholds_json = fields_json; stmt->node = std::move(d); break; }
    case 30: { ArtifactStoreDecl d; d.name = name; d.metadata_json = fields_json; stmt->node = std::move(d); break; }
    case 31: { StreamConfigDecl d; d.name = name; d.voice_json = fields_json; stmt->node = std::move(d); break; }
    case 32: { A2AConfigDecl d; d.name = name; d.agent_card_json = fields_json; stmt->node = std::move(d); break; }
    // v1.3: NeamLab
    case 33: { ProgramDecl d; d.name = name; d.metadata_json = fields_json; stmt->node = std::move(d); break; }
    case 34: { ExperimentLoopDecl d; d.name = name; d.until_json = fields_json; stmt->node = std::move(d); break; }
    case 35: { MetricExtractorDecl d; d.name = name; d.pattern = fields_json; stmt->node = std::move(d); break; }
    // v1.4: NeamWiki
    case 36: { WikiDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    // v1.4.5: NeamHarness (phase 0 — generic blob; full field parsing in later phase)
    case 37: { HarnessDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 38: { HandoffDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 39: { ToolRegistryDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 40: { AssertionRegistryDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 41: { HarnessBenchmarkDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    // v1.5: NeamEvolve
    case 42: { EvolveAgentDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 43: { BeliefDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 44: { SkillLibraryDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 45: { CurriculumDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    // v1.6: NeamMesh
    case 46: { ProcessDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 47: { TaskDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 48: { DecisionDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 49: { EventDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    case 50: { PoolDecl d; d.name = name; d.fields_json = fields_json; stmt->node = std::move(d); break; }
    default: error("Unknown v1.0 declaration type: " + keyword); break;
    }

    return stmt;
}

// v1.0: Special agent parsers (2-keyword pattern: "securitysentinel agent Name { ... }")
StmtPtr Parser::parse_security_sentinel_agent_decl(const Visibility& vis) {
    auto span = previous().line;
    if (!match(TokenType::Identifier)) error("Expected agent name");
    SecuritySentinelAgentDecl decl;
    decl.visibility = vis;
    decl.name = previous().lexeme;
    if (!match(TokenType::LeftBrace)) error("Expected '{'");

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        std::string fname = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");

        if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
        else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
        else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
        else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.budget = previous().lexeme; }
        else if (fname == "monitors") { decl.monitors_json = consume_json_block(); }
        else if (fname == "actions") { decl.actions_json = consume_json_block(); }
        else if (fname == "reporting") { decl.reporting_json = consume_json_block(); }
        else { advance(); /* skip unknown field value */ }
        match(TokenType::Comma);
    }
    if (!match(TokenType::RightBrace)) error("Expected '}'");
    auto stmt = std::make_unique<Statement>();
    stmt->span = {0, 0, span, 0};
    stmt->node = std::move(decl);
    return stmt;
}

StmtPtr Parser::parse_protocol_bridge_agent_decl(const Visibility& vis) {
    auto span = previous().line;
    if (!match(TokenType::Identifier)) error("Expected agent name");
    ProtocolBridgeAgentDecl decl;
    decl.visibility = vis;
    decl.name = previous().lexeme;
    if (!match(TokenType::LeftBrace)) error("Expected '{'");

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        std::string fname = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");

        if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
        else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
        else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
        else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.budget = previous().lexeme; }
        else if (fname == "protocols") { decl.protocols_json = consume_json_block(); }
        else if (fname == "firewall") { decl.firewall_json = consume_json_block(); }
        else { advance(); /* skip unknown field value */ }
        match(TokenType::Comma);
    }
    if (!match(TokenType::RightBrace)) error("Expected '}'");
    auto stmt = std::make_unique<Statement>();
    stmt->span = {0, 0, span, 0};
    stmt->node = std::move(decl);
    return stmt;
}

StmtPtr Parser::parse_cost_guardian_agent_decl(const Visibility& vis) {
    auto span = previous().line;
    if (!match(TokenType::Identifier)) error("Expected agent name");
    CostGuardianAgentDecl decl;
    decl.visibility = vis;
    decl.name = previous().lexeme;
    if (!match(TokenType::LeftBrace)) error("Expected '{'");

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        std::string fname = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");

        if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
        else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
        else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
        else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.budget = previous().lexeme; }
        else if (fname == "tracking") { decl.tracking_json = consume_json_block(); }
        else if (fname == "optimization") { decl.optimization_json = consume_json_block(); }
        else if (fname == "alerts") { decl.alerts_json = consume_json_block(); }
        else { advance(); /* skip unknown field value */ }
        match(TokenType::Comma);
    }
    if (!match(TokenType::RightBrace)) error("Expected '}'");
    auto stmt = std::make_unique<Statement>();
    stmt->span = {0, 0, span, 0};
    stmt->node = std::move(decl);
    return stmt;
}

// v1.1: KnowledgeWeaver agent parser
StmtPtr Parser::parse_knowledge_weaver_agent_decl(const Visibility& vis) {
    auto span = previous().line;
    if (!match(TokenType::Identifier)) error("Expected agent name");
    KnowledgeWeaverAgentDecl decl;
    decl.visibility = vis;
    decl.name = previous().lexeme;
    if (!match(TokenType::LeftBrace)) error("Expected '{'");

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        std::string fname = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");

        if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
        else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
        else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
        else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.budget = previous().lexeme; }
        else if (fname == "fabric") { decl.fabric_json = consume_json_block(); }
        else if (fname == "monitors") { decl.monitors_json = consume_json_block(); }
        else { advance(); }
        match(TokenType::Comma);
    }
    if (!match(TokenType::RightBrace)) error("Expected '}'");
    auto stmt = std::make_unique<Statement>();
    stmt->span = {0, 0, span, 0};
    stmt->node = std::move(decl);
    return stmt;
}

// v1.1: AdaptAgent parser
StmtPtr Parser::parse_adapt_agent_decl(const Visibility& vis) {
    auto span = previous().line;
    if (!match(TokenType::Identifier)) error("Expected agent name");
    AdaptAgentDecl decl;
    decl.visibility = vis;
    decl.name = previous().lexeme;
    if (!match(TokenType::LeftBrace)) error("Expected '{'");

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        std::string fname = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");

        if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
        else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
        else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
        else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.budget = previous().lexeme; }
        else if (fname == "monitors") { decl.monitors_json = consume_json_block(); }
        else if (fname == "proposals") { decl.proposals_json = consume_json_block(); }
        else { advance(); }
        match(TokenType::Comma);
    }
    if (!match(TokenType::RightBrace)) error("Expected '}'");
    auto stmt = std::make_unique<Statement>();
    stmt->span = {0, 0, span, 0};
    stmt->node = std::move(decl);
    return stmt;
}

// v1.1: Storyteller agent parser
StmtPtr Parser::parse_storyteller_agent_decl(const Visibility& vis) {
    auto span = previous().line;
    if (!match(TokenType::Identifier)) error("Expected agent name");
    StorytellerAgentDecl decl;
    decl.visibility = vis;
    decl.name = previous().lexeme;
    if (!match(TokenType::LeftBrace)) error("Expected '{'");

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        std::string fname = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");

        if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
        else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
        else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
        else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.budget = previous().lexeme; }
        else if (fname == "sub_agents") { decl.sub_agents_json = consume_json_block(); }
        else if (fname == "safety") { decl.safety_json = consume_json_block(); }
        else { advance(); }
        match(TokenType::Comma);
    }
    if (!match(TokenType::RightBrace)) error("Expected '}'");
    auto stmt = std::make_unique<Statement>();
    stmt->span = {0, 0, span, 0};
    stmt->node = std::move(decl);
    return stmt;
}

// v1.3: Research agent parser
StmtPtr Parser::parse_research_agent_decl(const Visibility& vis) {
    auto span = previous().line;
    if (!match(TokenType::Identifier)) error("Expected agent name");
    ResearchAgentDecl decl;
    decl.visibility = vis;
    decl.name = previous().lexeme;
    if (!match(TokenType::LeftBrace)) error("Expected '{'");

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        std::string fname = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");

        if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
        else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
        else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
        else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.budget = previous().lexeme; }
        else if (fname == "program") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.program_ref = previous().lexeme; }
        else if (fname == "metric") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.metric_ref = previous().lexeme; }
        else if (fname == "experiment_log") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.experiment_log_ref = previous().lexeme; }
        else if (fname == "eval_set") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.eval_set_ref = previous().lexeme; }
        else if (fname == "iteration_budget") { decl.iteration_budget_json = consume_json_block(); }
        else if (fname == "mutable_artifacts") {
            // Consume array literal: [ "file1", "file2", ... ]
            if (!match(TokenType::LeftBracket)) error("Expected '[' for mutable_artifacts");
            std::string arr = "[";
            int depth = 1;
            while (depth > 0 && !is_at_end()) {
                if (check(TokenType::LeftBracket)) { depth++; arr += "["; advance(); continue; }
                if (check(TokenType::RightBracket)) { depth--; if (depth > 0) { arr += "]"; advance(); continue; } else break; }
                if (check(TokenType::String)) { arr += "\"" + previous().lexeme; advance(); arr += "\""; continue; }
                if (check(TokenType::Comma)) { arr += ","; advance(); continue; }
                advance();
            }
            if (!match(TokenType::RightBracket)) error("Expected ']' for mutable_artifacts");
            arr += "]";
            decl.mutable_artifacts_json = arr;
        }
        else if (fname == "immutable_artifacts") {
            if (!match(TokenType::LeftBracket)) error("Expected '[' for immutable_artifacts");
            std::string arr = "[";
            int depth = 1;
            while (depth > 0 && !is_at_end()) {
                if (check(TokenType::LeftBracket)) { depth++; arr += "["; advance(); continue; }
                if (check(TokenType::RightBracket)) { depth--; if (depth > 0) { arr += "]"; advance(); continue; } else break; }
                if (check(TokenType::String)) { arr += "\"" + previous().lexeme; advance(); arr += "\""; continue; }
                if (check(TokenType::Comma)) { arr += ","; advance(); continue; }
                advance();
            }
            if (!match(TokenType::RightBracket)) error("Expected ']' for immutable_artifacts");
            arr += "]";
            decl.immutable_artifacts_json = arr;
        }
        else if (fname == "on_improvement") { if (!match(TokenType::String)) error("Expected string"); decl.on_improvement = previous().lexeme; }
        else if (fname == "on_failure") { if (!match(TokenType::String)) error("Expected string"); decl.on_failure = previous().lexeme; }
        else if (fname == "hypothesis_required") {
            if (match(TokenType::True)) decl.hypothesis_required = true;
            else if (match(TokenType::False)) decl.hypothesis_required = false;
            else { advance(); }
        }
        else if (fname == "until_interrupted") {
            if (match(TokenType::True)) decl.until_interrupted = true;
            else if (match(TokenType::False)) decl.until_interrupted = false;
            else { advance(); }
        }
        else if (fname == "target_metric") { if (!match(TokenType::Number)) error("Expected number"); decl.target_metric = std::stod(previous().lexeme); decl.target_metric_set = true; }
        else if (fname == "plugin_throttle") { if (!match(TokenType::String)) error("Expected string"); decl.plugin_throttle = previous().lexeme; }
        else { advance(); }
        match(TokenType::Comma);
    }
    if (!match(TokenType::RightBrace)) error("Expected '}'");
    auto stmt = std::make_unique<Statement>();
    stmt->span = {0, 0, span, 0};
    stmt->node = std::move(decl);
    return stmt;
}

// ═══════════════════════════════════════════════════════════════════════════
// v1.4: NeamWiki — parse_wiki_agent_decl (2-keyword: "wiki agent Name { ... }")
// ═══════════════════════════════════════════════════════════════════════════
StmtPtr Parser::parse_wiki_agent_decl(const Visibility& vis) {
    auto span = previous().line;
    if (!match(TokenType::Identifier)) error("Expected wiki agent name");
    WikiAgentDecl decl;
    decl.visibility = vis;
    decl.name = previous().lexeme;
    if (!match(TokenType::LeftBrace)) error("Expected '{'");

    auto consume_array = [&](const std::string& field_name) -> std::string {
        if (!match(TokenType::LeftBracket)) error("Expected '[' for " + field_name);
        std::string arr = "[";
        int depth = 1;
        bool first = true;
        while (depth > 0 && !is_at_end()) {
            if (check(TokenType::LeftBracket)) { depth++; arr += "["; advance(); first = true; continue; }
            if (check(TokenType::RightBracket)) { depth--; if (depth > 0) { arr += "]"; advance(); continue; } else break; }
            if (check(TokenType::Comma)) { arr += ","; advance(); first = true; continue; }
            if (check(TokenType::String)) { if (!first) {} arr += "\"" + peek().lexeme + "\""; advance(); first = false; continue; }
            if (check(TokenType::Identifier)) { arr += "\"" + peek().lexeme + "\""; advance(); first = false; continue; }
            if (check(TokenType::Number)) { arr += peek().lexeme; advance(); first = false; continue; }
            advance();
        }
        if (!match(TokenType::RightBracket)) error("Expected ']' for " + field_name);
        arr += "]";
        return arr;
    };

    while (!check(TokenType::RightBrace) && !is_at_end()) {
        if (!match(TokenType::Identifier) && !match_keyword_as_identifier()) error("Expected field name");
        std::string fname = previous().lexeme;
        if (!match(TokenType::Colon)) error("Expected ':'");

        if (fname == "provider") { if (!match(TokenType::String)) error("Expected string"); decl.provider = previous().lexeme; }
        else if (fname == "model") { if (!match(TokenType::String)) error("Expected string"); decl.model = previous().lexeme; }
        else if (fname == "temperature") { if (!match(TokenType::Number)) error("Expected number"); decl.temperature = std::stod(previous().lexeme); }
        else if (fname == "budget") { if (!match(TokenType::Identifier)) error("Expected ref"); decl.budget = previous().lexeme; }
        else if (fname == "wikis") { decl.wikis_json = consume_array("wikis"); }
        else if (fname == "operations") { decl.operations_json = consume_array("operations"); }
        else if (fname == "research_config") { decl.research_config_json = consume_json_block(); }
        else if (fname == "lint_policies") { decl.lint_policies_json = consume_json_block(); }
        else if (fname == "graph_config") { decl.graph_config_json = consume_json_block(); }
        else if (fname == "output_formats") { decl.output_formats_json = consume_array("output_formats"); }
        else if (fname == "governance") { decl.governance_json = consume_array("governance"); }
        else if (fname == "plugin_hooks") { decl.plugin_hooks_json = consume_array("plugin_hooks"); }
        else if (fname == "knowledge_cards") { decl.knowledge_cards_json = consume_array("knowledge_cards"); }
        else { advance(); }
        match(TokenType::Comma);
    }
    if (!match(TokenType::RightBrace)) error("Expected '}'");
    auto stmt = std::make_unique<Statement>();
    stmt->span = {0, 0, span, 0};
    stmt->node = std::move(decl);
    return stmt;
}

}  // namespace neamc
