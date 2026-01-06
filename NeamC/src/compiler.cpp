//
// NeamC - Compiler backend implementation
//

#include "neamc/compiler.hpp"

#include <stdexcept>

namespace neamc
{
using vm::OpCode;

vm::Chunk Compiler::compile(const Program& program)
{
  chunk_ = vm::Chunk{};
  chunk_.set_manifest(program.manifest);

  for (const auto& stmt : program.statements)
  {
    emit_statement(*stmt);
  }

  return chunk_;
}

void Compiler::emit_statement(const Statement& stmt)
{
  std::visit(
      [this](auto&& node)
      {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, ExpressionStmt>)
        {
          emit_expression(*node.expression);
          chunk_.write_op(OpCode::OP_POP);
        }
        else if constexpr (std::is_same_v<T, BlockStmt>)
        {
          for (const auto& inner : node.statements)
          {
            emit_statement(*inner);
          }
        }
        else if constexpr (std::is_same_v<T, AgentDecl>)
        {
          (void)node;
          // Future: create function object and emit agent declarations
        }
      },
      stmt.node);
}

void Compiler::emit_expression(const Expression& expr)
{
  std::visit(
      [this](auto&& node)
      {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, LiteralExpr>)
        {
          chunk_.emit_constant(vm::Value::Number(node.value));
        }
        else if constexpr (std::is_same_v<T, UnaryExpr>)
        {
          emit_expression(*node.operand);
          if (node.op == UnaryOp::Negate)
          {
            chunk_.write_op(OpCode::OP_NEGATE);
          }
          else
          {
            throw std::runtime_error("Unsupported unary operator");
          }
        }
        else if constexpr (std::is_same_v<T, BinaryExpr>)
        {
          emit_expression(*node.left);
          emit_expression(*node.right);
          switch (node.op)
          {
            case BinaryOp::Add:
              chunk_.write_op(OpCode::OP_ADD);
              break;
            case BinaryOp::Subtract:
              chunk_.write_op(OpCode::OP_SUB);
              break;
            case BinaryOp::Multiply:
              chunk_.write_op(OpCode::OP_MUL);
              break;
            case BinaryOp::Divide:
              chunk_.write_op(OpCode::OP_DIV);
              break;
            default:
              throw std::runtime_error("Unsupported binary operator");
          }
        }
      },
      expr.node);
}
}  // namespace neamc
