//
// NeamC - Minimal parser implementation
//

#include "neamc/parser.hpp"

#include <cctype>
#include <cstdlib>
#include <stdexcept>

namespace neamc
{
namespace
{
ExprPtr make_literal(double value)
{
  auto expr = std::make_unique<Expression>();
  expr->node = LiteralExpr{value};
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

StmtPtr make_expression_stmt(ExprPtr expression)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = ExpressionStmt{std::move(expression)};
  return stmt;
}

StmtPtr make_block_stmt(std::vector<StmtPtr> statements)
{
  auto stmt = std::make_unique<Statement>();
  stmt->node = BlockStmt{std::move(statements)};
  return stmt;
}
}  // namespace

Parser::Parser(std::string source) : source_(std::move(source)) {}

bool Parser::is_at_end() const
{
  return current_ >= source_.size();
}

char Parser::peek() const
{
  if (is_at_end())
  {
    return '\0';
  }
  return source_[current_];
}

char Parser::advance()
{
  if (is_at_end())
  {
    return '\0';
  }
  return source_[current_++];
}

bool Parser::match(char expected)
{
  if (peek() != expected)
  {
    return false;
  }
  ++current_;
  return true;
}

void Parser::skip_whitespace()
{
  while (!is_at_end() && std::isspace(static_cast<unsigned char>(peek())))
  {
    ++current_;
  }
}

[[noreturn]] void Parser::error(const std::string& message) const
{
  throw std::runtime_error("Parse error: " + message);
}

StmtPtr Parser::parse_statement()
{
  skip_whitespace();
  if (match('{'))
  {
    return parse_block();
  }

  auto expr = parse_expression();
  skip_whitespace();
  if (!match(';'))
  {
    error("Expected ';' after expression");
  }
  return make_expression_stmt(std::move(expr));
}

StmtPtr Parser::parse_block()
{
  std::vector<StmtPtr> statements;
  while (true)
  {
    skip_whitespace();
    if (is_at_end())
    {
      error("Unterminated block");
    }
    if (match('}'))
    {
      break;
    }
    statements.push_back(parse_statement());
  }
  return make_block_stmt(std::move(statements));
}

ExprPtr Parser::parse_expression()
{
  return parse_term();
}

ExprPtr Parser::parse_term()
{
  auto expr = parse_factor();
  while (true)
  {
    skip_whitespace();
    if (match('+'))
    {
      auto right = parse_factor();
      expr = make_binary(BinaryOp::Add, std::move(expr), std::move(right));
      continue;
    }
    if (match('-'))
    {
      auto right = parse_factor();
      expr = make_binary(BinaryOp::Subtract, std::move(expr), std::move(right));
      continue;
    }
    break;
  }
  return expr;
}

ExprPtr Parser::parse_factor()
{
  auto expr = parse_unary();
  while (true)
  {
    skip_whitespace();
    if (match('*'))
    {
      auto right = parse_unary();
      expr = make_binary(BinaryOp::Multiply, std::move(expr), std::move(right));
      continue;
    }
    if (match('/'))
    {
      auto right = parse_unary();
      expr = make_binary(BinaryOp::Divide, std::move(expr), std::move(right));
      continue;
    }
    break;
  }
  return expr;
}

ExprPtr Parser::parse_unary()
{
  skip_whitespace();
  if (match('-'))
  {
    auto operand = parse_unary();
    return make_unary(UnaryOp::Negate, std::move(operand));
  }
  return parse_primary();
}

ExprPtr Parser::parse_primary()
{
  skip_whitespace();
  if (std::isdigit(static_cast<unsigned char>(peek())))
  {
    const char* start = source_.data() + current_;
    char* end = nullptr;
    const double value = std::strtod(start, &end);
    if (start == end)
    {
      error("Invalid number literal");
    }
    current_ += static_cast<std::size_t>(end - start);
    return make_literal(value);
  }

  if (match('('))
  {
    auto expr = parse_expression();
    skip_whitespace();
    if (!match(')'))
    {
      error("Expected ')'");
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
    skip_whitespace();
    if (is_at_end())
    {
      break;
    }
    program.statements.push_back(parse_statement());
  }
  return program;
}
}  // namespace neamc
