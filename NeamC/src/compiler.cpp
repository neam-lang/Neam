//
// NeamC - Compiler backend implementation
//

#include "neamc/compiler.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

#include "neamc/vm/object.hpp"

namespace neamc
{
using vm::OpCode;

namespace
{
std::size_t emit_jump(vm::Chunk& chunk, OpCode jump_op)
{
  chunk.write_op(jump_op);
  const auto offset_index = chunk.code().size();
  chunk.write_short(0);
  return offset_index;
}

void patch_jump(vm::Chunk& chunk, std::size_t offset_index)
{
  const auto target = chunk.code().size();
  const auto offset = target - offset_index - 2;
  if (offset > std::numeric_limits<uint16_t>::max())
  {
    throw std::runtime_error("Jump offset too large");
  }
  chunk.patch_short(offset_index, static_cast<uint16_t>(offset));
}

std::size_t emit_string_constant(vm::Chunk& chunk, const std::string& value)
{
  return chunk.add_constant(vm::Value::String(value.c_str(), value.size()));
}

void emit_build_list(vm::Chunk& chunk, std::size_t count)
{
  if (count > std::numeric_limits<uint8_t>::max())
  {
    throw std::runtime_error("List literal too large");
  }
  chunk.write_op(OpCode::OP_BUILD_LIST);
  chunk.write_byte(static_cast<uint8_t>(count));
}

void emit_build_map(vm::Chunk& chunk, std::size_t count)
{
  if (count > std::numeric_limits<uint8_t>::max())
  {
    throw std::runtime_error("Map literal too large");
  }
  chunk.write_op(OpCode::OP_BUILD_MAP);
  chunk.write_byte(static_cast<uint8_t>(count));
}
}  // namespace

vm::Chunk Compiler::compile(const Program& program)
{
  chunk_ = vm::Chunk{};
  chunk_.set_manifest(program.manifest);
  locals_.clear();
  scope_depth_ = 0;

  for (const auto& stmt : program.statements)
  {
    emit_statement(*stmt);
  }

  chunk_.write_op(OpCode::OP_NIL);
  chunk_.write_op(OpCode::OP_RETURN);

  return chunk_;
}

vm::Value Compiler::compile_function(const FunctionDecl& decl)
{
  Compiler fn_compiler;
  fn_compiler.chunk_.set_manifest("fn:" + decl.name);
  fn_compiler.scope_depth_ = 0;
  fn_compiler.locals_.clear();

  fn_compiler.begin_scope();
  for (const auto& param : decl.parameters)
  {
    fn_compiler.locals_.push_back({param, fn_compiler.scope_depth_});
  }

  const auto* block_ptr = std::get_if<BlockStmt>(&decl.body->node);
  if (!block_ptr)
  {
    throw std::runtime_error("Function body must be a block");
  }
  fn_compiler.emit_block(*block_ptr);

  fn_compiler.chunk_.write_op(OpCode::OP_NIL);
  fn_compiler.chunk_.write_op(OpCode::OP_RETURN);

  auto* fn_obj = vm::new_function();
  fn_obj->arity = static_cast<int>(decl.parameters.size());
  fn_obj->name = vm::copy_string(decl.name.c_str(), decl.name.size());
  fn_obj->chunk = fn_compiler.chunk_;
  return vm::Value::FunctionValue(fn_obj);
}

void Compiler::begin_scope()
{
  ++scope_depth_;
}

void Compiler::end_scope()
{
  while (!locals_.empty() && locals_.back().depth >= scope_depth_)
  {
    chunk_.write_op(OpCode::OP_POP);
    locals_.pop_back();
  }
  --scope_depth_;
}

int Compiler::resolve_local(const std::string& name) const
{
  for (int i = static_cast<int>(locals_.size()) - 1; i >= 0; --i)
  {
    if (locals_[static_cast<std::size_t>(i)].name == name)
    {
      return i;
    }
  }
  return -1;
}

void Compiler::emit_block(const BlockStmt& block)
{
  begin_scope();
  for (const auto& stmt : block.statements)
  {
    emit_statement(*stmt);
  }
  end_scope();
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
        else if constexpr (std::is_same_v<T, EmitStmt>)
        {
          emit_expression(*node.value);
          chunk_.write_op(OpCode::OP_EMIT);
        }
        else if constexpr (std::is_same_v<T, BlockStmt>)
        {
          emit_block(node);
        }
        else if constexpr (std::is_same_v<T, LetStmt>)
        {
          emit_expression(*node.initializer);
          if (scope_depth_ == 0)
          {
            const auto name_constant =
                chunk_.add_constant(vm::Value::String(node.name.c_str(), node.name.size()));
            chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
            chunk_.write_short(static_cast<uint16_t>(name_constant));
          }
          else
          {
            locals_.push_back(Local{node.name, scope_depth_});
          }
        }
        else if constexpr (std::is_same_v<T, IfStmt>)
        {
          emit_expression(*node.condition);
          const auto then_jump = emit_jump(chunk_, OpCode::OP_JUMP_IF_FALSE);
          chunk_.write_op(OpCode::OP_POP);  // pop condition
          emit_statement(*node.then_branch);
          const auto else_jump = emit_jump(chunk_, OpCode::OP_JUMP);
          patch_jump(chunk_, then_jump);
          chunk_.write_op(OpCode::OP_POP);
          if (node.else_branch)
          {
            emit_statement(*node.else_branch);
          }
          patch_jump(chunk_, else_jump);
        }
        else if constexpr (std::is_same_v<T, WhileStmt>)
        {
          const auto loop_start = chunk_.code().size();
          emit_expression(*node.condition);
          const auto exit_jump = emit_jump(chunk_, OpCode::OP_JUMP_IF_FALSE);
          chunk_.write_op(OpCode::OP_POP);
          emit_statement(*node.body);
          chunk_.write_op(OpCode::OP_LOOP);
          const auto loop_offset_index = chunk_.code().size();
          chunk_.write_short(0);
          const auto offset = chunk_.code().size() - loop_start;
          if (offset > std::numeric_limits<uint16_t>::max())
          {
            throw std::runtime_error("Loop body too large");
          }
          chunk_.patch_short(loop_offset_index, static_cast<uint16_t>(offset));
          patch_jump(chunk_, exit_jump);
          chunk_.write_op(OpCode::OP_POP);
        }
        else if constexpr (std::is_same_v<T, ReturnStmt>)
        {
          emit_expression(*node.value);
          chunk_.write_op(OpCode::OP_RETURN);
        }
        else if constexpr (std::is_same_v<T, FunctionDecl>)
        {
          auto fn_value = compile_function(node);
          const auto constant_index = chunk_.add_constant(std::move(fn_value));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(constant_index));
          if (scope_depth_ == 0)
          {
            chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
            chunk_.write_short(static_cast<uint16_t>(
                chunk_.add_constant(vm::Value::String(node.name.c_str(), node.name.size()))));
          }
          else
          {
            locals_.push_back(Local{node.name, scope_depth_});
          }
        }
        else if constexpr (std::is_same_v<T, SkillDecl>)
        {
          auto fn_value = compile_function(node.impl);
          const auto fn_constant = chunk_.add_constant(std::move(fn_value));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.description)));

          for (const auto& param : node.params)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, param.name)));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "type")));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, param.type)));

            std::size_t param_field_count = 1;
            if (!param.enum_values.empty())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "enum")));
              for (const auto& enum_value : param.enum_values)
              {
                chunk_.write_op(OpCode::OP_CONST);
                chunk_.write_short(
                    static_cast<uint16_t>(emit_string_constant(chunk_, enum_value)));
              }
              emit_build_list(chunk_, param.enum_values.size());
              ++param_field_count;
            }
            emit_build_map(chunk_, param_field_count);
          }

          emit_build_map(chunk_, node.params.size());
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(fn_constant));
          chunk_.write_op(OpCode::OP_DEFINE_SKILL);
        }
        else if constexpr (std::is_same_v<T, AgentDecl>)
        {
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.provider)));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.model)));

          if (node.endpoint.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.endpoint.value())));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.api_key_env.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.api_key_env.value())));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.temperature.has_value())
          {
            chunk_.emit_constant(vm::Value::Number(node.temperature.value()));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.system.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.system.value())));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          for (const auto& skill_name : node.skills)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, skill_name)));
          }
          emit_build_list(chunk_, node.skills.size());
          chunk_.write_op(OpCode::OP_DEFINE_AGENT);
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
          chunk_.emit_constant(node.value);
        }
        else if constexpr (std::is_same_v<T, IdentifierExpr>)
        {
          const auto slot = resolve_local(node.name);
          if (slot < 0)
          {
            const auto name_constant =
                chunk_.add_constant(vm::Value::String(node.name.c_str(), node.name.size()));
            chunk_.write_op(OpCode::OP_GET_GLOBAL);
            chunk_.write_short(static_cast<uint16_t>(name_constant));
          }
          else
          {
            chunk_.write_op(OpCode::OP_GET_LOCAL);
            chunk_.write_short(static_cast<uint16_t>(slot));
          }
        }
        else if constexpr (std::is_same_v<T, AssignmentExpr>)
        {
          emit_expression(*node.value);
          const auto slot = resolve_local(node.name);
          if (slot < 0)
          {
            const auto name_constant =
                chunk_.add_constant(vm::Value::String(node.name.c_str(), node.name.size()));
            chunk_.write_op(OpCode::OP_SET_GLOBAL);
            chunk_.write_short(static_cast<uint16_t>(name_constant));
          }
          else
          {
            chunk_.write_op(OpCode::OP_SET_LOCAL);
            chunk_.write_short(static_cast<uint16_t>(slot));
          }
        }
        else if constexpr (std::is_same_v<T, UnaryExpr>)
        {
          emit_expression(*node.operand);
          if (node.op == UnaryOp::Negate)
          {
            chunk_.write_op(OpCode::OP_NEGATE);
          }
          else if (node.op == UnaryOp::Not)
          {
            chunk_.write_op(OpCode::OP_NOT);
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
            case BinaryOp::Greater:
              chunk_.write_op(OpCode::OP_GREATER);
              break;
            case BinaryOp::Less:
              chunk_.write_op(OpCode::OP_LESS);
              break;
            case BinaryOp::GreaterEqual:
              chunk_.write_op(OpCode::OP_LESS);
              chunk_.write_op(OpCode::OP_NOT);
              break;
            case BinaryOp::LessEqual:
              chunk_.write_op(OpCode::OP_GREATER);
              chunk_.write_op(OpCode::OP_NOT);
              break;
            case BinaryOp::Equal:
              chunk_.write_op(OpCode::OP_EQUAL);
              break;
            case BinaryOp::NotEqual:
              chunk_.write_op(OpCode::OP_EQUAL);
              chunk_.write_op(OpCode::OP_NOT);
              break;
          }
        }
        else if constexpr (std::is_same_v<T, CallExpr>)
        {
          if (const auto* get_expr = std::get_if<GetExpr>(&node.callee->node))
          {
            emit_expression(*get_expr->object);
            for (const auto& arg : node.arguments)
            {
              emit_expression(*arg);
            }
            const auto name_constant =
                chunk_.add_constant(vm::Value::String(get_expr->name.c_str(), get_expr->name.size()));
            chunk_.write_op(OpCode::OP_INVOKE);
            chunk_.write_short(static_cast<uint16_t>(name_constant));
            chunk_.write_byte(static_cast<uint8_t>(node.arguments.size()));
          }
          else
          {
            emit_expression(*node.callee);
            for (const auto& arg : node.arguments)
            {
              emit_expression(*arg);
            }
            const auto arg_count = static_cast<uint8_t>(node.arguments.size());
            bool native_call = false;
            if (const auto* ident = std::get_if<IdentifierExpr>(&node.callee->node))
            {
              native_call = ident->name == "print";
            }
            chunk_.write_op(native_call ? OpCode::OP_CALL_NATIVE : OpCode::OP_CALL);
            chunk_.write_byte(arg_count);
          }
        }
        else if constexpr (std::is_same_v<T, GetExpr>)
        {
          emit_expression(*node.object);
          const auto name_constant =
              chunk_.add_constant(vm::Value::String(node.name.c_str(), node.name.size()));
          chunk_.write_op(OpCode::OP_GET_PROPERTY);
          chunk_.write_short(static_cast<uint16_t>(name_constant));
        }
        else if constexpr (std::is_same_v<T, ListExpr>)
        {
          for (const auto& element : node.elements)
          {
            emit_expression(*element);
          }
          emit_build_list(chunk_, node.elements.size());
        }
        else if constexpr (std::is_same_v<T, MapExpr>)
        {
          for (const auto& entry : node.entries)
          {
            const auto key_constant =
                chunk_.add_constant(vm::Value::String(entry.first.c_str(), entry.first.size()));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(key_constant));
            emit_expression(*entry.second);
          }
          emit_build_map(chunk_, node.entries.size());
        }
      },
      expr.node);
}
}  // namespace neamc
