//
// NeamC - Minimal parser for arithmetic expressions and blocks
//

#pragma once

#include <string>

#include "neamc/ast.hpp"

namespace neamc
{
class Parser
{
public:
  explicit Parser(std::string source);

  Program parse();

private:
  bool is_at_end() const;
  char peek() const;
  char advance();
  bool match(char expected);
  void skip_whitespace();
  [[noreturn]] void error(const std::string& message) const;

  StmtPtr parse_statement();
  StmtPtr parse_block();
  ExprPtr parse_expression();
  ExprPtr parse_term();
  ExprPtr parse_factor();
  ExprPtr parse_unary();
  ExprPtr parse_primary();

  std::string source_;
  std::size_t current_ = 0;
};
}  // namespace neamc
