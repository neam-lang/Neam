// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Compiler backend implementation
//

#include "neamc/compiler.hpp"

#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>

#include "neamc/parser.hpp"
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

void emit_identifier_list(vm::Chunk& chunk, const std::vector<IdentifierRef>& values)
{
  for (const auto& value : values)
  {
    chunk.write_op(OpCode::OP_CONST);
    chunk.write_short(static_cast<uint16_t>(emit_string_constant(chunk, value.name)));
  }
  emit_build_list(chunk, values.size());
}

void emit_string_list(vm::Chunk& chunk, const std::vector<std::string>& values)
{
  for (const auto& value : values)
  {
    chunk.write_op(OpCode::OP_CONST);
    chunk.write_short(static_cast<uint16_t>(emit_string_constant(chunk, value)));
  }
  emit_build_list(chunk, values.size());
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

void emit_json_value(vm::Chunk& chunk, const nlohmann::json& value)
{
  if (value.is_object())
  {
    for (const auto& entry : value.items())
    {
      chunk.write_op(OpCode::OP_CONST);
      chunk.write_short(static_cast<uint16_t>(emit_string_constant(chunk, entry.key())));
      emit_json_value(chunk, entry.value());
    }
    emit_build_map(chunk, value.size());
    return;
  }
  if (value.is_array())
  {
    for (const auto& item : value)
    {
      emit_json_value(chunk, item);
    }
    emit_build_list(chunk, value.size());
    return;
  }
  if (value.is_string())
  {
    chunk.write_op(OpCode::OP_CONST);
    chunk.write_short(static_cast<uint16_t>(emit_string_constant(chunk, value.get<std::string>())));
    return;
  }
  if (value.is_boolean())
  {
    chunk.write_op(value.get<bool>() ? OpCode::OP_TRUE : OpCode::OP_FALSE);
    return;
  }
  if (value.is_number())
  {
    chunk.emit_constant(vm::Value::Number(value.get<double>()));
    return;
  }
  if (value.is_null())
  {
    chunk.write_op(OpCode::OP_NIL);
    return;
  }
  throw std::runtime_error("Unsupported JSON schema value");
}

double normalize_budget_value(const BudgetDimension& dimension)
{
  if (dimension.unit == "ms" || dimension.unit.empty() || dimension.unit == "$")
  {
    return dimension.value;
  }
  if (dimension.unit == "s")
  {
    return dimension.value * 1000.0;
  }
  if (dimension.unit == "min")
  {
    return dimension.value * 60.0 * 1000.0;
  }
  if (dimension.unit == "h")
  {
    return dimension.value * 60.0 * 60.0 * 1000.0;
  }
  return dimension.value;
}
}  // namespace

