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
  Minus,
  Plus,
  Slash,
  Star,
  Dot,
  LeftBracket,
  RightBracket,

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

  // Keywords.
  Let,
  If,
  Else,
  While,
  Fun,
  Return,
  Emit,
  Skill,
  Agent,
  Description,
  Params,
  Impl,
  Provider,
  Model,
  Endpoint,
  ApiKeyEnv,
  Temperature,
  System,
  Skills,
  True,
  False,
  Nil,

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
  StmtPtr parse_function();
  StmtPtr parse_skill();
  StmtPtr parse_agent();
  StmtPtr parse_let();
  StmtPtr parse_block();
  StmtPtr parse_if();
  StmtPtr parse_while();
  SkillParam parse_skill_param();
  std::vector<SkillParam> parse_skill_params();
  std::vector<IdentifierRef> parse_identifier_list();
  ExprPtr parse_expression();
  ExprPtr parse_assignment();
  ExprPtr parse_equality();
  ExprPtr parse_comparison();
  ExprPtr parse_term();
  ExprPtr parse_factor();
  ExprPtr parse_unary();
  ExprPtr parse_call();
  ExprPtr parse_primary();

  std::string source_;
  std::vector<Token> tokens_{};
  std::size_t current_ = 0;
};
}  // namespace neamc
