//
// NeamC - Parser implementation (tokenized, visitor-friendly)
//

#include "neamc/parser.hpp"

#include <cctype>
#include <cstdlib>
#include <stdexcept>
#include <unordered_map>

namespace neamc
{
namespace
{
ExprPtr make_literal(vm::Value value)
{
  auto expr = std::make_unique<Expression>();
  expr->node = LiteralExpr{std::move(value)};
  return expr;
}

ExprPtr make_identifier(std::string name)
{
  auto expr = std::make_unique<Expression>();
  expr->node = IdentifierExpr{std::move(name)};
  return expr;
}

ExprPtr make_assignment(std::string name, ExprPtr value)
{
  auto expr = std::make_unique<Expression>();
  expr->node = AssignmentExpr{std::move(name), std::move(value)};
  return expr;
}

ExprPtr make_unary(UnaryOp op, ExprPtr operand)
{
  auto expr = std::make_unique<Expression>();
  expr->node = UnaryExpr{op, std::move(operand)};
  return expr;
}

ExprPtr make_binary(BinaryOp op, ExprPtr left, ExprPtr right)
{
  auto expr = std::make_unique<Expression>();
  expr->node = BinaryExpr{op, std::move(left), std::move(right)};
  return expr;
}

ExprPtr make_call(ExprPtr callee, std::vector<ExprPtr> arguments)
{
  auto expr = std::make_unique<Expression>();
  expr->node = CallExpr{std::move(callee), std::move(arguments)};
  return expr;
}

ExprPtr make_get(ExprPtr object, std::string name)
{
  auto expr = std::make_unique<Expression>();
  expr->node = GetExpr{std::move(object), std::move(name)};
  return expr;
}

ExprPtr make_list(std::vector<ExprPtr> elements)
{
  auto expr = std::make_unique<Expression>();
  expr->node = ListExpr{std::move(elements)};
  return expr;
}

ExprPtr make_map(std::vector<std::pair<std::string, ExprPtr>> entries)
{
  auto expr = std::make_unique<Expression>();
  expr->node = MapExpr{std::move(entries)};
  return expr;
}

StmtPtr make_expression_stmt(ExprPtr expression)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = ExpressionStmt{std::move(expression)};
  return stmt;
}

StmtPtr make_emit_stmt(ExprPtr value)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = EmitStmt{std::move(value)};
  return stmt;
}

StmtPtr make_block_stmt(std::vector<StmtPtr> statements)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = BlockStmt{std::move(statements)};
  return stmt;
}

StmtPtr make_let_stmt(std::string name, ExprPtr initializer)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = LetStmt{std::move(name), std::move(initializer)};
  return stmt;
}

StmtPtr make_if_stmt(ExprPtr condition, StmtPtr then_branch, StmtPtr else_branch)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = IfStmt{std::move(condition), std::move(then_branch), std::move(else_branch)};
  return stmt;
}

StmtPtr make_while_stmt(ExprPtr condition, StmtPtr body)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = WhileStmt{std::move(condition), std::move(body)};
  return stmt;
}

StmtPtr make_return_stmt(ExprPtr value)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = ReturnStmt{std::move(value)};
  return stmt;
}

StmtPtr make_function_decl(std::string name, std::vector<std::string> params, StmtPtr body)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = FunctionDecl{std::move(name), std::move(params), std::move(body)};
  return stmt;
}

StmtPtr make_skill_decl(std::string name, std::string description, std::vector<SkillParam> params,
                        FunctionDecl impl)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = SkillDecl{std::move(name), std::move(description), std::move(params), std::move(impl)};
  return stmt;
}