vm::Chunk Compiler::compile(const Program& program)
{
  chunk_ = vm::Chunk{};
  chunk_.set_manifest(program.manifest);
  chunk_.clear_source_map();
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
  fn_compiler.chunk_.clear_source_map();
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

vm::Value Compiler::compile_block_function(const std::string& name,
                                           const std::vector<std::string>& parameters,
                                           const BlockStmt& body)
{
  Compiler fn_compiler;
  fn_compiler.chunk_.set_manifest("fn:" + name);
  fn_compiler.chunk_.clear_source_map();
  fn_compiler.scope_depth_ = 0;
  fn_compiler.locals_.clear();

  fn_compiler.begin_scope();
  for (const auto& param : parameters)
  {
    fn_compiler.locals_.push_back({param, fn_compiler.scope_depth_});
  }

  fn_compiler.emit_block(body);

  fn_compiler.chunk_.write_op(OpCode::OP_NIL);
  fn_compiler.chunk_.write_op(OpCode::OP_RETURN);

  auto* fn_obj = vm::new_function();
  fn_obj->arity = static_cast<int>(parameters.size());
  fn_obj->name = vm::copy_string(name.c_str(), name.size());
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
  chunk_.set_current_line(stmt.span.line);
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
        else if constexpr (std::is_same_v<T, ConstDecl>)
        {
          emit_expression(*node.value);
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
        else if constexpr (std::is_same_v<T, AssertStmt>)
        {
          std::string name;
          switch (node.kind)
          {
            case AssertStmt::Kind::kEq:
              name = "assert_eq";
              break;
            case AssertStmt::Kind::kNe:
              name = "assert_ne";
              break;
            case AssertStmt::Kind::kTrue:
              name = "assert_true";
              break;
            case AssertStmt::Kind::kFalse:
              name = "assert_false";
              break;
            case AssertStmt::Kind::kSome:
              name = "assert_some";
              break;
            case AssertStmt::Kind::kNone:
              name = "assert_none";
              break;
            case AssertStmt::Kind::kOk:
              name = "assert_ok";
              break;
            case AssertStmt::Kind::kErr:
              name = "assert_err";
              break;
            case AssertStmt::Kind::kThrows:
              name = "assert_throws";
              break;
          }
          chunk_.write_op(OpCode::OP_GET_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(chunk_.add_constant(vm::Value::String(name.c_str(), name.size()))));
          emit_expression(*node.left);
          uint8_t arg_count = 1;
          if (node.right)
          {
            emit_expression(*node.right);
            arg_count++;
          }
          if (node.exception_type)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                chunk_.add_constant(vm::Value::String(node.exception_type->name.c_str(),
                                                     node.exception_type->name.size()))));
            arg_count++;
          }
          chunk_.write_op(OpCode::OP_CALL_NATIVE);
          chunk_.write_byte(arg_count);
          chunk_.write_op(OpCode::OP_POP);
        }
        else if constexpr (std::is_same_v<T, WithStmt>)
        {
          begin_scope();
          emit_expression(*node.resource);
          locals_.push_back(Local{node.binding_name, scope_depth_});
          for (const auto& stmt : node.body->statements)
          {
            emit_statement(*stmt);
          }
          const auto slot = resolve_local(node.binding_name);
          if (slot >= 0)
          {
            chunk_.write_op(OpCode::OP_GET_LOCAL);
            chunk_.write_short(static_cast<uint16_t>(slot));
            chunk_.write_op(OpCode::OP_INVOKE);
            chunk_.write_short(static_cast<uint16_t>(
                chunk_.add_constant(vm::Value::String("close", 5))));
            chunk_.write_byte(0);
            chunk_.write_op(OpCode::OP_POP);
          }
          end_scope();
        }
        else if constexpr (std::is_same_v<T, TestDecl>)
        {
          (void)node;
        }
        else if constexpr (std::is_same_v<T, TestSuiteDecl>)
        {
          (void)node;
        }
        else if constexpr (std::is_same_v<T, ModuleDecl>)
        {
          // Set current module from path segments
          if (!node.path.empty())
          {
            std::string mod_name;
            for (std::size_t i = 0; i < node.path.size(); ++i)
            {
              if (i > 0) mod_name += ".";
              mod_name += node.path[i];
            }
            current_module_ = mod_name;
          }
        }
        else if constexpr (std::is_same_v<T, ImportDecl>)
        {
          compile_import(node);
        }
        else if constexpr (std::is_same_v<T, TypeAlias>)
        {
          (void)node;
        }
        else if constexpr (std::is_same_v<T, DocComment>)
        {
          (void)node;
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
          }
          emit_build_list(chunk_, node.params.size());

          for (const auto& param : node.params)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, param.name)));
            emit_json_value(chunk_, param.schema);
          }

          emit_build_map(chunk_, node.params.size());
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(fn_constant));
          chunk_.write_op(OpCode::OP_DEFINE_SKILL);
        }
        else if constexpr (std::is_same_v<T, KnowledgeDecl>)
        {
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.vector_store)));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.embedding_model)));
          chunk_.emit_constant(vm::Value::Number(static_cast<double>(node.chunk_size)));
          chunk_.emit_constant(vm::Value::Number(static_cast<double>(node.chunk_overlap)));

          for (const auto& source : node.sources)
          {
            std::size_t map_entries = 2;
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "type")));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, source.type)));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "path")));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, source.path)));
            if (!source.content.empty())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "content")));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, source.content)));
              map_entries = 3;
            }
            emit_build_map(chunk_, map_entries);
          }
          emit_build_list(chunk_, node.sources.size());

          // Emit retrieval strategy
          chunk_.emit_constant(vm::Value::Number(static_cast<double>(static_cast<int>(node.retrieval_strategy))));

          // Emit strategy options as a map
          std::size_t option_count = 0;
          // top_k
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "top_k")));
          chunk_.emit_constant(vm::Value::Number(static_cast<double>(node.strategy_options.top_k)));
          option_count++;
          // relevance_threshold
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "relevance_threshold")));
          chunk_.emit_constant(vm::Value::Number(node.strategy_options.relevance_threshold));
          option_count++;
          // mmr_lambda
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "mmr_lambda")));
          chunk_.emit_constant(vm::Value::Number(node.strategy_options.mmr_lambda));
          option_count++;
          // num_hypothetical
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "num_hypothetical")));
          chunk_.emit_constant(vm::Value::Number(static_cast<double>(node.strategy_options.num_hypothetical)));
          option_count++;
          // enable_relevance_check
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "enable_relevance_check")));
          chunk_.emit_constant(vm::Value::Bool(node.strategy_options.enable_relevance_check));
          option_count++;
          // enable_support_check
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "enable_support_check")));
          chunk_.emit_constant(vm::Value::Bool(node.strategy_options.enable_support_check));
          option_count++;
          // enable_web_fallback
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "enable_web_fallback")));
          chunk_.emit_constant(vm::Value::Bool(node.strategy_options.enable_web_fallback));
          option_count++;
          // enable_query_decomposition
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "enable_query_decomposition")));
          chunk_.emit_constant(vm::Value::Bool(node.strategy_options.enable_query_decomposition));
          option_count++;
          // max_corrections
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "max_corrections")));
          chunk_.emit_constant(vm::Value::Number(static_cast<double>(node.strategy_options.max_corrections)));
          option_count++;
          // max_iterations
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "max_iterations")));
          chunk_.emit_constant(vm::Value::Number(static_cast<double>(node.strategy_options.max_iterations)));
          option_count++;
          // enable_reflection
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "enable_reflection")));
          chunk_.emit_constant(vm::Value::Bool(node.strategy_options.enable_reflection));
          option_count++;
          // search_depth
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "search_depth")));
          chunk_.emit_constant(vm::Value::Number(static_cast<double>(node.strategy_options.search_depth)));
          option_count++;
          // include_communities
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "include_communities")));
          chunk_.emit_constant(vm::Value::Bool(node.strategy_options.include_communities));
          option_count++;

          emit_build_map(chunk_, option_count);
          chunk_.write_op(OpCode::OP_DEFINE_KNOWLEDGE);
        }
        else if constexpr (std::is_same_v<T, BudgetDecl>)
        {
          for (const auto& dimension : node.dimensions)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, dimension.name)));
            const double value = normalize_budget_value(dimension);
            chunk_.emit_constant(vm::Value::Number(value));
          }
          emit_build_map(chunk_, node.dimensions.size());
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
        }
        else if constexpr (std::is_same_v<T, GuardDecl>)
        {
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "description")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.description)));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "handlers")));
          for (std::size_t index = 0; index < node.handlers.size(); ++index)
          {
            const auto& handler = node.handlers[index];
            const auto handler_name = node.name + "_handler_" + std::to_string(index);
            auto fn_value =
                compile_block_function(handler_name, handler->parameters, *handler->body);
            const auto fn_constant = chunk_.add_constant(std::move(fn_value));

            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "type")));
            std::string type_name;
            switch (handler->type)
            {
              case GuardHandler::Type::kOnObservation:
                type_name = "on_observation";
                break;
              case GuardHandler::Type::kOnAction:
                type_name = "on_action";
                break;
              case GuardHandler::Type::kOnToolInput:
                type_name = "on_tool_input";
                break;
              case GuardHandler::Type::kOnToolOutput:
                type_name = "on_tool_output";
                break;
              case GuardHandler::Type::kOnToolCall:
                type_name = "on_tool_call";
                break;
              case GuardHandler::Type::kOnResult:
                type_name = "on_result";
                break;
            }
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, type_name)));

            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "params")));
            emit_string_list(chunk_, handler->parameters);

            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "returns")));
            if (handler->return_type)
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, handler->return_type->name)));
            }
            else
            {
              chunk_.write_op(OpCode::OP_NIL);
            }

            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "impl")));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(fn_constant));

            emit_build_map(chunk_, 4);
          }
          emit_build_list(chunk_, node.handlers.size());

          emit_build_map(chunk_, 2);
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
        }
        else if constexpr (std::is_same_v<T, GuardChainDecl>)
        {
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "guards")));
          emit_identifier_list(chunk_, node.guards);
          emit_build_map(chunk_, 1);
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
        }
        else if constexpr (std::is_same_v<T, CapabilityDecl>)
        {
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "pattern")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.pattern)));
          emit_build_map(chunk_, 1);
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
        }
        else if constexpr (std::is_same_v<T, ToolDecl>)
        {
          const auto impl_name = node.name + "_impl";
          auto fn_value =
              compile_block_function(impl_name, node.impl->parameters, *node.impl->body);
          const auto fn_constant = chunk_.add_constant(std::move(fn_value));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "description")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.description)));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "capabilities")));
          emit_identifier_list(chunk_, node.capabilities);

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "params")));
          for (const auto& param : node.params)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, param.name)));

            std::size_t param_entries = 1;
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "type")));
            if (param.type_expr)
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, param.type_expr->name)));
            }
            else
            {
              chunk_.write_op(OpCode::OP_NIL);
            }

            if (param.has_default && param.default_value)
            {
              ++param_entries;
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(
                  static_cast<uint16_t>(emit_string_constant(chunk_, "default")));
              emit_expression(*param.default_value);
            }

            emit_build_map(chunk_, param_entries);
          }
          emit_build_map(chunk_, node.params.size());

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "returns")));
          if (node.returns_type)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.returns_type->name)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "budget_costs")));
          for (const auto& cost : node.budget_costs)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, cost.resource)));
            chunk_.emit_constant(vm::Value::Number(cost.amount));
          }
          emit_build_map(chunk_, node.budget_costs.size());

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "guards")));
          emit_identifier_list(chunk_, node.guards);

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "impl")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(fn_constant));

          emit_build_map(chunk_, 7);
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
        }
        else if constexpr (std::is_same_v<T, MemoryDecl>)
        {
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "backend")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.backend)));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "retention")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.retention)));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "max_events")));
          chunk_.emit_constant(vm::Value::Number(node.max_events));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, "snapshot_interval")));
          chunk_.emit_constant(vm::Value::Number(node.snapshot_interval));

          emit_build_map(chunk_, 4);
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
        }
        else if constexpr (std::is_same_v<T, EnvDecl>)
        {
          for (const auto& config : node.configs)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, config.key)));
            if (config.is_env_var)
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(
                  static_cast<uint16_t>(emit_string_constant(chunk_, "env_var")));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, config.env_var_name)));
              emit_build_map(chunk_, 1);
            }
            else
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(
                  static_cast<uint16_t>(emit_string_constant(chunk_, config.value)));
            }
          }
          emit_build_map(chunk_, node.configs.size());
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
        }
        else if constexpr (std::is_same_v<T, ConnectorDecl>)
        {
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "protocol")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.protocol)));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "endpoint")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.endpoint)));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "contract")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.contract)));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "auth")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.auth)));

          emit_build_map(chunk_, 4);
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
        }
        else if constexpr (std::is_same_v<T, WorldModelDecl>)
        {
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "tier")));
          chunk_.emit_constant(vm::Value::Number(node.tier));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, "state_schema")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.state_schema)));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, "update_frequency")));
          chunk_.emit_constant(vm::Value::Number(node.update_frequency));

          emit_build_map(chunk_, 3);
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
        }
        else if constexpr (std::is_same_v<T, PlanDecl>)
        {
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "pattern")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.pattern)));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "max_depth")));
          chunk_.emit_constant(vm::Value::Number(node.max_depth));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "backtrack")));
          chunk_.write_op(node.backtrack ? OpCode::OP_TRUE : OpCode::OP_FALSE);

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "pruning")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.pruning)));

          emit_build_map(chunk_, 4);
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
        }
        else if constexpr (std::is_same_v<T, SubagentDecl>)
        {
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "base_agent")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.base_agent)));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "budget_share")));
          chunk_.emit_constant(vm::Value::Number(node.budget_share));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, "capability_inherit")));
          chunk_.write_op(node.capability_inherit ? OpCode::OP_TRUE : OpCode::OP_FALSE);

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "isolation")));
          chunk_.write_op(node.isolation ? OpCode::OP_TRUE : OpCode::OP_FALSE);

          emit_build_map(chunk_, 4);
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
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

          for (const auto& skill_ref : node.skills)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, skill_ref.name)));
          }
          emit_build_list(chunk_, node.skills.size());
          for (const auto& kb_ref : node.connected_knowledge)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, kb_ref.name)));
          }
          emit_build_list(chunk_, node.connected_knowledge.size());

          emit_identifier_list(chunk_, node.required_capabilities);
          emit_identifier_list(chunk_, node.guardchains);

          if (node.budget.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.budget->name)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.env.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.env->name)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.memory.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.memory->name)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.world_model.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.world_model->name)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.plan.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.plan->name)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.connector.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.connector->name)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          // NEW: Emit context_from path (agents.md integration - Phase 6)
          if (node.context_from.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.context_from.value())));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          // Emit output_type name (for structured output)
          if (node.output_type.has_value() && *node.output_type)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, (*node.output_type)->name)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          // Emit mcp_servers list (Phase 3)
          emit_identifier_list(chunk_, node.mcp_servers);

          // Emit cognitive fields (v0.5.0): reasoning, reflect, learning, goals, evolve, model_path
          if (node.reasoning.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.reasoning.value())));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.reflect_json.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.reflect_json.value())));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.learning_json.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.learning_json.value())));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.goals.has_value())
          {
            // Encode goals + triggers + initiative + budget as JSON
            nlohmann::json goals_json;
            goals_json["goals"] = node.goals.value();
            if (node.triggers_json.has_value())
            {
              goals_json["triggers"] = nlohmann::json::parse(node.triggers_json.value());
            }
            if (node.initiative.has_value())
            {
              goals_json["initiative"] = node.initiative.value();
            }
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, goals_json.dump())));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.evolve_json.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.evolve_json.value())));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          if (node.model_path.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.model_path.value())));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          chunk_.write_op(OpCode::OP_DEFINE_AGENT);

          // Emit handoffs for this agent (OpenAI SDK style)
          for (const auto& handoff : node.handoffs)
          {
            // Push agent name
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));

            // Push target agent name
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, handoff.agent.name)));

            // Push tool_name_override (or nil for default)
            if (handoff.tool_name_override.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, handoff.tool_name_override.value())));
            }
            else
            {
              chunk_.write_op(OpCode::OP_NIL);
            }

            // Push description (or nil for default)
            if (handoff.description.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, handoff.description.value())));
            }
            else
            {
              chunk_.write_op(OpCode::OP_NIL);
            }

            // Push input_filter function (or nil)
            // Need to resolve the function from globals
            if (handoff.input_filter.has_value())
            {
              // Emit GET_GLOBAL to resolve the function by name
              const auto name_constant = chunk_.add_constant(
                  vm::Value::String(handoff.input_filter->name.c_str(),
                                    handoff.input_filter->name.size()));
              chunk_.write_op(OpCode::OP_GET_GLOBAL);
              chunk_.write_short(static_cast<uint16_t>(name_constant));
            }
            else
            {
              chunk_.write_op(OpCode::OP_NIL);
            }

            // Push is_enabled expression (or nil)
            if (handoff.is_enabled && *handoff.is_enabled)
            {
              emit_expression(**handoff.is_enabled);
            }
            else
            {
              chunk_.write_op(OpCode::OP_NIL);
            }

            // Push on_handoff callback (or nil)
            if (handoff.on_handoff && *handoff.on_handoff)
            {
              emit_expression(**handoff.on_handoff);
            }
            else
            {
              chunk_.write_op(OpCode::OP_NIL);
            }

            // Push input_type name (or nil)
            if (handoff.input_type.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, handoff.input_type->name)));
            }
            else
            {
              chunk_.write_op(OpCode::OP_NIL);
            }

            chunk_.write_op(OpCode::OP_DEFINE_HANDOFF);
          }
        }
        else if constexpr (std::is_same_v<T, RunnerDecl>)
        {
          // Emit runner name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));

          // Emit entry agent name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.entry_agent.name)));

          // Emit max_turns (or default 10)
          if (node.max_turns.has_value())
          {
            chunk_.emit_constant(vm::Value::Number(static_cast<double>(node.max_turns.value())));
          }
          else
          {
            chunk_.emit_constant(vm::Value::Number(10.0));
          }

          // Emit tracing (identifier ref or nil)
          if (node.tracing.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, node.tracing->name)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          // Emit guardrails list (backward compat)
          for (const auto& guardrail : node.guardrails)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, guardrail.name)));
          }
          emit_build_list(chunk_, node.guardrails.size());

          // Emit input_guardrails list
          for (const auto& guardrail : node.input_guardrails)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, guardrail.name)));
          }
          emit_build_list(chunk_, node.input_guardrails.size());

          // Emit output_guardrails list
          for (const auto& guardrail : node.output_guardrails)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, guardrail.name)));
          }
          emit_build_list(chunk_, node.output_guardrails.size());

          // Emit on_turn callback (or nil)
          if (node.on_turn && *node.on_turn)
          {
            emit_expression(**node.on_turn);
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          // Emit on_complete callback (or nil)
          if (node.on_complete && *node.on_complete)
          {
            emit_expression(**node.on_complete);
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

          chunk_.write_op(OpCode::OP_DEFINE_RUNNER);
        }
        else if constexpr (std::is_same_v<T, GrantStmt>)
        {
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.capability.name)));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.target.name)));
          chunk_.write_op(OpCode::OP_GRANT);
        }
        else if constexpr (std::is_same_v<T, CheckpointStmt>)
        {
          if (!node.label.empty())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, node.label)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }
          chunk_.write_op(OpCode::OP_CHECKPOINT);
        }
        else if constexpr (std::is_same_v<T, RewindStmt>)
        {
          if (node.target)
          {
            emit_expression(*node.target);
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }
          chunk_.write_op(OpCode::OP_REWIND);
        }
        else if constexpr (std::is_same_v<T, VoicePipelineDecl>)
        {
          // Emit pipeline name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));

          // Emit agent name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.agent.name)));

          // Emit stt_provider (or default)
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.stt_provider.value_or("whisper"))));

          // Emit stt_model (or default)
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.stt_model.value_or("whisper-1"))));

          // Emit tts_provider (or default)
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.tts_provider.value_or("openai"))));

          // Emit tts_model (or default)
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.tts_model.value_or("tts-1"))));

          // Emit tts_voice (or default)
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.tts_voice.value_or("alloy"))));

          // Emit 7 new fields (or empty string defaults)
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.stt_endpoint.value_or(""))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.tts_endpoint.value_or(""))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.tts_format.value_or(""))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.tts_speed.value_or(""))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.tts_instructions.value_or(""))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.stt_language.value_or(""))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.stt_format.value_or(""))));

          chunk_.write_op(OpCode::OP_DEFINE_VOICE_PIPELINE);
        }
        else if constexpr (std::is_same_v<T, RealtimeVoiceDecl>)
        {
          // Emit config name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));

          // Emit agent name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.agent.name)));

          // Emit 14 config fields (or defaults)
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.provider.value_or("openai"))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.model.value_or(""))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.voice.value_or(""))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.vad.value_or("server"))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.vad_threshold.value_or("0.5"))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.silence_duration_ms.value_or("500"))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.input_format.value_or("pcm16"))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.output_format.value_or("pcm16"))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.sample_rate.value_or("24000"))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.speed.value_or("1.0"))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.stt_endpoint.value_or(""))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.tts_endpoint.value_or(""))));

          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.llm_endpoint.value_or(""))));

          chunk_.write_op(OpCode::OP_DEFINE_REALTIME_VOICE);
        }
        else if constexpr (std::is_same_v<T, TryCatchStmt>)
        {
          // OP_TRY_BEGIN <catch_offset>  -- push exception handler
          const auto catch_jump = emit_jump(chunk_, OpCode::OP_TRY_BEGIN);

          // try body
          emit_statement(*node.try_body);

          // OP_TRY_END  -- pop exception handler on normal path
          chunk_.write_op(OpCode::OP_TRY_END);

          // Jump past catch block on normal path
          const auto end_jump = emit_jump(chunk_, OpCode::OP_JUMP);

          // Patch catch jump target to here
          patch_jump(chunk_, catch_jump);

          // At this point the VM has pushed the error value onto the stack.
          // Create a local variable for the catch variable.
          begin_scope();
          locals_.push_back(Local{node.catch_var, scope_depth_});

          // catch body
          emit_statement(*node.catch_body);

          // end_scope pops the catch variable
          end_scope();

          // Patch end jump
          patch_jump(chunk_, end_jump);
        }
        else if constexpr (std::is_same_v<T, ThrowStmt>)
        {
          emit_expression(*node.value);
          chunk_.write_op(OpCode::OP_THROW);
        }
      },
      stmt.node);
}

