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
  Provider,
  Model,
  Endpoint,
  ApiKeyEnv,
  Temperature,
  System,
  Skills,
  ConnectedKnowledge,
  True,
  False,
  Nil,
  DocComment,

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
  StmtPtr parse_knowledge(const Visibility& visibility);
  StmtPtr parse_agent(const Visibility& visibility);
  StmtPtr parse_module_decl();
  StmtPtr parse_import_decl(const Visibility& visibility, bool is_reexport);
  StmtPtr parse_const_decl(const Visibility& visibility);
  StmtPtr parse_type_alias(const Visibility& visibility);
  StmtPtr parse_doc_comment(Token first_token);
  StmtPtr parse_test_decl_statement();
  StmtPtr parse_test_suite_statement();
  StmtPtr parse_with_statement();
  StmtPtr parse_assert_statement(TokenType type);
  StmtPtr parse_let();
  StmtPtr parse_block();
  BlockStmt parse_block_node();
  StmtPtr parse_if();
  StmtPtr parse_while();
  TestAttribute parse_test_attribute();
  std::vector<TestAttribute> parse_test_attributes();
  std::unique_ptr<TestDecl> parse_test_decl_node(std::vector<TestAttribute> attributes);
  std::unique_ptr<TestSuiteDecl> parse_test_suite_node();
  SkillParam parse_skill_param();
  std::vector<SkillParam> parse_skill_params();
  std::vector<IdentifierRef> parse_identifier_list();
  std::vector<KnowledgeSource> parse_knowledge_sources();
  Visibility parse_visibility();
  std::vector<std::string> parse_module_path();
  std::vector<std::string> parse_import_items();
  ExprPtr parse_expression();
  ExprPtr parse_assignment();
  ExprPtr parse_equality();
  ExprPtr parse_comparison();
  ExprPtr parse_term();
  ExprPtr parse_factor();
  ExprPtr parse_unary();
  ExprPtr parse_call();
  ExprPtr parse_primary();
  std::unique_ptr<TypeExpression> parse_type_expression();

  std::string source_;
  std::vector<Token> tokens_{};
  std::size_t current_ = 0;
};
}  // namespace neamc
