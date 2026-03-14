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
      {"warehouse", TokenType::Warehouse}};

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
      current.type == TokenType::Endpoint || current.type == TokenType::Temperature)
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
      std::move(loop), std::move(verify), std::move(checkpoint)};
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

}  // namespace neamc