void Compiler::emit_expression(const Expression& expr)
{
  chunk_.set_current_line(expr.span.line);
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
            case BinaryOp::Modulo:
              chunk_.write_op(OpCode::OP_MOD);
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
        else if constexpr (std::is_same_v<T, IndexExpr>)
        {
          emit_expression(*node.base);
          emit_expression(*node.index);
          chunk_.write_op(OpCode::OP_GET_INDEX);
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
            emit_expression(*entry.key);
            emit_expression(*entry.value);
          }
          emit_build_map(chunk_, node.entries.size());
        }
        else if constexpr (std::is_same_v<T, FileOpenExpr>)
        {
          chunk_.write_op(OpCode::OP_GET_GLOBAL);
          chunk_.write_short(static_cast<uint16_t>(
              chunk_.add_constant(vm::Value::String("file_open", 9))));
          emit_expression(*node.path);
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              chunk_.add_constant(vm::Value::String(node.mode.c_str(), node.mode.size()))));
          chunk_.write_op(OpCode::OP_CALL_NATIVE);
          chunk_.write_byte(2);
        }
        else if constexpr (std::is_same_v<T, PathLiteralExpr>)
        {
          chunk_.emit_constant(vm::Value::String(node.path.c_str(), node.path.size()));
        }
        else if constexpr (std::is_same_v<T, TryExpr>)
        {
          emit_expression(*node.expr);
          chunk_.write_op(OpCode::OP_DUP);
          chunk_.write_op(OpCode::OP_GET_PROPERTY);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, "ok")));
          const auto error_jump = emit_jump(chunk_, OpCode::OP_JUMP_IF_FALSE);
          chunk_.write_op(OpCode::OP_POP);
          chunk_.write_op(OpCode::OP_GET_PROPERTY);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, "value")));
          const auto done_jump = emit_jump(chunk_, OpCode::OP_JUMP);
          patch_jump(chunk_, error_jump);
          chunk_.write_op(OpCode::OP_POP);
          chunk_.write_op(OpCode::OP_RETURN);
          patch_jump(chunk_, done_jump);
        }
        else if constexpr (std::is_same_v<T, PanicExpr>)
        {
          chunk_.write_op(OpCode::OP_GET_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, "panic")));
          emit_expression(*node.message);
          chunk_.write_op(OpCode::OP_CALL_NATIVE);
          chunk_.write_byte(1);
        }
        else if constexpr (std::is_same_v<T, CatchPanicExpr>)
        {
          emit_expression(*node.closure);
        }
        else if constexpr (std::is_same_v<T, ContextExpr>)
        {
          chunk_.write_op(OpCode::OP_GET_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, "context")));
          emit_expression(*node.expr);
          emit_expression(*node.message);
          chunk_.write_op(OpCode::OP_CALL_NATIVE);
          chunk_.write_byte(2);
        }
        else if constexpr (std::is_same_v<T, WithContextExpr>)
        {
          chunk_.write_op(OpCode::OP_GET_GLOBAL);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, "with_context")));
          emit_expression(*node.expr);
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.key)));
          emit_expression(*node.value);
          chunk_.write_op(OpCode::OP_CALL_NATIVE);
          chunk_.write_byte(3);
        }
      },
      expr.node);
}
void Compiler::set_module_context(module::ModuleResolver* resolver,
                                  module::ModuleCache* module_cache,
                                  module::ModuleGraph* dep_graph,
                                  const std::string& current_module)
{
  resolver_ = resolver;
  module_cache_ = module_cache;
  dep_graph_ = dep_graph;
  current_module_ = current_module;
}