StmtPtr make_agent_decl(std::string name, std::string provider, std::string model,
                        std::optional<std::string> endpoint,
                        std::optional<std::string> api_key_env,
                        std::optional<double> temperature,
                        std::optional<std::string> system,
                        std::vector<std::string> skills)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = AgentDecl{std::move(name), std::move(provider), std::move(model), std::move(endpoint),
                         std::move(api_key_env), std::move(temperature), std::move(system),
                         std::move(skills)};
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
      {"let", TokenType::Let},     {"if", TokenType::If},         {"else", TokenType::Else},
      {"while", TokenType::While}, {"fun", TokenType::Fun},       {"return", TokenType::Return},
      {"emit", TokenType::Emit},   {"true", TokenType::True},     {"false", TokenType::False},
      {"nil", TokenType::Nil},     {"skill", TokenType::Skill},   {"agent", TokenType::Agent},
      {"description", TokenType::Description},
      {"params", TokenType::Params},
      {"impl", TokenType::Impl},
      {"provider", TokenType::Provider},
      {"model", TokenType::Model},
      {"endpoint", TokenType::Endpoint},
      {"api_key_env", TokenType::ApiKeyEnv},
      {"temperature", TokenType::Temperature},
      {"system", TokenType::System},
      {"skills", TokenType::Skills}};

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
      case '.':
        add(TokenType::Dot);
        continue;
      case '[':
        add(TokenType::LeftBracket);
        continue;
      case ']':
        add(TokenType::RightBracket);
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
}

StmtPtr Parser::parse_declaration()
{
  if (match(TokenType::Skill))
  {
    return parse_skill();
  }
  if (match(TokenType::Agent))
  {
    return parse_agent();
  }
  if (match(TokenType::Fun))
  {
    return parse_function();
  }
  if (match(TokenType::Let))
  {
    return parse_let();
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
  if (match(TokenType::LeftBrace))
  {
    return parse_block();
  }

  auto expr = parse_expression();
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after expression");
  }
  return make_expression_stmt(std::move(expr));
}

StmtPtr Parser::parse_return()
{
  ExprPtr value;
  if (!check(TokenType::Semicolon))
  {
    value = parse_expression();
  }
  else
  {
    value = make_literal(vm::Value::Nil());
  }
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after return value");
  }
  return make_return_stmt(std::move(value));
}

StmtPtr Parser::parse_emit()
{
  auto value = parse_expression();
  if (!match(TokenType::Semicolon))
  {
    error("Expected ';' after emit value");
  }
  return make_emit_stmt(std::move(value));
}

StmtPtr Parser::parse_function()
{
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
  return make_function_decl(name, std::move(params), std::move(body));
}

StmtPtr Parser::parse_skill()
{
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
      impl = FunctionDecl{name + ".impl", std::move(impl_params), std::move(body)};
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
  return make_skill_decl(name, *description, std::move(params), std::move(*impl));
}

StmtPtr Parser::parse_agent()
{
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
  std::vector<std::string> skills;

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
  return make_agent_decl(name, *provider, *model, std::move(endpoint), std::move(api_key_env),
                         std::move(temperature), std::move(system), std::move(skills));
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
  std::vector<std::string> enum_values;

  if (match(TokenType::LeftParen))
  {
    if (!match(TokenType::Identifier) || previous().lexeme != "enum")
    {
      error("Expected 'enum' in param type annotation");
    }
    if (!match(TokenType::Equal))
    {
      error("Expected '=' after enum");
    }
    if (!match(TokenType::LeftBracket))
    {
      error("Expected '[' after enum=");
    }
    if (!check(TokenType::RightBracket))
    {
      do
      {
        if (!match(TokenType::String))
        {
          error("Expected string literal in enum list");
        }
        enum_values.push_back(previous().lexeme);
      } while (match(TokenType::Comma));
    }
    if (!match(TokenType::RightBracket))
    {
      error("Expected ']' after enum list");
    }
    if (!match(TokenType::RightParen))
    {
      error("Expected ')' after enum spec");
    }
  }

  return SkillParam{name, type, std::move(enum_values)};
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

std::vector<std::string> Parser::parse_identifier_list()
{
  if (!match(TokenType::LeftBracket))
  {
    error("Expected '[' to start list");
  }
  std::vector<std::string> values;
  if (!check(TokenType::RightBracket))
  {
    do
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected identifier in list");
      }
      values.push_back(previous().lexeme);
    } while (match(TokenType::Comma));
  }
  if (!match(TokenType::RightBracket))
  {
    error("Expected ']' after list");
  }
  return values;
}

