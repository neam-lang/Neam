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
                        SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = AgentDecl{std::move(visibility), std::move(name), std::move(provider),
                         std::move(model), std::move(endpoint), std::move(api_key_env),
                         std::move(temperature), std::move(system), std::move(skills),
                         std::move(connected_knowledge)};
  return stmt;
}

StmtPtr make_knowledge_decl(Visibility visibility, std::string name, std::string vector_store,
                            std::string embedding_model,
                            std::size_t chunk_size, std::size_t chunk_overlap,
                            std::vector<KnowledgeSource> sources, SourceSpan span)
{
  auto stmt = std::make_unique<Statement>();
  stmt->span = span;
  stmt->node = KnowledgeDecl{std::move(visibility), std::move(name), std::move(vector_store),
                             std::move(embedding_model), chunk_size, chunk_overlap,
                             std::move(sources)};
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
      {"provider", TokenType::Provider},
      {"model", TokenType::Model},
      {"endpoint", TokenType::Endpoint},
      {"api_key_env", TokenType::ApiKeyEnv},
      {"temperature", TokenType::Temperature},
      {"system", TokenType::System},
      {"skills", TokenType::Skills},
      {"connected_knowledge", TokenType::ConnectedKnowledge}};

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
      case '+':
        add(TokenType::Plus);
        continue;
      case '-':
        add(TokenType::Minus);
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
  if (match(TokenType::Knowledge))
  {
    return parse_knowledge(has_visibility ? visibility : Visibility{});
  }
  if (match(TokenType::Agent))
  {
    return parse_agent(has_visibility ? visibility : Visibility{});
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
  return make_expression_stmt(std::move(expr), expr->span);
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
                         std::move(skills), std::move(connected_knowledge), span);
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
                             *chunk_overlap, std::move(sources), span);
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
}  // namespace neamc