void Compiler::compile_import(const ImportDecl& node)
{
  if (!resolver_ || !module_cache_ || !dep_graph_)
  {
    // No module context set — silently ignore imports (legacy mode)
    return;
  }

  // Build module path string for graph and cache key
  std::string mod_path_str;
  for (std::size_t i = 0; i < node.path.size(); ++i)
  {
    if (i > 0) mod_path_str += ".";
    mod_path_str += node.path[i];
  }

  // Record dependency in graph
  dep_graph_->add_module(current_module_);
  dep_graph_->add_module(mod_path_str);
  dep_graph_->add_import(current_module_, mod_path_str);

  // Check for cycles
  std::vector<std::string> cycle;
  if (dep_graph_->has_cycle(&cycle))
  {
    std::string cycle_str;
    for (const auto& c : cycle)
    {
      if (!cycle_str.empty()) cycle_str += " -> ";
      cycle_str += c;
    }
    throw std::runtime_error("Circular import detected: " + cycle_str);
  }

  // Resolve file path
  auto location = resolver_->resolve(node.path);
  if (!location)
  {
    throw std::runtime_error("Could not resolve module: " + mod_path_str);
  }

  // Compile the module if not already cached
  auto* cached = module_cache_->get(location->path.string());
  if (!cached)
  {
    compile_module_file(mod_path_str, location->path);
    cached = module_cache_->get(location->path.string());
  }
  else if (cached->is_compiling)
  {
    throw std::runtime_error("Circular import detected: " + mod_path_str);
  }

  if (!cached || !cached->is_compiled)
  {
    return;
  }

  // Import exported symbols into current compilation unit as globals
  for (const auto& sym : cached->exports)
  {
    // Apply selective imports
    if (!node.items.empty())
    {
      bool found = false;
      for (const auto& item : node.items)
      {
        if (item == sym.name)
        {
          found = true;
          break;
        }
      }
      if (!found) continue;
    }
    else if (!node.is_wildcard && node.alias.has_value())
    {
      // import foo.bar as baz; — import the module itself, skip individual symbols
      continue;
    }

    // Only import public symbols
    if (sym.visibility != module::VisibilityLevel::kPublic)
    {
      continue;
    }

    // Emit the value as a constant and define it as a global
    const auto constant_index = chunk_.add_constant(sym.value);
    chunk_.write_op(vm::OpCode::OP_CONST);
    chunk_.write_short(static_cast<uint16_t>(constant_index));

    std::string global_name = sym.name;
    // If alias is set and this is a selective import, don't alias individual names
    // (alias applies to the module name in non-selective imports)

    const auto name_constant =
        chunk_.add_constant(vm::Value::String(global_name.c_str(), global_name.size()));
    chunk_.write_op(vm::OpCode::OP_DEFINE_GLOBAL);
    chunk_.write_short(static_cast<uint16_t>(name_constant));
  }
}