StmtPtr Parser::parse_let()
{
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
  return make_let_stmt(name, std::move(initializer));
}

StmtPtr Parser::parse_block()
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
  return make_block_stmt(std::move(statements));
}

StmtPtr Parser::parse_if()
{
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
  return make_if_stmt(std::move(condition), std::move(then_branch), std::move(else_branch));
}

StmtPtr Parser::parse_while()
{
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
  return make_while_stmt(std::move(condition), std::move(body));
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
      return make_assignment(ident->name, std::move(value));
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
    expr = make_binary(op_token == TokenType::EqualEqual ? BinaryOp::Equal : BinaryOp::NotEqual,
                       std::move(expr), std::move(right));
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
    expr = make_binary(op, std::move(expr), std::move(right));
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
    expr = make_binary(op_token == TokenType::Plus ? BinaryOp::Add : BinaryOp::Subtract, std::move(expr),
                       std::move(right));
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
    expr = make_binary(op_token == TokenType::Star ? BinaryOp::Multiply : BinaryOp::Divide, std::move(expr),
                       std::move(right));
  }
  return expr;
}

ExprPtr Parser::parse_unary()
{
  if (match(TokenType::Bang))
  {
    return make_unary(UnaryOp::Not, parse_unary());
  }
  if (match(TokenType::Minus))
  {
    return make_unary(UnaryOp::Negate, parse_unary());
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
      expr = make_call(std::move(expr), std::move(args));
      continue;
    }
    if (match(TokenType::Dot))
    {
      if (!match(TokenType::Identifier))
      {
        error("Expected property name after '.'");
      }
      expr = make_get(std::move(expr), previous().lexeme);
      continue;
    }
    break;
  }
  return expr;
}

ExprPtr Parser::parse_primary()
{
  if (match(TokenType::Number))
  {
    const double value = std::strtod(previous().lexeme.c_str(), nullptr);
    return make_literal(vm::Value::Number(value));
  }
  if (match(TokenType::String))
  {
    return make_literal(
        vm::Value::String(previous().lexeme.c_str(), previous().lexeme.size()));
  }
  if (match(TokenType::True))
  {
    return make_literal(vm::Value::Bool(true));
  }
  if (match(TokenType::False))
  {
    return make_literal(vm::Value::Bool(false));
  }
  if (match(TokenType::Nil))
  {
    return make_literal(vm::Value::Nil());
  }
  if (match(TokenType::Identifier))
  {
    return make_identifier(previous().lexeme);
  }
  if (match(TokenType::LeftBracket))
  {
    std::vector<ExprPtr> elements;
    if (!check(TokenType::RightBracket))
    {
      do
      {
        elements.push_back(parse_expression());
      } while (match(TokenType::Comma));
    }
    if (!match(TokenType::RightBracket))
    {
      error("Expected ']' after list literal");
    }
    return make_list(std::move(elements));
  }
  if (match(TokenType::LeftBrace))
  {
    std::vector<std::pair<std::string, ExprPtr>> entries;
    if (!check(TokenType::RightBrace))
    {
      do
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
          error("Expected identifier or string key in map literal");
        }
        if (!match(TokenType::Colon))
        {
          error("Expected ':' after map key");
        }
        auto value = parse_expression();
        entries.emplace_back(std::move(key), std::move(value));
      } while (match(TokenType::Comma));
    }
    if (!match(TokenType::RightBrace))
    {
      error("Expected '}' after map literal");
    }
    return make_map(std::move(entries));
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