void Compiler::compile_module_file(const std::string& module_name,
                                   const std::filesystem::path& path)
{
  // Read the source file
  std::ifstream file(path);
  if (!file.is_open())
  {
    throw std::runtime_error("Cannot open module file: " + path.string());
  }
  std::string source((std::istreambuf_iterator<char>(file)),
                     std::istreambuf_iterator<char>());

  // Create cache entry and mark as compiling
  auto& cached = module_cache_->create(path.string());
  cached.module_name = module_name;
  cached.is_compiling = true;

  // Parse and compile
  Parser parser(source);
  Program program = parser.parse();

  Compiler nested_compiler;
  nested_compiler.set_module_context(resolver_, module_cache_, dep_graph_, module_name);
  vm::Chunk module_chunk = nested_compiler.compile(program);

  cached.chunk = std::move(module_chunk);

  // Collect exported symbols: all top-level function and const declarations
  // with public visibility become exports
  for (const auto& stmt : program.statements)
  {
    std::visit(
        [&](auto&& node)
        {
          using T = std::decay_t<decltype(node)>;
          if constexpr (std::is_same_v<T, FunctionDecl>)
          {
            if (node.visibility.level == Visibility::Level::kPublic ||
                node.visibility.level == Visibility::Level::kPrivate)
            {
              // Re-compile the function to get its value
              auto fn_value = nested_compiler.compile_function(node);
              module::ExportedSymbol sym;
              sym.name = node.name;
              sym.value = std::move(fn_value);
              sym.visibility = module::VisibilityLevel::kPublic;
              sym.kind = module::SymbolKind::kFunction;
              cached.exports.push_back(std::move(sym));
            }
          }
          else if constexpr (std::is_same_v<T, ConstDecl>)
          {
            if (node.visibility.level == Visibility::Level::kPublic ||
                node.visibility.level == Visibility::Level::kPrivate)
            {
              // For constants, we can't easily extract the runtime value at compile time.
              // Instead, store a placeholder — the VM will resolve via OP_GET_GLOBAL.
              module::ExportedSymbol sym;
              sym.name = node.name;
              sym.value = vm::Value::Nil();
              sym.visibility = module::VisibilityLevel::kPublic;
              sym.kind = module::SymbolKind::kConstant;
              cached.exports.push_back(std::move(sym));
            }
          }
        },
        stmt->node);
  }

  cached.is_compiling = false;
  cached.is_compiled = true;
}
}  // namespace neamc
