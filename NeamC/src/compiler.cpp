//
// NeamC - Compiler backend implementation
//

#include "neamc/compiler.hpp"

#include <cstdio>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <queue>
#include <stdexcept>
#include <unordered_set>
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
  if (count > std::numeric_limits<uint16_t>::max())
  {
    throw std::runtime_error("List literal too large (max " +
        std::to_string(std::numeric_limits<uint16_t>::max()) + " elements)");
  }
  chunk.write_op(OpCode::OP_BUILD_LIST);
  chunk.write_short(static_cast<uint16_t>(count));
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
  if (count > std::numeric_limits<uint16_t>::max())
  {
    throw std::runtime_error("Map literal too large (max " +
        std::to_string(std::numeric_limits<uint16_t>::max()) + " entries)");
  }
  chunk.write_op(OpCode::OP_BUILD_MAP);
  chunk.write_short(static_cast<uint16_t>(count));
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
            default:
              throw std::runtime_error(
                  "Unhandled assert kind: " +
                  std::to_string(static_cast<int>(node.kind)));
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
          std::cerr << "[warning] 'test' declarations are not yet compiled. "
                       "Test '" << node.name << "' will be ignored.\n";
        }
        else if constexpr (std::is_same_v<T, TestSuiteDecl>)
        {
          std::cerr << "[warning] 'test suite' declarations are not yet compiled. "
                       "Suite '" << node.name << "' will be ignored.\n";
        }
        else if constexpr (std::is_same_v<T, ModuleDecl>)
        {
          std::string path;
          for (std::size_t i = 0; i < node.path.size(); ++i)
          {
            if (i > 0) path += ".";
            path += node.path[i];
          }
          std::cerr << "[warning] 'module' declarations are not yet compiled. "
                       "Module '" << path << "' will be ignored.\n";
        }
        else if constexpr (std::is_same_v<T, ImportDecl>)
        {
          std::string path;
          for (std::size_t i = 0; i < node.path.size(); ++i)
          {
            if (i > 0) path += ".";
            path += node.path[i];
          }
          std::cerr << "[warning] 'import' statements are not yet compiled. "
                       "Import of '" << path << "' will be ignored.\n";
        }
        else if constexpr (std::is_same_v<T, TypeAlias>)
        {
          std::cerr << "[warning] 'type' aliases are not yet compiled. "
                       "Type alias '" << node.name << "' will be ignored.\n";
        }
        else if constexpr (std::is_same_v<T, DocComment>)
        {
          // Doc comments are safely ignorable — no warning needed
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
          // v0.6.9 D10: sensitive marker
          chunk_.write_op(node.sensitive ? OpCode::OP_TRUE : OpCode::OP_FALSE);
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
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "type")));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, source.type)));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "path")));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, source.path)));
            emit_build_map(chunk_, 2);
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
              default:
                throw std::runtime_error(
                    "Unhandled guard handler type: " +
                    std::to_string(static_cast<int>(handler->type)));
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
        // v0.6.9: Policy codegen
        else if constexpr (std::is_same_v<T, PolicyDecl>)
        {
          // Emit "allow" key + list
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "allow")));
          emit_string_list(chunk_, node.allow_tools);

          // Emit "deny" key + list
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "deny")));
          emit_string_list(chunk_, node.deny_tools);

          // Emit "confirm" key + list
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "confirm")));
          emit_string_list(chunk_, node.confirm_tools);

          // Emit "default_deny" key + value
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, "default_deny")));
          if (node.default_deny)
          {
            chunk_.write_op(OpCode::OP_TRUE);
          }
          else
          {
            chunk_.write_op(OpCode::OP_FALSE);
          }

          emit_build_map(chunk_, 4);
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

          // v0.6.9: emit policy reference
          if (node.policy.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.policy->name)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }

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
          chunk_.write_op(OpCode::OP_DEFINE_AGENT);
          agent_types_[node.name] = AgentKind::Stateless;
        }
        // v0.8: Claw agent emit handler
        else if constexpr (std::is_same_v<T, ClawAgentDecl>)
        {
          // name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // provider
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.provider)));
          // model
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.model)));
          // endpoint
          if (node.endpoint.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.endpoint.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // api_key_env
          if (node.api_key_env.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.api_key_env.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // temperature
          if (node.temperature.has_value())
          {
            chunk_.emit_constant(vm::Value::Number(node.temperature.value()));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // system
          if (node.system.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.system.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // skills list
          emit_identifier_list(chunk_, node.skills);
          // connected_knowledge list
          emit_identifier_list(chunk_, node.connected_knowledge);
          // guardchains list
          emit_identifier_list(chunk_, node.guardchains);
          // policy
          if (node.policy.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.policy->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // budget
          if (node.budget.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.budget->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // env
          if (node.env.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.env->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // workspace
          if (node.workspace.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.workspace.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // session config as map
          {
            std::size_t count = 0;
            const auto& s = node.session;
            if (s.idle_reset_minutes.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "idle_reset_minutes")));
              chunk_.emit_constant(vm::Value::Number(static_cast<double>(*s.idle_reset_minutes)));
              ++count;
            }
            if (s.daily_reset_hour.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "daily_reset_hour")));
              chunk_.emit_constant(vm::Value::Number(static_cast<double>(*s.daily_reset_hour)));
              ++count;
            }
            if (s.max_history_turns.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "max_history_turns")));
              chunk_.emit_constant(vm::Value::Number(static_cast<double>(*s.max_history_turns)));
              ++count;
            }
            if (s.compaction.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "compaction")));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, *s.compaction)));
              ++count;
            }
            emit_build_map(chunk_, count);
          }
          // channels list
          emit_identifier_list(chunk_, node.channels);
          // lanes list (list of maps)
          for (const auto& lane : node.lanes)
          {
            std::size_t lcount = 1;  // name is always present
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, "name")));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, lane.name)));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, "concurrency")));
            chunk_.emit_constant(vm::Value::Number(static_cast<double>(lane.concurrency)));
            ++lcount;
            if (lane.priority.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "priority")));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, *lane.priority)));
              ++lcount;
            }
            emit_build_map(chunk_, lcount);
          }
          emit_build_list(chunk_, node.lanes.size());
          // semantic_memory
          if (node.semantic_memory.has_value())
          {
            std::size_t scount = 0;
            const auto& sm = *node.semantic_memory;
            if (sm.backend.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "backend")));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, *sm.backend)));
              ++scount;
            }
            if (sm.search.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "search")));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, *sm.search)));
              ++scount;
            }
            // flush_on_compact
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, "flush_on_compact")));
            if (sm.flush_on_compact)
            {
              chunk_.write_op(OpCode::OP_TRUE);
            }
            else
            {
              chunk_.write_op(OpCode::OP_FALSE);
            }
            ++scount;
            emit_build_map(chunk_, scount);
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          chunk_.write_op(OpCode::OP_DEFINE_CLAW_AGENT);
          agent_types_[node.name] = AgentKind::Claw;
        }
        // v0.8: Forge agent emit handler
        else if constexpr (std::is_same_v<T, ForgeAgentDecl>)
        {
          // name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // provider
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.provider)));
          // model
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.model)));
          // endpoint
          if (node.endpoint.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.endpoint.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // api_key_env
          if (node.api_key_env.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.api_key_env.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // temperature
          if (node.temperature.has_value())
          {
            chunk_.emit_constant(vm::Value::Number(node.temperature.value()));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // system
          if (node.system.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.system.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // skills list
          emit_identifier_list(chunk_, node.skills);
          // guardchains list
          emit_identifier_list(chunk_, node.guardchains);
          // policy
          if (node.policy.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.policy->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // budget
          if (node.budget.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.budget->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // env
          if (node.env.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.env->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // workspace
          if (node.workspace.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.workspace.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // loop config as map
          {
            std::size_t count = 0;
            const auto& lp = node.loop;
            if (lp.max_iterations.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "max_iterations")));
              chunk_.emit_constant(vm::Value::Number(static_cast<double>(*lp.max_iterations)));
              ++count;
            }
            if (lp.max_cost.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "max_cost")));
              chunk_.emit_constant(vm::Value::Number(*lp.max_cost));
              ++count;
            }
            if (lp.max_tokens.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "max_tokens")));
              chunk_.emit_constant(vm::Value::Number(static_cast<double>(*lp.max_tokens)));
              ++count;
            }
            if (lp.prompt_file.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "prompt_file")));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, *lp.prompt_file)));
              ++count;
            }
            if (lp.plan_file.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "plan_file")));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, *lp.plan_file)));
              ++count;
            }
            if (lp.progress_file.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "progress_file")));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, *lp.progress_file)));
              ++count;
            }
            if (lp.learnings_file.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "learnings_file")));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, *lp.learnings_file)));
              ++count;
            }
            emit_build_map(chunk_, count);
          }
          // verify expression
          emit_expression(*node.verify);
          // checkpoint
          if (node.checkpoint.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.checkpoint.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          chunk_.write_op(OpCode::OP_DEFINE_FORGE_AGENT);
          agent_types_[node.name] = AgentKind::Forge;

          // v1.4.5 Phase 7: if the forge agent has role/function/ops set,
          // emit a secondary OP_DEFINE_DIO_DECLARATION (sub-type 30) that
          // registers the metadata into HarnessRegistry. This keeps the
          // legacy OP_DEFINE_FORGE_AGENT path unchanged.
          if (!node.role.empty() || !node.function_json.empty() ||
              !node.ops_json.empty())
          {
            uint8_t field_count = 0;
            auto push_str_md = [&](const std::string& k, const std::string& v) {
              chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
              chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
              field_count++; };
            push_str_md("name", node.name);
            if (!node.role.empty())          push_str_md("role", node.role);
            if (!node.function_json.empty()) push_str_md("function", node.function_json);
            if (!node.ops_json.empty())      push_str_md("ops", node.ops_json);
            chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
            chunk_.write_byte(30); /* sub-type 30 = ForgeMetadata */
            chunk_.write_byte(field_count);
          }
        }
        // v0.9: Schema declaration
        else if constexpr (std::is_same_v<T, SchemaDecl>)
        {
          // name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // fields as a map: { field_name: { type: "...", constraints: [...] } }
          std::size_t field_count = 0;
          for (const auto& field : node.fields)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, field.name)));
            // Build field info map
            std::size_t info_count = 1;  // type always present
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "type")));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, field.type_name)));
            if (!field.constraints.empty())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, "constraints")));
              for (const auto& c : field.constraints)
              {
                chunk_.write_op(OpCode::OP_CONST);
                chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, c.type)));
              }
              emit_build_list(chunk_, field.constraints.size());
              ++info_count;
            }
            emit_build_map(chunk_, info_count);
            ++field_count;
          }
          emit_build_map(chunk_, field_count);
          // version
          if (node.version.has_value())
          {
            chunk_.emit_constant(vm::Value::Number(static_cast<double>(*node.version)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }
          chunk_.write_op(OpCode::OP_DEFINE_SCHEMA);
          schema_defs_[node.name] = true;
        }
        // v0.9: Source declaration
        else if constexpr (std::is_same_v<T, SourceDecl>)
        {
          // name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // type
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.type)));
          // connection
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.connection)));
          // format
          if (node.format.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, *node.format)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // refresh
          if (node.refresh.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, *node.refresh)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // schema_ref
          if (node.schema_ref.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, *node.schema_ref)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // classification
          if (node.classification.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, *node.classification)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // mode
          if (node.mode.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, *node.mode)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // partition_by
          emit_string_list(chunk_, node.partition_by);
          chunk_.write_op(OpCode::OP_DEFINE_SOURCE);
          source_defs_[node.name] = true;
        }
        // v0.9: Sink declaration
        else if constexpr (std::is_same_v<T, SinkDecl>)
        {
          // name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // type
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.type)));
          // connection
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.connection)));
          // format
          if (node.format.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, *node.format)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // write_mode
          if (node.write_mode.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, *node.write_mode)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // batch_size
          if (node.batch_size.has_value())
          {
            chunk_.emit_constant(vm::Value::Number(static_cast<double>(*node.batch_size)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // schema_ref
          if (node.schema_ref.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, *node.schema_ref)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // compute_ref
          if (node.compute_ref.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, *node.compute_ref)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          chunk_.write_op(OpCode::OP_DEFINE_SINK);
        }
        // v0.9: Quality declaration
        else if constexpr (std::is_same_v<T, QualityDecl>)
        {
          // name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // freshness
          if (node.freshness.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, *node.freshness)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // completeness
          if (node.completeness.has_value())
          {
            chunk_.emit_constant(vm::Value::Number(*node.completeness));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // uniqueness
          emit_string_list(chunk_, node.uniqueness);
          // drift_detection
          if (node.drift_detection.has_value())
          {
            chunk_.write_op(*node.drift_detection ? OpCode::OP_TRUE : OpCode::OP_FALSE);
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // anomaly_threshold
          if (node.anomaly_threshold.has_value())
          {
            chunk_.emit_constant(vm::Value::Number(*node.anomaly_threshold));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // on_violation
          if (node.on_violation.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, *node.on_violation)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          chunk_.write_op(OpCode::OP_DEFINE_QUALITY);
        }
        // v0.9: Compute declaration
        else if constexpr (std::is_same_v<T, ComputeDecl>)
        {
          // name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // engine
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.engine)));
          // config (optional expression)
          if (node.config)
          {
            emit_expression(*node.config);
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          chunk_.write_op(OpCode::OP_DEFINE_COMPUTE);
        }
        // v0.9: Governance declaration
        else if constexpr (std::is_same_v<T, GovernanceDecl>)
        {
          // name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // body (expression, typically a map)
          if (node.body)
          {
            emit_expression(*node.body);
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          chunk_.write_op(OpCode::OP_DEFINE_GOVERNANCE);
        }
        // v0.9: Catalog declaration
        else if constexpr (std::is_same_v<T, CatalogDecl>)
        {
          // name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // engine
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.engine)));
          // register_opts (optional expression)
          if (node.register_opts)
          {
            emit_expression(*node.register_opts);
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // discovery
          if (node.discovery.has_value())
          {
            chunk_.write_op(*node.discovery ? OpCode::OP_TRUE : OpCode::OP_FALSE);
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          chunk_.write_op(OpCode::OP_DEFINE_CATALOG);
        }
        // v0.9: Data agent declaration
        else if constexpr (std::is_same_v<T, DataAgentDecl>)
        {
          // name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // provider
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.provider)));
          // model
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.model)));
          // endpoint
          if (node.endpoint.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.endpoint.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // api_key_env
          if (node.api_key_env.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.api_key_env.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // temperature
          if (node.temperature.has_value())
          {
            chunk_.emit_constant(vm::Value::Number(node.temperature.value()));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // system
          if (node.system.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.system.value())));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // skills list
          emit_identifier_list(chunk_, node.skills);
          // guardchains list
          emit_identifier_list(chunk_, node.guardchains);
          // policy
          if (node.policy.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.policy->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // budget
          if (node.budget.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.budget->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // env
          if (node.env.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.env->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // sources list
          emit_identifier_list(chunk_, node.sources);
          // sinks list
          emit_identifier_list(chunk_, node.sinks);
          // schema_ref
          if (node.schema_ref.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.schema_ref->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // quality_ref
          if (node.quality_ref.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.quality_ref->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // compute block as map: { default: "...", available: [...], routing: { stage: engine } }
          if (node.compute.has_value())
          {
            std::size_t ccount = 0;
            const auto& cb = *node.compute;
            if (cb.default_engine.has_value())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "default")));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, cb.default_engine->name)));
              ++ccount;
            }
            if (!cb.available.empty())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "available")));
              emit_identifier_list(chunk_, cb.available);
              ++ccount;
            }
            if (!cb.routing.empty())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "routing")));
              std::size_t rcount = 0;
              for (const auto& [stage, engine] : cb.routing)
              {
                chunk_.write_op(OpCode::OP_CONST);
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, stage)));
                chunk_.write_op(OpCode::OP_CONST);
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, engine.name)));
                ++rcount;
              }
              emit_build_map(chunk_, rcount);
              ++ccount;
            }
            emit_build_map(chunk_, ccount);
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // governance_ref
          if (node.governance_ref.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.governance_ref->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // catalog_ref
          if (node.catalog_ref.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.catalog_ref->name)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // lineage
          if (node.lineage.has_value())
          {
            chunk_.write_op(*node.lineage ? OpCode::OP_TRUE : OpCode::OP_FALSE);
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // role
          if (node.role.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, *node.role)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // purpose
          if (node.purpose.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, *node.purpose)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // autonomy
          if (node.autonomy.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, *node.autonomy)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // pipeline as map: { extract: [...], transform: [...], load: [...] }
          if (node.pipeline.has_value())
          {
            std::size_t pcount = 0;
            const auto& pl = *node.pipeline;
            // extract
            if (!pl.extract.empty())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "extract")));
              emit_identifier_list(chunk_, pl.extract);
              ++pcount;
            }
            // transform (list of maps: each op has name + args)
            if (!pl.transforms.empty())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "transform")));
              for (const auto& op : pl.transforms)
              {
                std::size_t ocount = 1;  // name always present
                chunk_.write_op(OpCode::OP_CONST);
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, "name")));
                chunk_.write_op(OpCode::OP_CONST);
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, op.name)));
                for (const auto& [key, val] : op.args)
                {
                  chunk_.write_op(OpCode::OP_CONST);
                  chunk_.write_short(static_cast<uint16_t>(
                      emit_string_constant(chunk_, key)));
                  emit_expression(*val);
                  ++ocount;
                }
                emit_build_map(chunk_, ocount);
              }
              emit_build_list(chunk_, pl.transforms.size());
              ++pcount;
            }
            // load
            if (!pl.load.empty())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, "load")));
              emit_identifier_list(chunk_, pl.load);
              ++pcount;
            }
            emit_build_map(chunk_, pcount);
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          // agent_md
          if (node.agent_md.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, *node.agent_md)));
          }
          else { chunk_.write_op(OpCode::OP_NIL); }
          chunk_.write_op(OpCode::OP_DEFINE_DATA_AGENT);
          agent_types_[node.name] = AgentKind::Data;
        }
        // v0.9.1: ETL agent declaration
        else if constexpr (std::is_same_v<T, ETLAgentDecl>)
        {
          uint8_t field_count = 0;

          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };

          auto push_bool = [&](const std::string& n, bool v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::Bool(v));
            field_count++;
          };

          auto push_refs = [&](const std::string& n, const std::vector<IdentifierRef>& refs) {
            // Serialize ref list as comma-separated string to keep 2-slot-per-field invariant
            std::string joined;
            for (size_t i = 0; i < refs.size(); ++i) {
              if (i > 0) joined += ",";
              joined += refs[i].name;
            }
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(joined.c_str(), joined.size()));
            field_count++;
          };

          // Shared agent fields
          push_str("name", node.name);
          push_str("agent_type", "etl");
          push_str("provider", node.provider);
          push_str("model", node.model);
          if (node.system) push_str("system", *node.system);
          if (node.temperature)
          {
            chunk_.emit_constant(vm::Value::String("temperature", 11));
            chunk_.emit_constant(vm::Value::Number(*node.temperature));
            field_count++;
          }
          if (node.endpoint) push_str("endpoint", *node.endpoint);
          if (node.api_key_env) push_str("api_key_env", *node.api_key_env);
          if (node.budget) push_str("budget", node.budget->name);

          // Inherited DataAgent fields
          push_refs("sources", node.sources);
          if (!node.sinks.empty()) push_refs("sinks", node.sinks);
          if (!node.skills.empty()) push_refs("skills", node.skills);
          if (!node.connected_knowledge.empty())
            push_refs("connected_knowledge", node.connected_knowledge);
          if (!node.guardchains.empty()) push_refs("guardchains", node.guardchains);

          if (node.quality) push_str("quality", node.quality->name);
          if (node.governance) push_str("governance", node.governance->name);
          if (node.catalog) push_str("catalog", node.catalog->name);
          if (node.lineage) push_bool("lineage", *node.lineage);

          if (node.compute && node.compute->default_engine)
            push_str("compute_default", node.compute->default_engine->name);

          if (node.role) push_str("role", *node.role);
          if (node.purpose) push_str("purpose", *node.purpose);
          if (node.autonomy) push_str("autonomy", *node.autonomy);

          // ETL agent-specific fields
          push_str("warehouse", node.warehouse.name);

          if (node.model_type) push_str("model_type", *node.model_type);
          if (node.semantic) push_str("semantic", node.semantic->name);
          if (node.self_heal_flag) push_bool("self_heal", *node.self_heal_flag);
          if (node.on_failure) push_str("on_failure", *node.on_failure);

          // Layers
          if (node.layers)
          {
            const auto& layers = *node.layers;
            push_bool("has_layers", true);

            if (layers.staging)
            {
              if (layers.staging->prefix)
                push_str("staging_prefix", *layers.staging->prefix);
              if (layers.staging->materialization)
                push_str("staging_materialization", *layers.staging->materialization);
            }
            if (layers.integration)
            {
              if (layers.integration->prefix)
                push_str("integration_prefix", *layers.integration->prefix);
              if (layers.integration->materialization)
                push_str("integration_materialization", *layers.integration->materialization);
            }

            // Mart count
            chunk_.emit_constant(vm::Value::String("mart_count", 10));
            chunk_.emit_constant(vm::Value::Number(static_cast<double>(layers.marts.size())));
            field_count++;
          }

          // Incremental config
          if (node.incremental)
          {
            const auto& inc = *node.incremental;
            push_str("incremental_strategy", inc.strategy);
            if (inc.key) push_str("incremental_key", *inc.key);
            if (inc.lookback) push_str("incremental_lookback", *inc.lookback);
            if (inc.on_schema_change) push_str("incremental_on_schema_change", *inc.on_schema_change);
          }

          // Auto-model config
          if (node.auto_model)
          {
            const auto& am = *node.auto_model;
            push_bool("auto_model_enabled", am.enabled);
            if (am.methodology) push_str("auto_model_methodology", *am.methodology);
            if (am.approval) push_str("auto_model_approval", *am.approval);
          }

          // Compiler tracking
          agent_types_[node.name] = AgentKind::ETL;
          etl_agent_defs_[node.name] = {
            static_cast<int>(node.sources.size()),
            true,
            node.layers ? static_cast<int>(node.layers->marts.size()) : 0,
          };

          chunk_.write_op(vm::OpCode::OP_DEFINE_ETL_AGENT);
          chunk_.write_byte(field_count);
        }
        // v0.9.2: Migration agent declaration
        else if constexpr (std::is_same_v<T, MigrationAgentDecl>)
        {
          validate_migration_agent(node);

          uint8_t field_count = 0;

          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };

          auto push_bool = [&](const std::string& n, bool v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::Bool(v));
            field_count++;
          };
          (void)push_bool;

          // Agent name
          push_str("name", node.name);
          push_str("agent_type", "migration");

          // Common agent fields
          if (node.provider) push_str("provider", *node.provider);
          if (node.model) push_str("model", *node.model);
          if (node.system_prompt) push_str("system", *node.system_prompt);
          if (node.temperature)
          {
            chunk_.emit_constant(vm::Value::String("temperature", 11));
            chunk_.emit_constant(vm::Value::Number(*node.temperature));
            field_count++;
          }
          if (node.budget) push_str("budget", *node.budget);
          if (!node.skills.empty())
          {
            std::string joined;
            for (size_t i = 0; i < node.skills.size(); ++i)
            {
              if (i > 0) joined += ",";
              joined += node.skills[i];
            }
            push_str("skills", joined);
          }

          // Inherited DataAgent fields
          if (node.role) push_str("role", *node.role);
          if (node.purpose) push_str("purpose", *node.purpose);
          if (node.autonomy) push_str("autonomy", *node.autonomy);
          if (node.agent_md) push_str("agent_md", *node.agent_md);

          // Migration-specific: source & target
          push_str("source", node.source);
          push_str("target", node.target);
          if (node.staging) push_str("staging", *node.staging);

          // Strategy
          switch (node.strategy)
          {
            case MigrationStrategy::LIFT_AND_SHIFT:  push_str("strategy", "lift_and_shift"); break;
            case MigrationStrategy::RE_PLATFORM:     push_str("strategy", "re_platform"); break;
            case MigrationStrategy::RE_ARCHITECTURE: push_str("strategy", "re_architecture"); break;
          }

          // Complex configs serialized as JSON
          push_str("waves", serialize_wave_config(node.waves));
          push_str("movement", serialize_movement_config(node.movement));
          push_str("schema_translation", serialize_schema_translation_config(node.schema_translation));
          push_str("validation", serialize_validation_config(node.validation));
          push_str("cutover", serialize_cutover_config(node.cutover));
          push_str("self_heal", serialize_self_heal_config(node.self_heal));

          if (node.assessment)
            push_str("assessment", serialize_assessment_config(*node.assessment));

          if (node.governance)
            push_str("governance", serialize_governance_migration_config(*node.governance));

          // Compiler tracking
          agent_types_[node.name] = AgentKind::Migration;

          chunk_.write_op(vm::OpCode::OP_DEFINE_MIGRATION_AGENT);
          chunk_.write_byte(field_count);
        }
        // v0.9.3: Scheduler declaration
        else if constexpr (std::is_same_v<T, SchedulerDecl>)
        {
          uint8_t field_count = 0;

          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };

          push_str("name", node.name);
          switch (node.sched_type)
          {
            case SchedulerType::AIRFLOW:         push_str("type", "airflow"); break;
            case SchedulerType::CONTROLM:        push_str("type", "controlm"); break;
            case SchedulerType::CRON:            push_str("type", "cron"); break;
            case SchedulerType::DATABRICKS:      push_str("type", "databricks"); break;
            case SchedulerType::SNOWFLAKE_TASKS: push_str("type", "snowflake_tasks"); break;
            case SchedulerType::DBT:             push_str("type", "dbt"); break;
            case SchedulerType::GLUE:            push_str("type", "glue"); break;
            case SchedulerType::ADF:             push_str("type", "adf"); break;
            case SchedulerType::INFORMATICA:     push_str("type", "informatica"); break;
            case SchedulerType::LUIGI:           push_str("type", "luigi"); break;
          }
          push_str("connection", node.connection);
          if (!node.credentials.empty()) push_str("credentials", node.credentials);
          if (!node.poll_interval.empty()) push_str("poll_interval", node.poll_interval);
          if (!node.filters.empty())
          {
            std::string joined;
            for (size_t i = 0; i < node.filters.size(); ++i) {
              if (i > 0) joined += ",";
              joined += node.filters[i];
            }
            push_str("filters", joined);
          }
          if (node.timezone) push_str("timezone", *node.timezone);
          if (node.datacenter) push_str("datacenter", *node.datacenter);
          if (node.host) push_str("host", *node.host);

          scheduler_defs_.insert(node.name);

          chunk_.write_op(vm::OpCode::OP_DEFINE_SCHEDULER);
          chunk_.write_byte(field_count);
        }
        // v0.9.3: Audit table declaration
        else if constexpr (std::is_same_v<T, AuditTableDecl>)
        {
          uint8_t field_count = 0;

          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };

          push_str("name", node.name);
          push_str("source", node.source_ref);
          push_str("table", node.table_name);
          push_str("column_map", serialize_audit_column_map(node.column_map));
          if (!node.poll_interval.empty()) push_str("poll_interval", node.poll_interval);
          if (node.lookback_window) push_str("lookback_window", *node.lookback_window);
          if (node.retention_analysis) push_str("retention_analysis", *node.retention_analysis);
          push_str("anomalies", serialize_audit_anomalies(node.anomalies));

          audit_table_defs_.insert(node.name);

          chunk_.write_op(vm::OpCode::OP_DEFINE_AUDIT_TABLE);
          chunk_.write_byte(field_count);
        }
        // v0.9.3: Log source declaration
        else if constexpr (std::is_same_v<T, LogSourceDecl>)
        {
          uint8_t field_count = 0;

          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };

          push_str("name", node.name);
          switch (node.log_type)
          {
            case LogSourceType::SNOWFLAKE: push_str("type", "snowflake"); break;
            case LogSourceType::ORACLE:    push_str("type", "oracle"); break;
            case LogSourceType::POSTGRES:  push_str("type", "postgres"); break;
            case LogSourceType::MYSQL:     push_str("type", "mysql"); break;
            case LogSourceType::SQLSERVER: push_str("type", "sqlserver"); break;
            case LogSourceType::SPARK:     push_str("type", "spark"); break;
            case LogSourceType::REDSHIFT:  push_str("type", "redshift"); break;
            case LogSourceType::BIGQUERY:  push_str("type", "bigquery"); break;
            case LogSourceType::KAFKA:     push_str("type", "kafka"); break;
          }
          push_str("connection", node.connection);
          if (!node.credentials.empty()) push_str("credentials", node.credentials);
          if (!node.views.empty())
          {
            std::string joined;
            for (size_t i = 0; i < node.views.size(); ++i) {
              if (i > 0) joined += ",";
              joined += node.views[i];
            }
            push_str("views", joined);
          }
          if (node.log_file) push_str("log_file", *node.log_file);
          if (node.log_format) push_str("log_format", *node.log_format);
          if (!node.poll_interval.empty()) push_str("poll_interval", node.poll_interval);
          if (node.lookback_window) push_str("lookback_window", *node.lookback_window);
          push_str("alerts", serialize_log_alerts(node.alerts));

          log_source_defs_.insert(node.name);

          chunk_.write_op(vm::OpCode::OP_DEFINE_LOG_SOURCE);
          chunk_.write_byte(field_count);
        }
        // v0.9.3: Platform monitor declaration
        else if constexpr (std::is_same_v<T, PlatformDecl>)
        {
          uint8_t field_count = 0;

          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };

          push_str("name", node.name);
          switch (node.plat_type)
          {
            case PlatformType::SNOWFLAKE:   push_str("type", "snowflake"); break;
            case PlatformType::S3:          push_str("type", "s3"); break;
            case PlatformType::ADLS:        push_str("type", "adls"); break;
            case PlatformType::GCS:         push_str("type", "gcs"); break;
            case PlatformType::HDFS:        push_str("type", "hdfs"); break;
            case PlatformType::REDSHIFT:    push_str("type", "redshift"); break;
            case PlatformType::BIGQUERY:    push_str("type", "bigquery"); break;
            case PlatformType::DATABRICKS:  push_str("type", "databricks"); break;
          }
          push_str("connection", node.connection);
          if (!node.credentials.empty()) push_str("credentials", node.credentials);
          if (node.database) push_str("database", *node.database);
          push_str("health_checks", serialize_health_checks(node.health_checks));
          push_str("finops", serialize_finops(node.finops));

          platform_defs_.insert(node.name);

          chunk_.write_op(vm::OpCode::OP_DEFINE_PLATFORM);
          chunk_.write_byte(field_count);
        }
        // v0.9.3: Incident policy declaration
        else if constexpr (std::is_same_v<T, IncidentPolicyDecl>)
        {
          uint8_t field_count = 0;

          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };

          push_str("name", node.name);
          push_str("severity", serialize_severity_levels(node.severity_levels));
          push_str("auto_heal", serialize_auto_heal(node.auto_heal));

          incident_policy_defs_.insert(node.name);

          chunk_.write_op(vm::OpCode::OP_DEFINE_INCIDENT_POLICY);
          chunk_.write_byte(field_count);
        }
        // v0.9.3: Correlation declaration
        else if constexpr (std::is_same_v<T, CorrelationDecl>)
        {
          uint8_t field_count = 0;

          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };

          push_str("name", node.name);
          push_str("scope", serialize_correlation_scope(node.scope));
          if (!node.time_window.empty()) push_str("time_window", node.time_window);
          push_str("sla", serialize_correlation_sla(node.sla));
          if (!node.dependencies.empty())
          {
            nlohmann::json j;
            for (const auto& [k, v] : node.dependencies) j[k] = v;
            push_str("dependencies", j.dump());
          }

          correlation_defs_.insert(node.name);

          chunk_.write_op(vm::OpCode::OP_DEFINE_CORRELATION);
          chunk_.write_byte(field_count);
        }
        // v0.9.3: DataOps agent declaration
        else if constexpr (std::is_same_v<T, DataOpsAgentDecl>)
        {
          validate_dataops_agent(node);

          uint8_t field_count = 0;

          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };

          auto push_bool = [&](const std::string& n, bool v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::Bool(v));
            field_count++;
          };
          (void)push_bool;

          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) {
              if (i > 0) joined += ",";
              joined += list[i];
            }
            push_str(n, joined);
          };

          // Agent name & type
          push_str("name", node.name);
          push_str("agent_type", "dataops");

          // Common agent fields
          if (node.provider) push_str("provider", *node.provider);
          if (node.model) push_str("model", *node.model);
          if (node.system_prompt) push_str("system", *node.system_prompt);
          if (node.temperature)
          {
            chunk_.emit_constant(vm::Value::String("temperature", 11));
            chunk_.emit_constant(vm::Value::Number(*node.temperature));
            field_count++;
          }
          if (node.budget) push_str("budget", *node.budget);
          if (!node.skills.empty()) push_str_list("skills", node.skills);
          if (!node.guardchains.empty()) push_str_list("guardchains", node.guardchains);
          if (node.endpoint) push_str("endpoint", *node.endpoint);
          if (node.api_key_env) push_str("api_key_env", *node.api_key_env);

          // Optional agent fields
          if (node.agent_md) push_str("agent_md", *node.agent_md);
          if (node.policy) push_str("policy", *node.policy);

          // DataOps-specific references
          if (!node.platforms.empty()) push_str_list("platforms", node.platforms);
          if (!node.schedulers.empty()) push_str_list("schedulers", node.schedulers);
          if (!node.audit_tables.empty()) push_str_list("audit_tables", node.audit_tables);
          if (!node.log_sources.empty()) push_str_list("log_sources", node.log_sources);
          if (!node.correlations.empty()) push_str_list("correlations", node.correlations);
          if (node.incident_policy) push_str("incident_policy", *node.incident_policy);

          // Mode
          switch (node.mode)
          {
            case DataOpsMode::CONTINUOUS: push_str("mode", "continuous"); break;
            case DataOpsMode::SCHEDULED:  push_str("mode", "scheduled"); break;
            case DataOpsMode::ON_DEMAND:  push_str("mode", "on_demand"); break;
          }

          // Reports config
          {
            nlohmann::json j;
            auto serialize_report = [](const DataOpsReportConfig& r) {
              nlohmann::json rj;
              if (r.time) rj["time"] = *r.time;
              if (r.day) rj["day"] = *r.day;
              if (r.frequency) rj["frequency"] = *r.frequency;
              if (r.channel) rj["channel"] = *r.channel;
              return rj;
            };
            if (node.daily_digest.time || node.daily_digest.channel)
              j["daily_digest"] = serialize_report(node.daily_digest);
            if (node.weekly_summary.time || node.weekly_summary.channel || node.weekly_summary.day)
              j["weekly_summary"] = serialize_report(node.weekly_summary);
            if (node.cost_report.time || node.cost_report.channel || node.cost_report.frequency)
              j["cost_report"] = serialize_report(node.cost_report);
            if (!j.empty()) push_str("reports", j.dump());
          }

          // Compiler tracking
          agent_types_[node.name] = AgentKind::DataOps;

          chunk_.write_op(vm::OpCode::OP_DEFINE_DATAOPS_AGENT);
          chunk_.write_byte(field_count);
        }
        // v0.9.1: Mart declaration
        else if constexpr (std::is_same_v<T, MartDecl>)
        {
          uint8_t field_count = 0;

          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };

          // Serialize string lists as comma-separated values to maintain 2-slot-per-field invariant
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) {
              if (i > 0) joined += ",";
              joined += list[i];
            }
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(joined.c_str(), joined.size()));
            field_count++;
          };

          push_str("name", node.name);
          push_str_list("facts", node.facts);
          push_str_list("dimensions", node.dimensions);
          push_str("grain", node.grain);
          push_str_list("measures", node.measures);

          if (!node.scd.empty())
          {
            // Serialize SCD as comma-separated "dim:type" pairs to maintain 2-slot-per-field invariant
            std::string scd_joined;
            for (size_t i = 0; i < node.scd.size(); ++i) {
              if (i > 0) scd_joined += ",";
              scd_joined += node.scd[i].dimension_name + ":" + node.scd[i].scd_type;
            }
            chunk_.emit_constant(vm::Value::String("scd", 3));
            chunk_.emit_constant(vm::Value::String(scd_joined.c_str(), scd_joined.size()));
            field_count++;
          }

          if (!node.conformed.empty())
            push_str_list("conformed", node.conformed);

          if (node.materialization)
            push_str("materialization", *node.materialization);

          mart_defs_.insert(node.name);

          chunk_.write_op(vm::OpCode::OP_DEFINE_MART);
          chunk_.write_byte(field_count);
        }
        // v0.9.1: Semantic declaration
        else if constexpr (std::is_same_v<T, SemanticDecl>)
        {
          uint8_t field_count = 0;

          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };

          push_str("name", node.name);

          // Metrics: serialize as comma-separated "name|sql|description|type" to maintain 2-slot invariant
          {
            std::string metrics_joined;
            for (size_t i = 0; i < node.metrics.size(); ++i) {
              if (i > 0) metrics_joined += ";";
              metrics_joined += node.metrics[i].name + "|" + node.metrics[i].sql + "|"
                + node.metrics[i].description + "|" + node.metrics[i].type;
            }
            chunk_.emit_constant(vm::Value::String("metrics", 7));
            chunk_.emit_constant(vm::Value::String(metrics_joined.c_str(), metrics_joined.size()));
            field_count++;
          }

          // Entities: serialize as comma-separated "name|table|key"
          if (!node.entities.empty())
          {
            std::string entities_joined;
            for (size_t i = 0; i < node.entities.size(); ++i) {
              if (i > 0) entities_joined += ";";
              entities_joined += node.entities[i].name + "|" + node.entities[i].table + "|"
                + node.entities[i].key;
            }
            chunk_.emit_constant(vm::Value::String("entities", 8));
            chunk_.emit_constant(vm::Value::String(entities_joined.c_str(), entities_joined.size()));
            field_count++;
          }

          // Synonyms: serialize as comma-separated "alias:target"
          if (!node.synonyms.empty())
          {
            std::string synonyms_joined;
            bool first = true;
            for (const auto& [k, v] : node.synonyms)
            {
              if (!first) synonyms_joined += ",";
              synonyms_joined += k + ":" + v;
              first = false;
            }
            chunk_.emit_constant(vm::Value::String("synonyms", 8));
            chunk_.emit_constant(vm::Value::String(synonyms_joined.c_str(), synonyms_joined.size()));
            field_count++;
          }

          // Time intelligence
          if (node.time_intelligence)
          {
            const auto& ti = *node.time_intelligence;
            if (ti.fiscal_year_start)
              push_str("fiscal_year_start", *ti.fiscal_year_start);
            if (ti.week_start)
              push_str("week_start", *ti.week_start);
            if (ti.default_timezone)
              push_str("default_timezone", *ti.default_timezone);
          }

          semantic_defs_.insert(node.name);

          chunk_.write_op(vm::OpCode::OP_DEFINE_SEMANTIC);
          chunk_.write_byte(field_count);
        }
        // v0.8 Phase 6: Channel declaration
        else if constexpr (std::is_same_v<T, ChannelDecl>)
        {
          // Push name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // Push config as map
          std::size_t count = 0;
          for (const auto& [key, value] : node.config)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, key)));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, value)));
            ++count;
          }
          emit_build_map(chunk_, count);
          chunk_.write_op(OpCode::OP_DEFINE_CHANNEL);
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
        // v0.6.7: External skill declarations
        else if constexpr (std::is_same_v<T, ExternSkillDecl>)
        {
          // Push name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // Push description
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.description)));
          // Push param_names list
          for (const auto& param : node.params)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, param.name)));
          }
          emit_build_list(chunk_, node.params.size());
          // Push params map (name -> schema)
          for (const auto& param : node.params)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, param.name)));
            emit_json_value(chunk_, param.schema);
          }
          emit_build_map(chunk_, node.params.size());
          // Push binding type as string
          std::string binding_type_str;
          switch (node.binding.type)
          {
            case SkillBindingSpec::Type::kMcp:
              binding_type_str = "mcp";
              break;
            case SkillBindingSpec::Type::kHttp:
              binding_type_str = "http";
              break;
            case SkillBindingSpec::Type::kClaudeBuiltin:
              binding_type_str = "claude_builtin";
              break;
          }
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, binding_type_str)));
          // Push binding config as map
          std::size_t config_count = 0;
          auto emit_config_entry = [&](const std::string& key, const std::string& value) {
            if (!value.empty())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, key)));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(
                  static_cast<uint16_t>(emit_string_constant(chunk_, value)));
              ++config_count;
            }
          };
          switch (node.binding.type)
          {
            case SkillBindingSpec::Type::kMcp:
              emit_config_entry("server", node.binding.mcp_server);
              emit_config_entry("tool", node.binding.mcp_tool);
              break;
            case SkillBindingSpec::Type::kHttp:
              emit_config_entry("method", node.binding.http_method);
              emit_config_entry("url", node.binding.http_url);
              emit_config_entry("body_template", node.binding.http_body_template);
              emit_config_entry("response_path", node.binding.http_response_path);
              if (node.binding.http_timeout_ms != 30000)
              {
                chunk_.write_op(OpCode::OP_CONST);
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, "timeout")));
                chunk_.emit_constant(vm::Value::Number(
                    static_cast<double>(node.binding.http_timeout_ms)));
                ++config_count;
              }
              // Emit headers as a sub-list
              if (!node.binding.http_headers.empty())
              {
                chunk_.write_op(OpCode::OP_CONST);
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, "headers")));
                for (const auto& [hk, hv] : node.binding.http_headers)
                {
                  chunk_.write_op(OpCode::OP_CONST);
                  chunk_.write_short(static_cast<uint16_t>(
                      emit_string_constant(chunk_, hk + ": " + hv)));
                }
                emit_build_list(chunk_, node.binding.http_headers.size());
                ++config_count;
              }
              break;
            case SkillBindingSpec::Type::kClaudeBuiltin:
              emit_config_entry("type", node.binding.claude_tool_type);
              break;
          }
          emit_build_map(chunk_, config_count);
          // v0.6.9 D10: sensitive marker
          chunk_.write_op(node.sensitive ? OpCode::OP_TRUE : OpCode::OP_FALSE);
          chunk_.write_op(OpCode::OP_DEFINE_EXTERN_SKILL);
        }
        else if constexpr (std::is_same_v<T, McpServerDecl>)
        {
          // Push server name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(emit_string_constant(chunk_, node.name)));
          // Push config map
          std::size_t config_count = 0;
          auto emit_entry = [&](const std::string& key, const std::string& value) {
            if (!value.empty())
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(
                  static_cast<uint16_t>(emit_string_constant(chunk_, key)));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(
                  static_cast<uint16_t>(emit_string_constant(chunk_, value)));
              ++config_count;
            }
          };
          emit_entry("command", node.command);
          emit_entry("url", node.url);
          if (!node.args.empty())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, "args")));
            emit_string_list(chunk_, node.args);
            ++config_count;
          }
          if (!node.env.empty())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(
                static_cast<uint16_t>(emit_string_constant(chunk_, "env")));
            std::size_t env_count = 0;
            for (const auto& [k, v] : node.env)
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(
                  static_cast<uint16_t>(emit_string_constant(chunk_, k)));
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(
                  static_cast<uint16_t>(emit_string_constant(chunk_, v)));
              ++env_count;
            }
            emit_build_map(chunk_, env_count);
            ++config_count;
          }
          emit_build_map(chunk_, config_count);
          chunk_.write_op(OpCode::OP_DEFINE_MCP_SERVER);
        }
        else if constexpr (std::is_same_v<T, AdoptStmt>)
        {
          // Push server name
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(
              static_cast<uint16_t>(emit_string_constant(chunk_, node.server_name)));
          // Push filter list (empty = wildcard)
          emit_string_list(chunk_, node.tool_names);
          // Push alias or nil
          if (node.alias_prefix.has_value())
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, *node.alias_prefix)));
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }
          chunk_.write_op(OpCode::OP_ADOPT_MCP_TOOLS);
        }
        // v0.7.0: For-in loop
        else if constexpr (std::is_same_v<T, ForInStmt>)
        {
          // Emit iterable
          emit_expression(*node.iterable);
          chunk_.write_op(OpCode::OP_GET_ITER);

          // Track the iterator as a hidden local so stack slot indices are correct.
          // Use an outer scope for the iterator (persists across iterations).
          begin_scope();
          locals_.push_back(Local{"$iter", scope_depth_});

          // Record loop start for continue
          const auto loop_start = chunk_.code().size();
          loop_starts_.push_back(loop_start);
          break_patches_.emplace_back();
          // Save local count before loop body locals (just the iterator)
          loop_local_counts_.push_back(locals_.size());

          // OP_FOR_ITER with exit placeholder
          chunk_.write_op(OpCode::OP_FOR_ITER);
          const auto exit_jump = chunk_.code().size();
          chunk_.write_short(0);

          // Define loop variable as local in inner scope
          // (OP_FOR_ITER pushes next value onto stack)
          begin_scope();
          locals_.push_back(Local{node.variable, scope_depth_});

          // If second variable exists (for (k,v) destructuring), we need to
          // handle it by pushing map key and value
          if (!node.second_variable.empty())
          {
            locals_.push_back(Local{node.second_variable, scope_depth_});
            // Push a nil placeholder for second var (users use entry.key, entry.value)
            chunk_.write_op(OpCode::OP_NIL);
          }

          // Emit body
          const auto* body_stmt = &node.body->node;
          if (auto* block = std::get_if<BlockStmt>(body_stmt))
          {
            for (const auto& s : block->statements)
            {
              emit_statement(*s);
            }
          }
          else
          {
            emit_statement(*node.body);
          }

          // Pop locals declared in body scope (loop var + any body locals, NOT iterator)
          end_scope();

          // OP_LOOP back to OP_FOR_ITER
          chunk_.write_op(OpCode::OP_LOOP);
          const auto loop_back_index = chunk_.code().size();
          chunk_.write_short(0);
          const auto back_offset = chunk_.code().size() - loop_start;
          if (back_offset > std::numeric_limits<uint16_t>::max())
          {
            throw std::runtime_error("Loop body too large");
          }
          chunk_.patch_short(loop_back_index, static_cast<uint16_t>(back_offset));

          // Patch exit jump (OP_FOR_ITER jumps here when done)
          const auto exit_offset = chunk_.code().size() - exit_jump - 2;
          if (exit_offset > std::numeric_limits<uint16_t>::max())
          {
            throw std::runtime_error("For-in exit jump too large");
          }
          chunk_.patch_short(exit_jump, static_cast<uint16_t>(exit_offset));

          // Pop iterator via end_scope of the outer loop scope
          end_scope();

          // Patch breaks — break target is here (after iterator scope cleaned up)
          for (auto break_offset : break_patches_.back())
          {
            const auto bp_offset = chunk_.code().size() - break_offset - 2;
            chunk_.patch_short(break_offset, static_cast<uint16_t>(bp_offset));
          }
          break_patches_.pop_back();
          loop_starts_.pop_back();
          loop_local_counts_.pop_back();
        }
        else if constexpr (std::is_same_v<T, BreakStmt>)
        {
          if (break_patches_.empty())
          {
            throw std::runtime_error("'break' used outside of loop");
          }
          // Pop all body locals (loop var + anything declared in body)
          const auto base_locals = loop_local_counts_.back();
          for (std::size_t i = locals_.size(); i > base_locals; --i)
          {
            chunk_.write_op(OpCode::OP_POP);
          }
          // Pop the iterator (hidden $iter local in outer loop scope)
          chunk_.write_op(OpCode::OP_POP);
          chunk_.write_op(OpCode::OP_JUMP);
          break_patches_.back().push_back(chunk_.code().size());
          chunk_.write_short(0);
        }
        else if constexpr (std::is_same_v<T, ContinueStmt>)
        {
          if (loop_starts_.empty())
          {
            throw std::runtime_error("'continue' used outside of loop");
          }
          // Pop all locals declared inside the loop scope (but NOT the iterator)
          const auto base_locals = loop_local_counts_.back();
          for (std::size_t i = locals_.size(); i > base_locals; --i)
          {
            chunk_.write_op(OpCode::OP_POP);
          }
          chunk_.write_op(OpCode::OP_LOOP);
          const auto target = chunk_.code().size();
          chunk_.write_short(0);
          const auto offset = chunk_.code().size() - loop_starts_.back();
          if (offset > std::numeric_limits<uint16_t>::max())
          {
            throw std::runtime_error("Continue jump too large");
          }
          chunk_.patch_short(target, static_cast<uint16_t>(offset));
        }
        // v0.7.0: Destructuring let
        else if constexpr (std::is_same_v<T, DestructureLetStmt>)
        {
          emit_expression(*node.initializer);
          const auto& pattern = node.pattern;
          if (pattern.has_rest)
          {
            // Count names before and after rest
            int before = pattern.rest_position;
            int after = static_cast<int>(pattern.names.size()) - before;
            chunk_.write_op(OpCode::OP_UNPACK_REST);
            chunk_.write_short(static_cast<uint16_t>(before));
            chunk_.write_short(static_cast<uint16_t>(after));
            // Stack now has (bottom to top): [name0, name1, ..., rest_list, nameN, ...]
            if (scope_depth_ == 0)
            {
              // OP_DEFINE_GLOBAL takes from stack top, so define in reverse order:
              // after names (reverse), rest, before names (reverse)
              for (int i = static_cast<int>(pattern.names.size()) - 1; i >= before; --i)
              {
                const auto nc = chunk_.add_constant(
                    vm::Value::String(pattern.names[i].c_str(), pattern.names[i].size()));
                chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
                chunk_.write_short(static_cast<uint16_t>(nc));
              }
              // Rest variable
              const auto nc = chunk_.add_constant(
                  vm::Value::String(pattern.rest_name.c_str(), pattern.rest_name.size()));
              chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
              chunk_.write_short(static_cast<uint16_t>(nc));
              // Before names (reverse)
              for (int i = before - 1; i >= 0; --i)
              {
                const auto nc2 = chunk_.add_constant(
                    vm::Value::String(pattern.names[i].c_str(), pattern.names[i].size()));
                chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
                chunk_.write_short(static_cast<uint16_t>(nc2));
              }
            }
            else
            {
              // For locals, just register in forward order (stack slots match)
              for (int i = 0; i < before; ++i)
              {
                locals_.push_back(Local{pattern.names[i], scope_depth_});
              }
              locals_.push_back(Local{pattern.rest_name, scope_depth_});
              for (int i = before; i < static_cast<int>(pattern.names.size()); ++i)
              {
                locals_.push_back(Local{pattern.names[i], scope_depth_});
              }
            }
          }
          else
          {
            chunk_.write_op(OpCode::OP_UNPACK);
            chunk_.write_short(static_cast<uint16_t>(pattern.names.size()));
            if (scope_depth_ == 0)
            {
              // OP_DEFINE_GLOBAL takes from stack top, so define in reverse order
              for (int i = static_cast<int>(pattern.names.size()) - 1; i >= 0; --i)
              {
                const auto nc = chunk_.add_constant(
                    vm::Value::String(pattern.names[i].c_str(), pattern.names[i].size()));
                chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
                chunk_.write_short(static_cast<uint16_t>(nc));
              }
            }
            else
            {
              for (const auto& name : pattern.names)
              {
                locals_.push_back(Local{name, scope_depth_});
              }
            }
          }
        }
        // v0.7.1: Struct declaration
        else if constexpr (std::is_same_v<T, StructDecl>)
        {
          // Push field names onto stack, then OP_DEFINE_STRUCT
          for (const auto& field : node.fields)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, field.name)));
          }
          chunk_.write_op(OpCode::OP_DEFINE_STRUCT);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.name)));
          chunk_.write_byte(static_cast<uint8_t>(node.fields.size()));
          chunk_.write_byte(node.is_mutable ? 1 : 0);

          // v0.7.1 Phase 5: Compile property observer functions
          for (const auto& field : node.fields)
          {
            if (field.will_set_body)
            {
              // Compile willSet as fn TypeName.$willSet_fieldName(self, newValue)
              const auto* block_ptr = std::get_if<BlockStmt>(&field.will_set_body->node);
              if (block_ptr)
              {
                auto fn_value = compile_block_function(
                    node.name + ".$willSet_" + field.name,
                    {"self", field.will_set_param}, *block_ptr);
                const auto fn_const = chunk_.add_constant(std::move(fn_value));
                chunk_.write_op(OpCode::OP_CONST);
                chunk_.write_short(static_cast<uint16_t>(fn_const));
                chunk_.write_op(OpCode::OP_SET_FIELD_OBSERVER);
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, node.name)));
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, field.name)));
                chunk_.write_byte(0);  // kind = willSet
              }
            }
            if (field.did_set_body)
            {
              const auto* block_ptr = std::get_if<BlockStmt>(&field.did_set_body->node);
              if (block_ptr)
              {
                auto fn_value = compile_block_function(
                    node.name + ".$didSet_" + field.name,
                    {"self"}, *block_ptr);
                const auto fn_const = chunk_.add_constant(std::move(fn_value));
                chunk_.write_op(OpCode::OP_CONST);
                chunk_.write_short(static_cast<uint16_t>(fn_const));
                chunk_.write_op(OpCode::OP_SET_FIELD_OBSERVER);
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, node.name)));
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, field.name)));
                chunk_.write_byte(1);  // kind = didSet
              }
            }
            if (field.guard_expr)
            {
              // Compile guard as fn TypeName.$guard_fieldName(self, value) { return expr; }
              // We need to wrap the expression in a function
              auto guard_body = std::make_unique<Statement>();
              guard_body->node = BlockStmt{{[&]() {
                auto ret = std::make_unique<Statement>();
                ret->node = ReturnStmt{std::move(const_cast<FieldDef&>(field).guard_expr)};
                std::vector<StmtPtr> stmts;
                stmts.push_back(std::move(ret));
                return stmts;
              }()}};

              const auto* block_ptr = std::get_if<BlockStmt>(&guard_body->node);
              if (block_ptr)
              {
                auto fn_value = compile_block_function(
                    node.name + ".$guard_" + field.name,
                    {"self", field.name}, *block_ptr);
                const auto fn_const = chunk_.add_constant(std::move(fn_value));
                chunk_.write_op(OpCode::OP_CONST);
                chunk_.write_short(static_cast<uint16_t>(fn_const));
                chunk_.write_op(OpCode::OP_SET_FIELD_OBSERVER);
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, node.name)));
                chunk_.write_short(static_cast<uint16_t>(
                    emit_string_constant(chunk_, field.name)));
                chunk_.write_byte(2);  // kind = guard
              }
            }
          }
        }
        // v0.7.1: Impl block
        else if constexpr (std::is_same_v<T, ImplBlock>)
        {
          for (const auto& method : node.methods)
          {
            // Compile method as a function.
            // For instance methods, prepend "self" as first parameter.
            std::vector<std::string> full_params;
            if (!method.is_static)
            {
              full_params.push_back("self");
            }
            for (const auto& p : method.parameters)
            {
              full_params.push_back(p);
            }

            const auto* block_ptr = std::get_if<BlockStmt>(&method.body->node);
            if (!block_ptr)
            {
              throw std::runtime_error("Method body must be a block");
            }
            auto fn_value = compile_block_function(
                node.type_name + "." + method.name, full_params, *block_ptr);
            const auto fn_const = chunk_.add_constant(std::move(fn_value));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(fn_const));

            // If this is a trait impl, use OP_IMPL_TRAIT
            if (node.trait_name.has_value())
            {
              // v0.8: Validate trait compatibility with agent type
              if (is_neamclaw_trait(*node.trait_name))
              {
                validate_neamclaw_trait_compat(*node.trait_name, node.type_name);
              }
              chunk_.write_op(OpCode::OP_IMPL_TRAIT);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, node.trait_name.value())));
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, node.type_name)));
              chunk_.write_byte(method.is_static ? 1 : 0);
            }
            else
            {
              chunk_.write_op(OpCode::OP_IMPL_METHOD);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, node.type_name)));
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, method.name)));
              chunk_.write_byte(method.is_static ? 1 : 0);
            }
          }
        }
        // v0.7.1 Phase 2: Trait declaration
        else if constexpr (std::is_same_v<T, TraitDecl>)
        {
          // Compile default methods as functions first
          for (const auto& method : node.default_methods)
          {
            std::vector<std::string> full_params;
            if (!method.is_static) full_params.push_back("self");
            for (const auto& p : method.parameters) full_params.push_back(p);
            const auto* block_ptr = std::get_if<BlockStmt>(&method.body->node);
            if (!block_ptr) throw std::runtime_error("Default method body must be a block");
            auto fn_value = compile_block_function(
                node.name + "." + method.name, full_params, *block_ptr);
            chunk_.emit_constant(std::move(fn_value));
          }
          // Push required method names
          for (const auto& req : node.required_methods)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, req.name)));
          }
          chunk_.write_op(OpCode::OP_DEFINE_TRAIT);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.name)));
          chunk_.write_byte(static_cast<uint8_t>(node.required_methods.size()));
          chunk_.write_byte(static_cast<uint8_t>(node.default_methods.size()));
        }
        // v0.7.1 Phase 2: Sealed type declaration
        else if constexpr (std::is_same_v<T, SealedDecl>)
        {
          // Push variant definitions: for each variant, push name + field names
          for (const auto& variant : node.variants)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, variant.name)));
            for (const auto& field : variant.fields)
            {
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, field.name)));
            }
            chunk_.emit_constant(vm::Value::Number(static_cast<double>(variant.fields.size())));
          }
          chunk_.write_op(OpCode::OP_DEFINE_SEALED);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.name)));
          chunk_.write_byte(static_cast<uint8_t>(node.variants.size()));
        }
        // v0.7.1 Phase 3: Extend block — same as impl block, adds methods to existing type
        else if constexpr (std::is_same_v<T, ExtendBlock>)
        {
          for (const auto& method : node.methods)
          {
            std::vector<std::string> full_params;
            if (!method.is_static) full_params.push_back("self");
            for (const auto& p : method.parameters) full_params.push_back(p);
            const auto* block_ptr = std::get_if<BlockStmt>(&method.body->node);
            if (!block_ptr) throw std::runtime_error("Extend method body must be a block");
            auto fn_value = compile_block_function(
                node.target + "." + method.name, full_params, *block_ptr);
            const auto fn_const = chunk_.add_constant(std::move(fn_value));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(fn_const));
            chunk_.write_op(OpCode::OP_IMPL_METHOD);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, node.target)));
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, method.name)));
            chunk_.write_byte(method.is_static ? 1 : 0);
          }
        }
        // v0.7.1 Phase 4: Agentic patterns — compiled to function definitions
        else if constexpr (std::is_same_v<T, PipelineDecl>)
        {
          // Generate a function: fn PipelineName_run(input) { ... }
          // For now, register the pipeline name as a global with step list
          for (const auto& step : node.step_agents)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, step)));
          }
          chunk_.write_op(OpCode::OP_BUILD_LIST);
          chunk_.write_short(static_cast<uint16_t>(node.step_agents.size()));
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.name + "_steps")));
        }
        else if constexpr (std::is_same_v<T, DispatchDecl>)
        {
          // Store dispatch config as a global map
          for (const auto& [route_key, agent_name] : node.routes)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, route_key)));
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, agent_name)));
          }
          chunk_.write_op(OpCode::OP_BUILD_MAP);
          chunk_.write_short(static_cast<uint16_t>(node.routes.size()));
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.name + "_routes")));
        }
        else if constexpr (std::is_same_v<T, ParallelDecl>)
        {
          for (const auto& agent : node.agents)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, agent)));
          }
          chunk_.write_op(OpCode::OP_BUILD_LIST);
          chunk_.write_short(static_cast<uint16_t>(node.agents.size()));
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.name + "_agents")));
        }
        else if constexpr (std::is_same_v<T, LoopPatternDecl>)
        {
          // Store loop pattern config as globals
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.generator_agent)));
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.name + "_generator")));
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.critic_agent)));
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.name + "_critic")));
          chunk_.emit_constant(vm::Value::Number(static_cast<double>(node.max_iterations)));
          chunk_.write_op(OpCode::OP_DEFINE_GLOBAL);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.name + "_max_iterations")));
        }
        // ═══ v0.9.4 Governance Agent declarations ═══
        else if constexpr (std::is_same_v<T, GovCatalogSourceDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          push_str("name", node.name);
          // Map enum to string
          static const char* cst_names[] = {"snowflake","oracle","postgres","mysql","sqlserver","redshift","bigquery","databricks","s3","adls","gcs","hdfs","collibra","atlas","alation","purview","informatica","atlan"};
          push_str("type", cst_names[static_cast<int>(node.source_type)]);
          push_str("connection", node.connection);
          if (!node.credentials.empty()) push_str("credentials", node.credentials);
          if (!node.databases.empty()) push_str_list("databases", node.databases);
          if (!node.prefixes.empty()) push_str_list("prefixes", node.prefixes);
          if (!node.scan_interval.empty()) push_str("scan_interval", node.scan_interval);
          push_str("include_views", node.include_views ? "true" : "false");
          push_str("include_stages", node.include_stages ? "true" : "false");
          push_str("detect_formats", node.detect_formats ? "true" : "false");
          if (!node.exclude_patterns.empty()) push_str_list("exclude_patterns", node.exclude_patterns);
          static const char* sm_names[] = {"push","pull","bidirectional"};
          push_str("sync_mode", sm_names[static_cast<int>(node.sync_mode)]);
          if (!node.sync_interval.empty()) push_str("sync_interval", node.sync_interval);
          static const char* cr_names[] = {"agent_wins","external_wins","manual"};
          push_str("conflict_resolution", cr_names[static_cast<int>(node.conflict_resolution)]);
          catalog_source_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_CATALOG_SOURCE);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, GovCatalogDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          push_str("name", node.name);
          if (!node.sources.empty()) push_str_list("sources", node.sources);
          if (node.auto_document.enabled) {
            nlohmann::json j;
            j["enabled"] = node.auto_document.enabled;
            j["provider"] = node.auto_document.provider;
            j["model"] = node.auto_document.model;
            j["require_review"] = node.auto_document.require_review;
            j["review_channel"] = node.auto_document.review_channel;
            push_str("auto_document", j.dump());
          }
          if (!node.staleness_threshold.empty()) push_str("staleness_threshold", node.staleness_threshold);
          push_str("shadow_dataset_detection", node.shadow_dataset_detection ? "true" : "false");
          if (node.ownership.auto_assign || node.ownership.require_owner) {
            nlohmann::json j;
            j["auto_assign"] = node.ownership.auto_assign;
            j["default_domain"] = node.ownership.default_domain;
            j["require_owner"] = node.ownership.require_owner;
            push_str("ownership", j.dump());
          }
          gov_catalog_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_GOV_CATALOG);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, GlossaryDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          push_str("name", node.name);
          if (!node.domains.empty()) push_str_list("domains", node.domains);
          if (node.auto_suggest.enabled) {
            nlohmann::json j;
            j["enabled"] = node.auto_suggest.enabled;
            j["provider"] = node.auto_suggest.provider;
            j["model"] = node.auto_suggest.model;
            j["sources"] = node.auto_suggest.sources;
            j["require_approval"] = node.auto_suggest.require_approval;
            j["approval_channel"] = node.auto_suggest.approval_channel;
            push_str("auto_suggest", j.dump());
          }
          if (node.synonym_detection.enabled) {
            nlohmann::json j;
            j["enabled"] = node.synonym_detection.enabled;
            j["confidence_threshold"] = node.synonym_detection.confidence_threshold;
            j["cross_database"] = node.synonym_detection.cross_database;
            push_str("synonym_detection", j.dump());
          }
          if (!node.terms_json.empty()) push_str("terms", node.terms_json);
          if (!node.external_sync_json.empty()) push_str("external_sync", node.external_sync_json);
          glossary_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_GLOSSARY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, ClassificationPolicyDecl>)
        {
          validate_classification_policy(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          // Serialize levels as JSON blob
          nlohmann::json levels_json;
          for (const auto& [lname, lvl] : node.levels) {
            levels_json[lname] = {{"level", lvl.level}, {"controls", lvl.controls}, {"retention_max", lvl.retention_max}, {"cross_border", lvl.cross_border}};
          }
          push_str("levels", levels_json.dump());
          push_str("auto_classify", serialize_auto_classify(node.auto_classify));
          nlohmann::json prop_json;
          prop_json["lineage_based"] = node.propagation.lineage_based;
          prop_json["inheritance"] = node.propagation.inheritance;
          push_str("propagation", prop_json.dump());
          classification_policy_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_CLASSIFICATION_POLICY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, AccessPolicyDecl>)
        {
          validate_access_policy(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          static const char* am_names[] = {"rbac","abac","hybrid_rbac_abac"};
          push_str("model", am_names[static_cast<int>(node.model)]);
          if (!node.roles.empty()) {
            nlohmann::json roles_json;
            for (const auto& [rname, role] : node.roles) {
              roles_json[rname] = {{"description", role.description}, {"permissions", role.permissions}, {"databases", role.databases}};
              if (!role.masking_json.empty()) roles_json[rname]["masking"] = nlohmann::json::parse(role.masking_json);
              if (!role.row_level_security_json.empty()) roles_json[rname]["row_level_security"] = nlohmann::json::parse(role.row_level_security_json);
              if (!role.restrictions_json.empty()) roles_json[rname]["restrictions"] = nlohmann::json::parse(role.restrictions_json);
            }
            push_str("roles", roles_json.dump());
          }
          if (!node.attributes_json.empty()) push_str("attributes", node.attributes_json);
          if (!node.access_review.mode.empty()) {
            nlohmann::json ar;
            ar["mode"] = node.access_review.mode;
            ar["unused_access_threshold"] = node.access_review.unused_access_threshold;
            ar["excessive_access_detection"] = node.access_review.excessive_access_detection;
            ar["auto_revoke_unused"] = node.access_review.auto_revoke_unused;
            push_str("access_review", ar.dump());
          }
          access_policy_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_ACCESS_POLICY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, QualityPolicyDecl>)
        {
          validate_quality_policy(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (node.profiling.enabled) {
            nlohmann::json j;
            j["enabled"] = node.profiling.enabled;
            j["scan_interval"] = node.profiling.scan_interval;
            j["sample_size"] = node.profiling.sample_size;
            j["targets"] = node.profiling.targets;
            push_str("profiling", j.dump());
          }
          if (!node.rules_json.empty()) push_str("rules", node.rules_json);
          if (node.scoring.enabled) {
            nlohmann::json j;
            j["enabled"] = node.scoring.enabled;
            j["accuracy_weight"] = node.scoring.accuracy_weight;
            j["completeness_weight"] = node.scoring.completeness_weight;
            j["consistency_weight"] = node.scoring.consistency_weight;
            j["timeliness_weight"] = node.scoring.timeliness_weight;
            j["validity_weight"] = node.scoring.validity_weight;
            j["uniqueness_weight"] = node.scoring.uniqueness_weight;
            j["minimum_score"] = node.scoring.minimum_score;
            push_str("scoring", j.dump());
          }
          quality_policy_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_QUALITY_POLICY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, LineagePolicyDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (node.auto_discover.enabled) {
            nlohmann::json j;
            j["enabled"] = node.auto_discover.enabled;
            j["sources"] = node.auto_discover.sources;
            j["methods"] = node.auto_discover.methods;
            j["scan_interval"] = node.auto_discover.scan_interval;
            static const char* ld_names[] = {"table","column","transformation"};
            j["depth"] = ld_names[static_cast<int>(node.auto_discover.depth)];
            push_str("auto_discover", j.dump());
          }
          if (node.impact_analysis.enabled) {
            nlohmann::json j;
            j["enabled"] = true;
            j["downstream_depth"] = node.impact_analysis.downstream_depth;
            j["include_reports"] = node.impact_analysis.include_reports;
            j["include_apis"] = node.impact_analysis.include_apis;
            j["include_ml_models"] = node.impact_analysis.include_ml_models;
            push_str("impact_analysis", j.dump());
          }
          if (node.tag_propagation.enabled) {
            nlohmann::json j;
            j["enabled"] = true;
            j["direction"] = node.tag_propagation.direction;
            j["inherit_sensitivity"] = node.tag_propagation.inherit_sensitivity;
            j["inherit_pii_tags"] = node.tag_propagation.inherit_pii_tags;
            push_str("tag_propagation", j.dump());
          }
          if (!node.external_sync_json.empty()) push_str("external_sync", node.external_sync_json);
          lineage_policy_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_LINEAGE_POLICY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, CompliancePolicyDecl>)
        {
          validate_compliance_policy(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          push_str("name", node.name);
          if (!node.regulations.empty()) push_str_list("regulations", node.regulations);
          if (!node.gdpr_json.empty()) push_str("gdpr", node.gdpr_json);
          if (!node.ccpa_json.empty()) push_str("ccpa", node.ccpa_json);
          if (!node.hipaa_json.empty()) push_str("hipaa", node.hipaa_json);
          if (!node.bcbs_239_json.empty()) push_str("bcbs_239", node.bcbs_239_json);
          if (!node.monitoring.scan_interval.empty()) {
            nlohmann::json j;
            j["scan_interval"] = node.monitoring.scan_interval;
            j["scoring"] = node.monitoring.scoring;
            j["alert_on_non_compliance"] = node.monitoring.alert_on_non_compliance;
            push_str("monitoring", j.dump());
          }
          compliance_policy_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_COMPLIANCE_POLICY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, LifecyclePolicyDecl>)
        {
          validate_lifecycle_policy(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.retention.empty()) {
            nlohmann::json j;
            for (const auto& [lname, rule] : node.retention) {
              j[lname] = {{"max_retention", rule.max_retention}, {"min_retention", rule.min_retention}, {"action_on_expiry", rule.action_on_expiry}, {"archive_tier", rule.archive_tier}, {"requires_approval", rule.requires_approval}};
            }
            push_str("retention", j.dump());
          }
          if (!node.regulatory_retention_json.empty()) push_str("regulatory_retention", node.regulatory_retention_json);
          if (node.tiering.enabled) {
            nlohmann::json j;
            j["enabled"] = true;
            j["hot_to_warm"] = node.tiering.hot_to_warm;
            j["warm_to_cold"] = node.tiering.warm_to_cold;
            j["cold_to_archive"] = node.tiering.cold_to_archive;
            push_str("tiering", j.dump());
          }
          if (node.legal_hold.enabled) {
            nlohmann::json j;
            j["enabled"] = true;
            j["hold_overrides_retention"] = node.legal_hold.hold_overrides_retention;
            j["notification_channel"] = node.legal_hold.notification_channel;
            push_str("legal_hold", j.dump());
          }
          lifecycle_policy_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_LIFECYCLE_POLICY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, DataProductDecl>)
        {
          validate_data_product(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.domain.empty()) push_str("domain", node.domain);
          if (!node.owner.empty()) push_str("owner", node.owner);
          if (!node.description.empty()) push_str("description", node.description);
          if (!node.contract.schema_version.empty()) {
            nlohmann::json j;
            j["schema_version"] = node.contract.schema_version;
            j["sla"] = {{"freshness", node.contract.sla.freshness}, {"availability", node.contract.sla.availability}, {"quality_score", node.contract.sla.quality_score}};
            j["breaking_change_policy"] = node.contract.breaking_change_policy;
            j["consumers"] = node.contract.consumers;
            push_str("contract", j.dump());
          }
          if (!node.quality_json.empty()) push_str("quality", node.quality_json);
          if (!node.access_json.empty()) push_str("access", node.access_json);
          data_product_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DATA_PRODUCT);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, ContractPolicyDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          push_str("schema_validation_on_deploy", node.schema_validation_on_deploy ? "true" : "false");
          push_str("breaking_change_detection", node.breaking_change_detection ? "true" : "false");
          push_str("notify_consumers", node.notify_consumers ? "true" : "false");
          if (!node.freshness_check_interval.empty()) push_str("freshness_check_interval", node.freshness_check_interval);
          if (!node.versioning_strategy.empty()) push_str("versioning_strategy", node.versioning_strategy);
          contract_policy_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_CONTRACT_POLICY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, MasterDataDecl>)
        {
          validate_master_data(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          push_str("name", node.name);
          if (!node.entity.empty()) push_str("entity", node.entity);
          if (!node.golden_source.empty()) push_str("golden_source", node.golden_source);
          if (!node.contributing_sources.empty()) push_str_list("contributing_sources", node.contributing_sources);
          nlohmann::json mj;
          mj["strategy"] = node.matching.strategy;
          mj["fields"] = node.matching.fields;
          mj["confidence_threshold"] = node.matching.confidence_threshold;
          mj["manual_review_threshold"] = node.matching.manual_review_threshold;
          push_str("matching", mj.dump());
          if (!node.survivorship_json.empty()) push_str("survivorship", node.survivorship_json);
          if (!node.quality_json.empty()) push_str("quality", node.quality_json);
          master_data_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_MASTER_DATA);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, GovExternalToolDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          push_str("type", node.tool_type);
          push_str("connection", node.connection);
          if (!node.credentials.empty()) push_str("credentials", node.credentials);
          if (!node.capabilities_json.empty()) push_str("capabilities", node.capabilities_json);
          if (!node.sync_interval.empty()) push_str("sync_interval", node.sync_interval);
          static const char* cr_names[] = {"agent_wins","external_wins","manual"};
          push_str("conflict_resolution", cr_names[static_cast<int>(node.conflict_resolution)]);
          external_tool_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_EXTERNAL_TOOL);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, GovernanceAgentDecl>)
        {
          validate_governance_agent(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          push_str("name", node.name);
          if (node.provider) push_str("provider", *node.provider);
          if (node.model) push_str("model", *node.model);
          if (node.endpoint) push_str("endpoint", *node.endpoint);
          if (node.api_key_env) push_str("api_key_env", *node.api_key_env);
          if (node.temperature) push_str("temperature", std::to_string(*node.temperature));
          if (node.system_prompt) push_str("system", *node.system_prompt);
          if (node.budget) push_str("budget", *node.budget);
          // Pillar refs
          if (node.catalog) push_str("catalog", *node.catalog);
          if (node.glossary) push_str("glossary", *node.glossary);
          if (node.classification) push_str("classification", *node.classification);
          if (node.access_control) push_str("access_control", *node.access_control);
          if (node.quality) push_str("quality", *node.quality);
          if (node.lineage) push_str("lineage", *node.lineage);
          if (node.compliance) push_str("compliance", *node.compliance);
          if (node.lifecycle) push_str("lifecycle", *node.lifecycle);
          // Ref lists
          if (!node.external_tools.empty()) push_str_list("external_tools", node.external_tools);
          if (!node.coordinates_with.empty()) push_str_list("coordinates_with", node.coordinates_with);
          if (!node.skills.empty()) push_str_list("skills", node.skills);
          if (!node.guardchains.empty()) push_str_list("guardchains", node.guardchains);
          // Reports
          if (!node.reports.governance_scorecard_json.empty())
            push_str("reports", node.reports.governance_scorecard_json);
          if (node.policy) push_str("policy", *node.policy);
          if (node.agent_md) push_str("agent_md", *node.agent_md);
          // Tracking
          governance_agent_defs_.insert(node.name);
          agent_types_[node.name] = AgentKind::Governance;
          chunk_.write_op(vm::OpCode::OP_DEFINE_GOVERNANCE_AGENT);
          chunk_.write_byte(field_count);
        }
        // ═══════════════════════════════════════════════════
        // v0.9.5 Modeling Agent emit handlers
        // ═══════════════════════════════════════════════════
        else if constexpr (std::is_same_v<T, SchemaSourceDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.type_str.empty()) push_str("type", node.type_str);
          if (!node.connection.empty()) push_str("connection", node.connection);
          if (!node.credentials.empty()) push_str("credentials", node.credentials);
          if (!node.scan_interval.empty()) push_str("scan_interval", node.scan_interval);
          if (!node.path.empty()) push_str("path", node.path);
          if (!node.format.empty()) push_str("format", node.format);
          if (!node.model_type.empty()) push_str("model_type", node.model_type);
          if (!node.sync_direction.empty()) push_str("sync_direction", node.sync_direction);
          if (!node.api_connection.empty()) push_str("api_connection", node.api_connection);
          if (!node.dialect.empty()) push_str("dialect", node.dialect);
          if (!node.project_path.empty()) push_str("project_path", node.project_path);
          if (!node.manifest_path.empty()) push_str("manifest_path", node.manifest_path);
          if (!node.databases.empty()) {
            std::string joined; for (size_t i = 0; i < node.databases.size(); ++i) { if (i) joined += ","; joined += node.databases[i]; }
            push_str("databases", joined);
          }
          if (!node.prefixes.empty()) {
            std::string joined; for (size_t i = 0; i < node.prefixes.size(); ++i) { if (i) joined += ","; joined += node.prefixes[i]; }
            push_str("prefixes", joined);
          }
          if (!node.infer_relationships_json.empty()) push_str("infer_relationships", node.infer_relationships_json);
          if (!node.schema_evolution_json.empty()) push_str("schema_evolution", node.schema_evolution_json);
          schema_source_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_SCHEMA_SOURCE);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, ERModelDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.version.empty()) push_str("version", node.version);
          if (!node.source.empty()) push_str("source", node.source);
          if (!node.levels.empty()) {
            std::string joined; for (size_t i = 0; i < node.levels.size(); ++i) { if (i) joined += ","; joined += node.levels[i]; }
            push_str("levels", joined);
          }
          if (!node.notation_json.empty()) push_str("notation", node.notation_json);
          if (!node.domains_json.empty()) push_str("domains", node.domains_json);
          if (!node.relationship_inference_json.empty()) push_str("relationship_inference", node.relationship_inference_json);
          if (!node.sync_json.empty()) push_str("sync", node.sync_json);
          er_model_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_ER_MODEL);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, ModelingEntityDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.domain.empty()) push_str("domain", node.domain);
          if (!node.attributes_json.empty()) push_str("attributes", node.attributes_json);
          if (!node.relationships_json.empty()) push_str("relationships", node.relationships_json);
          if (!node.glossary_term.empty()) push_str("glossary_term", node.glossary_term);
          if (!node.owner.empty()) push_str("owner", node.owner);
          if (!node.description.empty()) push_str("description", node.description);
          modeling_entity_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_ENTITY_MODEL);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, DimensionalModelDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.methodology.empty()) push_str("methodology", node.methodology);
          if (!node.source.empty()) push_str("source", node.source);
          if (!node.facts_json.empty()) push_str("facts", node.facts_json);
          if (!node.dimensions_json.empty()) push_str("dimensions", node.dimensions_json);
          if (!node.conformed.empty()) {
            std::string joined; for (size_t i = 0; i < node.conformed.size(); ++i) { if (i) joined += ","; joined += node.conformed[i]; }
            push_str("conformed", joined);
          }
          if (!node.target_platform.empty()) push_str("target_platform", node.target_platform);
          if (!node.target_schema.empty()) push_str("target_schema", node.target_schema);
          if (!node.output_json.empty()) push_str("output", node.output_json);
          dimensional_model_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIMENSIONAL_MODEL);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, DataMartDecl_v095>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.dimensional_model.empty()) push_str("dimensional_model", node.dimensional_model);
          if (!node.purpose.empty()) push_str("purpose", node.purpose);
          if (!node.owner.empty()) push_str("owner", node.owner);
          if (!node.facts.empty()) {
            std::string joined; for (size_t i = 0; i < node.facts.size(); ++i) { if (i) joined += ","; joined += node.facts[i]; }
            push_str("facts", joined);
          }
          if (!node.dimensions.empty()) {
            std::string joined; for (size_t i = 0; i < node.dimensions.size(); ++i) { if (i) joined += ","; joined += node.dimensions[i]; }
            push_str("dimensions", joined);
          }
          if (!node.materialization_json.empty()) push_str("materialization", node.materialization_json);
          if (!node.quality_json.empty()) push_str("quality", node.quality_json);
          datamart_v095_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DATAMART_V095);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, NormalizationAnalysisDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.scope_json.empty()) push_str("scope", node.scope_json);
          if (!node.target_nf.empty()) push_str("target_nf", node.target_nf);
          if (!node.fd_discovery_json.empty()) push_str("fd_discovery", node.fd_discovery_json);
          if (!node.report_json.empty()) push_str("report", node.report_json);
          if (!node.governance.empty()) push_str("governance", node.governance);
          if (!node.on_violation.empty()) push_str("on_violation", node.on_violation);
          norm_analysis_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_NORM_ANALYSIS);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, AmendmentConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.monitor_json.empty()) push_str("monitor", node.monitor_json);
          if (!node.change_types_json.empty()) push_str("change_types", node.change_types_json);
          if (!node.impact_scope_json.empty()) push_str("impact_scope", node.impact_scope_json);
          if (!node.approval_json.empty()) push_str("approval", node.approval_json);
          if (!node.document_json.empty()) push_str("document", node.document_json);
          amendment_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_AMENDMENT_CONFIG);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, AmendmentDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.type_str.empty()) push_str("type", node.type_str);
          if (!node.description.empty()) push_str("description", node.description);
          if (!node.changes_json.empty()) push_str("changes", node.changes_json);
          if (node.auto_analyze) push_str("auto_analyze", "true");
          if (!node.require_approval) push_str("require_approval", "false");
          amendment_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_AMENDMENT);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, DataProfileDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.sources_json.empty()) push_str("sources", node.sources_json);
          if (!node.profiling_json.empty()) push_str("profiling", node.profiling_json);
          if (!node.output_json.empty()) push_str("output", node.output_json);
          data_profile_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DATA_PROFILE);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, ModelingToolDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.type_str.empty()) push_str("type", node.type_str);
          if (!node.path.empty()) push_str("path", node.path);
          if (!node.api_url.empty()) push_str("api_url", node.api_url);
          if (!node.credentials.empty()) push_str("credentials", node.credentials);
          if (!node.repository.empty()) push_str("repository", node.repository);
          if (!node.submodels.empty()) {
            std::string joined; for (size_t i = 0; i < node.submodels.size(); ++i) { if (i) joined += ","; joined += node.submodels[i]; }
            push_str("submodels", joined);
          }
          if (!node.sync_json.empty()) push_str("sync", node.sync_json);
          if (!node.mapping_json.empty()) push_str("mapping", node.mapping_json);
          if (!node.on_conflict_json.empty()) push_str("on_conflict", node.on_conflict_json);
          modeling_tool_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_MODELING_TOOL);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, ModelingAgentDecl>)
        {
          validate_modeling_agent(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          push_str("name", node.name);
          if (node.provider) push_str("provider", *node.provider);
          if (node.model) push_str("model", *node.model);
          if (node.endpoint) push_str("endpoint", *node.endpoint);
          if (node.api_key_env) push_str("api_key_env", *node.api_key_env);
          if (node.budget) push_str("budget", *node.budget);
          if (!node.sources.empty()) push_str_list("sources", node.sources);
          if (node.catalog) push_str("catalog", *node.catalog);
          if (node.governance) push_str("governance", *node.governance);
          if (!node.modeling_tools.empty()) push_str_list("modeling_tools", node.modeling_tools);
          if (!node.capabilities_json.empty()) push_str("capabilities", node.capabilities_json);
          if (!node.coordinates_with.empty()) push_str_list("coordinates_with", node.coordinates_with);
          if (node.enrich_from_governance) push_str("enrich_from_governance", "true");
          if (node.role) push_str("role", *node.role);
          if (node.purpose) push_str("purpose", *node.purpose);
          modeling_agent_defs_.insert(node.name);
          agent_types_[node.name] = AgentKind::Modeling;
          chunk_.write_op(vm::OpCode::OP_DEFINE_MODELING_AGENT);
          chunk_.write_byte(field_count);
        }
        // ═══════════════════════════════════════════════════════════════
        // v0.9.6 Analyst Agent emit handlers
        // ═══════════════════════════════════════════════════════════════
        else if constexpr (std::is_same_v<T, SQLConnectionDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.platform_str.empty()) push_str("platform", node.platform_str);
          if (!node.connection.empty()) push_str("connection", node.connection);
          if (!node.credentials.empty()) push_str("credentials", node.credentials);
          if (!node.warehouse.empty()) push_str("warehouse", node.warehouse);
          if (!node.database.empty()) push_str("database", node.database);
          if (!node.schema.empty()) push_str("schema", node.schema);
          if (!node.project.empty()) push_str("project", node.project);
          if (!node.dataset.empty()) push_str("dataset", node.dataset);
          if (!node.catalog.empty()) push_str("catalog", node.catalog);
          if (!node.cluster.empty()) push_str("cluster", node.cluster);
          if (node.timeout != 300) push_str("timeout", std::to_string(node.timeout));
          if (node.max_rows != 10000) push_str("max_rows", std::to_string(node.max_rows));
          if (node.cost_limit > 0.0) push_str("cost_limit", std::to_string(node.cost_limit));
          if (!node.queue.empty()) push_str("queue", node.queue);
          if (!node.prefer_materialized_views) push_str("prefer_materialized_views", "false");
          if (!node.use_result_cache) push_str("use_result_cache", "false");
          if (!node.partition_pruning) push_str("partition_pruning", "false");
          if (!node.schema_source.empty()) push_str("schema_source", node.schema_source);
          if (!node.semantic_layer.empty()) push_str("semantic_layer", node.semantic_layer);
          sql_connection_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_SQL_CONNECTION);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, DomainContextDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          push_str("name", node.name);
          if (!node.models.empty()) push_str_list("models", node.models);
          if (!node.dimensional_models.empty()) push_str_list("dimensional_models", node.dimensional_models);
          if (!node.marts.empty()) push_str_list("marts", node.marts);
          if (!node.schema_sources.empty()) push_str_list("schema_sources", node.schema_sources);
          if (!node.glossary.empty()) push_str("glossary", node.glossary);
          if (!node.data_products.empty()) push_str_list("data_products", node.data_products);
          if (!node.classification.empty()) push_str("classification", node.classification);
          if (!node.access_policy.empty()) push_str("access_policy", node.access_policy);
          if (!node.semantic_layers.empty()) push_str_list("semantic_layers", node.semantic_layers);
          if (node.query_history) push_str("query_history", "true");
          if (node.feedback_loop) push_str("feedback_loop", "true");
          domain_context_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DOMAIN_CONTEXT);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, QueryTemplateDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.description.empty()) push_str("description", node.description);
          if (!node.category.empty()) push_str("category", node.category);
          if (!node.params_json.empty()) push_str("params", node.params_json);
          if (!node.sql.empty()) push_str("sql", node.sql);
          if (!node.default_format.empty()) push_str("default_format", node.default_format);
          if (!node.chart_json.empty()) push_str("chart", node.chart_json);
          if (!node.classification.empty()) push_str("classification", node.classification);
          if (!node.audit) push_str("audit", "false");
          query_template_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_QUERY_TEMPLATE);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, QueryOptimizerDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.cost_model.empty()) push_str("cost_model", node.cost_model);
          if (node.max_cost_per_query > 0.0) push_str("max_cost_per_query", std::to_string(node.max_cost_per_query));
          if (node.max_scan_gb > 0.0) push_str("max_scan_gb", std::to_string(node.max_scan_gb));
          if (node.max_execution_time != 300) push_str("max_execution_time", std::to_string(node.max_execution_time));
          if (!node.rules_json.empty()) push_str("rules", node.rules_json);
          if (!node.explain_optimizations) push_str("explain_optimizations", "false");
          if (!node.show_cost_comparison) push_str("show_cost_comparison", "false");
          query_optimizer_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_QUERY_OPTIMIZER);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, ExecutionPolicyDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (node.max_rows != 10000) push_str("max_rows", std::to_string(node.max_rows));
          if (node.max_cost > 0.0) push_str("max_cost", std::to_string(node.max_cost));
          if (node.timeout != 300) push_str("timeout", std::to_string(node.timeout));
          if (!node.read_only) push_str("read_only", "false");
          if (!node.apply_masking) push_str("apply_masking", "false");
          if (!node.apply_row_level_security) push_str("apply_row_level_security", "false");
          if (!node.audit_all_queries) push_str("audit_all_queries", "false");
          if (node.retry_on_timeout) push_str("retry_on_timeout", "true");
          if (node.retry_with_smaller_warehouse) push_str("retry_with_smaller_warehouse", "true");
          if (!node.cache_results) push_str("cache_results", "false");
          if (!node.cache_ttl.empty()) push_str("cache_ttl", node.cache_ttl);
          if (!node.cache_key.empty()) push_str("cache_key", node.cache_key);
          execution_policy_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_EXECUTION_POLICY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, OutputFormatDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.type_str.empty()) push_str("type", node.type_str);
          if (!node.excel_json.empty()) push_str("excel", node.excel_json);
          if (!node.pdf_json.empty()) push_str("pdf", node.pdf_json);
          if (!node.html_json.empty()) push_str("html", node.html_json);
          if (!node.csv_json.empty()) push_str("csv", node.csv_json);
          if (!node.json_config_json.empty()) push_str("json_config", node.json_config_json);
          if (!node.slack_json.empty()) push_str("slack", node.slack_json);
          output_format_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_OUTPUT_FORMAT);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, QueryLibraryDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          push_str("name", node.name);
          if (!node.storage.empty()) push_str("storage", node.storage);
          if (!node.path.empty()) push_str("path", node.path);
          if (!node.categories.empty()) push_str_list("categories", node.categories);
          if (node.tags) push_str("tags", "true");
          if (!node.library_visibility.empty()) push_str("visibility", node.library_visibility);
          if (node.approval_required) push_str("approval_required", "true");
          if (node.track_usage) push_str("track_usage", "true");
          if (node.track_performance) push_str("track_performance", "true");
          if (node.suggest_similar) push_str("suggest_similar", "true");
          if (node.auto_optimize) push_str("auto_optimize", "true");
          query_library_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_QUERY_LIBRARY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, AnalysisScheduleDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.query.empty()) push_str("query", node.query);
          if (!node.cron.empty()) push_str("cron", node.cron);
          if (!node.connection.empty()) push_str("connection", node.connection);
          if (!node.format.empty()) push_str("format", node.format);
          if (!node.output_path.empty()) push_str("output_path", node.output_path);
          if (!node.delivery_json.empty()) push_str("delivery", node.delivery_json);
          if (!node.audit) push_str("audit", "false");
          if (!node.budget.empty()) push_str("budget", node.budget);
          analysis_schedule_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_ANALYSIS_SCHEDULE);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, AnalystAgentDecl>)
        {
          validate_analyst_agent(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& n, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(n.c_str(), n.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          push_str("name", node.name);
          if (node.provider) push_str("provider", *node.provider);
          if (node.model) push_str("model", *node.model);
          if (node.system_prompt) push_str("system", *node.system_prompt);
          if (node.temperature != 0.2) push_str("temperature", std::to_string(node.temperature));
          if (node.endpoint) push_str("endpoint", *node.endpoint);
          if (node.api_key_env) push_str("api_key_env", *node.api_key_env);
          if (!node.connections.empty()) push_str_list("connections", node.connections);
          if (!node.domain_context.empty()) push_str("domain_context", node.domain_context);
          if (!node.models.empty()) push_str_list("models", node.models);
          if (!node.dimensional_models.empty()) push_str_list("dimensional_models", node.dimensional_models);
          if (!node.marts.empty()) push_str_list("marts", node.marts);
          if (!node.glossary.empty()) push_str("glossary", node.glossary);
          if (!node.semantic_layers.empty()) push_str_list("semantic_layers", node.semantic_layers);
          if (!node.governance.empty()) push_str("governance", node.governance);
          if (!node.classification.empty()) push_str("classification", node.classification);
          if (!node.access_policy.empty()) push_str("access_policy", node.access_policy);
          if (!node.optimizer.empty()) push_str("optimizer", node.optimizer);
          if (!node.execution_policy.empty()) push_str("execution_policy", node.execution_policy);
          if (!node.query_library.empty()) push_str("query_library", node.query_library);
          if (!node.default_output.empty()) push_str("default_output", node.default_output);
          if (!node.output_formats.empty()) push_str_list("output_formats", node.output_formats);
          if (!node.skills.empty()) push_str_list("skills", node.skills);
          if (!node.extern_skills.empty()) push_str_list("extern_skills", node.extern_skills);
          if (!node.coordinates_with.empty()) push_str_list("coordinates_with", node.coordinates_with);
          if (!node.handoffs.empty()) push_str_list("handoffs", node.handoffs);
          if (node.role) push_str("role", *node.role);
          if (node.purpose) push_str("purpose", *node.purpose);
          if (node.autonomy) push_str("autonomy", *node.autonomy);
          if (node.budget) push_str("budget", *node.budget);
          analyst_agent_defs_.insert(node.name);
          agent_types_[node.name] = AgentKind::Analyst;
          chunk_.write_op(vm::OpCode::OP_DEFINE_ANALYST_AGENT);
          chunk_.write_byte(field_count);
        }
        // v0.9.7: Data Pipeline Deployment emit handlers
        else if constexpr (std::is_same_v<T, DeployTargetDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.environment.empty()) push_str("environment", node.environment);
          if (!node.connection.empty()) push_str("connection", node.connection);
          if (!node.namespace_name.empty()) push_str("namespace", node.namespace_name);
          if (!node.region.empty()) push_str("region", node.region);
          if (!node.tags_json.empty()) push_str("tags", node.tags_json);
          if (!node.variables_json.empty()) push_str("variables", node.variables_json);
          push_str("frozen", node.frozen ? "true" : "false");
          if (!node.freeze_reason.empty()) push_str("freeze_reason", node.freeze_reason);

          if (!node.environment.empty() &&
              node.environment != "dev" && node.environment != "staging" &&
              node.environment != "prod" && node.environment != "test")
          {
            compiler_warning("[DEP-009] deploy_target '" + node.name +
              "' has invalid environment '" + node.environment +
              "'; expected 'dev', 'staging', 'prod', or 'test'");
          }

          deploy_target_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DEPLOY_TARGET);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, PromotionRuleDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.from_env.empty()) push_str("from_env", node.from_env);
          if (!node.to_env.empty()) push_str("to_env", node.to_env);
          push_str("require_tests", node.require_tests ? "true" : "false");
          push_str("require_approval", node.require_approval ? "true" : "false");
          if (!node.approvers.empty())
          {
            std::string csv;
            for (size_t i = 0; i < node.approvers.size(); i++)
            {
              if (i > 0) csv += ",";
              csv += node.approvers[i];
            }
            push_str("approvers", csv);
          }
          push_str("auto_promote", node.auto_promote ? "true" : "false");
          if (!node.cooldown.empty()) push_str("cooldown", node.cooldown);
          if (!node.gate_checks_json.empty()) push_str("gate_checks", node.gate_checks_json);

          if (!node.from_env.empty() && !node.to_env.empty() && node.from_env == node.to_env)
          {
            compiler_warning("[DEP-003] promotion_rule '" + node.name +
              "' has from_env == to_env ('" + node.from_env + "'); environments must differ");
          }
          if (node.auto_promote && node.require_approval)
          {
            compiler_warning("[DEP-004] promotion_rule '" + node.name +
              "' has both auto_promote and require_approval; these are mutually exclusive");
          }

          promotion_rule_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_PROMOTION_RULE);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, RollbackPolicyDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.strategy.empty()) push_str("strategy", node.strategy);
          push_str("keep_data", node.keep_data ? "true" : "false");
          push_str("notify_dataops", node.notify_dataops ? "true" : "false");
          if (!node.max_rollback_window.empty()) push_str("max_rollback_window", node.max_rollback_window);
          push_str("reconciliation", node.reconciliation ? "true" : "false");
          if (!node.pre_rollback_json.empty()) push_str("pre_rollback_checks", node.pre_rollback_json);
          if (!node.notifications_json.empty()) push_str("notifications", node.notifications_json);

          if (!node.strategy.empty() &&
              node.strategy != "immediate" && node.strategy != "gradual" && node.strategy != "point_in_time")
          {
            compiler_warning("[DEP-005] rollback_policy '" + node.name +
              "' has invalid strategy '" + node.strategy +
              "'; expected 'immediate', 'gradual', or 'point_in_time'");
          }

          rollback_policy_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_ROLLBACK_POLICY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, ArtifactRegistryDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.storage.empty()) push_str("storage", node.storage);
          if (!node.path.empty()) push_str("path", node.path);
          if (!node.versioning.empty()) push_str("versioning", node.versioning);
          if (!node.retention.empty()) push_str("retention", node.retention);
          push_str("sign_artifacts", node.sign_artifacts ? "true" : "false");
          if (!node.checksum.empty()) push_str("checksum", node.checksum);
          push_str("immutable", node.immutable ? "true" : "false");

          if (!node.storage.empty() &&
              node.storage != "local" && node.storage != "s3" && node.storage != "registry")
          {
            compiler_warning("[DEP-006] artifact_registry '" + node.name +
              "' has invalid storage '" + node.storage + "'; expected 'local', 's3', or 'registry'");
          }
          if (!node.versioning.empty() &&
              node.versioning != "semver" && node.versioning != "timestamp" && node.versioning != "git_sha")
          {
            compiler_warning("[DEP-007] artifact_registry '" + node.name +
              "' has invalid versioning '" + node.versioning +
              "'; expected 'semver', 'timestamp', or 'git_sha'");
          }

          artifact_registry_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_ARTIFACT_REGISTRY);
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, DeployConfigDecl>)
        {
          validate_deploy_config(node);

          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.target.empty()) push_str("target", node.target);
          if (!node.strategy.empty()) push_str("strategy", node.strategy);
          push_str("approval_gate", node.approval_gate ? "true" : "false");
          if (!node.pipeline_ref.empty()) push_str("pipeline_ref", node.pipeline_ref);
          if (!node.pre_deploy_json.empty()) push_str("pre_deploy_checks", node.pre_deploy_json);
          if (!node.post_deploy_json.empty()) push_str("post_deploy_checks", node.post_deploy_json);
          if (!node.notifications_json.empty()) push_str("notifications", node.notifications_json);
          if (!node.schedule.empty()) push_str("schedule", node.schedule);
          push_str("auto_rollback", node.auto_rollback ? "true" : "false");
          if (!node.rollback_policy.empty()) push_str("rollback_policy", node.rollback_policy);
          if (!node.artifact_registry.empty()) push_str("artifact_registry", node.artifact_registry);

          deploy_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DEPLOY_CONFIG);
          chunk_.write_byte(field_count);
        }
        // v0.9.8: Data Scientist Agent emit handlers
        // ═══════════════════════════════════════════════════════════════
        // Key type 1: ProblemStatementDecl (full handler)
        else if constexpr (std::is_same_v<T, ProblemStatementDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.statement.empty()) push_str("statement", node.statement);
          if (!node.business_context_json.empty()) push_str("business_context", node.business_context_json);
          if (!node.constraints_json.empty()) push_str("constraints", node.constraints_json);
          if (!node.deliverables_json.empty()) push_str("deliverables", node.deliverables_json);

          problem_statement_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_PROBLEM_STATEMENT);
          chunk_.write_byte(field_count);
        }
        // Compact type: HypothesisTestDecl
        else if constexpr (std::is_same_v<T, HypothesisTestDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.null_hypothesis.empty()) push_str("null_hypothesis", node.null_hypothesis);
          if (!node.alternative.empty()) push_str("alternative", node.alternative);
          if (!node.test_type.empty()) push_str("test_type", node.test_type);
          if (node.significance_level != 0.05) push_str("significance_level", std::to_string(node.significance_level));
          if (node.power != 0.80) push_str("power", std::to_string(node.power));
          if (!node.effect_size.empty()) push_str("effect_size", node.effect_size);
          if (!node.data_source.empty()) push_str("data_source", node.data_source);
          if (!node.group_a.empty()) push_str("group_a", node.group_a);
          if (!node.group_b.empty()) push_str("group_b", node.group_b);
          if (!node.assumptions_json.empty()) push_str("assumptions", node.assumptions_json);
          if (!node.if_significant_json.empty()) push_str("if_significant", node.if_significant_json);
          if (!node.if_not_significant_json.empty()) push_str("if_not_significant", node.if_not_significant_json);

          hypothesis_test_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_HYPOTHESIS_TEST);
          chunk_.write_byte(field_count);
        }
        // Compact type: FeatureEngineeringDecl
        else if constexpr (std::is_same_v<T, FeatureEngineeringDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.source_tables_json.empty()) push_str("source_tables", node.source_tables_json);
          if (!node.strategies_json.empty()) push_str("strategies", node.strategies_json);
          if (!node.selection_json.empty()) push_str("selection", node.selection_json);
          if (!node.output_json.empty()) push_str("output", node.output_json);

          feature_engineering_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_FEATURE_ENGINEERING);
          chunk_.write_byte(field_count);
        }
        // Key type 2: MLExperimentDecl (full handler)
        else if constexpr (std::is_same_v<T, MLExperimentDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_num = [&](const std::string& key, double val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Number(static_cast<double>(val)));
            field_count++;
          };
          auto push_bool = [&](const std::string& key, bool val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Bool(val));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.problem_type.empty()) push_str("problem_type", node.problem_type);
          if (!node.target.empty()) push_str("target", node.target);
          if (!node.positive_class.empty()) push_str("positive_class", node.positive_class);
          if (!node.dataset.empty()) push_str("dataset", node.dataset);
          if (node.train_test_split != 0.8) push_num("train_test_split", node.train_test_split);
          push_bool("stratify", node.stratify);
          if (!node.cross_validation_json.empty()) push_str("cross_validation", node.cross_validation_json);
          if (!node.algorithms.empty()) push_str("algorithms", node.algorithms);
          if (!node.metrics_json.empty()) push_str("metrics", node.metrics_json);
          if (!node.interpretability.empty()) push_str("interpretability", node.interpretability);
          if (!node.latency_requirement.empty()) push_str("latency_requirement", node.latency_requirement);
          if (!node.max_training_time.empty()) push_str("max_training_time", node.max_training_time);
          if (!node.budget.empty()) push_str("budget", node.budget);

          ml_experiment_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_ML_EXPERIMENT);
          chunk_.write_byte(field_count);
        }
        // Compact type: AutoMLConfigDecl
        else if constexpr (std::is_same_v<T, AutoMLConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.algorithms.empty()) push_str("algorithms", node.algorithms);
          if (!node.preprocessing_search_json.empty()) push_str("preprocessing_search", node.preprocessing_search_json);
          if (!node.optimization_json.empty()) push_str("optimization", node.optimization_json);
          if (node.cv_folds != 5) push_str("cv_folds", std::to_string(node.cv_folds));
          if (!node.primary_metric.empty()) push_str("primary_metric", node.primary_metric);
          push_str("holdout_validation", node.holdout_validation ? "true" : "false");
          if (!node.selection_criteria_json.empty()) push_str("selection_criteria", node.selection_criteria_json);
          push_str("leaderboard", node.leaderboard ? "true" : "false");

          automl_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_AUTOML_CONFIG);
          chunk_.write_byte(field_count);
        }
        // Compact type: HyperparameterConfigDecl
        else if constexpr (std::is_same_v<T, HyperparameterConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.algorithm.empty()) push_str("algorithm", node.algorithm);
          if (!node.search_space_json.empty()) push_str("search_space", node.search_space_json);
          if (!node.optimizer_json.empty()) push_str("optimizer", node.optimizer_json);
          if (!node.early_stopping_json.empty()) push_str("early_stopping", node.early_stopping_json);

          hyperparameter_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_HYPERPARAMETER_CONFIG);
          chunk_.write_byte(field_count);
        }
        // Compact type: StackedModelDecl
        else if constexpr (std::is_same_v<T, StackedModelDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.base_learners_json.empty()) push_str("base_learners", node.base_learners_json);
          if (!node.meta_learner_json.empty()) push_str("meta_learner", node.meta_learner_json);
          if (!node.strategy_json.empty()) push_str("strategy", node.strategy_json);
          if (!node.compare_against.empty()) push_str("compare_against", node.compare_against);
          if (node.improvement_threshold != 0.005) push_str("improvement_threshold", std::to_string(node.improvement_threshold));

          stacked_model_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_STACKED_MODEL);
          chunk_.write_byte(field_count);
        }
        // Compact type: EvaluationConfigDecl
        else if constexpr (std::is_same_v<T, EvaluationConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.classification_json.empty()) push_str("classification", node.classification_json);
          if (!node.regression_json.empty()) push_str("regression", node.regression_json);
          if (!node.clustering_json.empty()) push_str("clustering", node.clustering_json);
          if (!node.business_json.empty()) push_str("business", node.business_json);
          if (!node.cv_strategy.empty()) push_str("cv_strategy", node.cv_strategy);
          if (node.outer_folds != 5) push_str("outer_folds", std::to_string(node.outer_folds));
          if (node.inner_folds != 3) push_str("inner_folds", std::to_string(node.inner_folds));
          if (!node.model_comparison_json.empty()) push_str("model_comparison", node.model_comparison_json);

          evaluation_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_EVALUATION_CONFIG);
          chunk_.write_byte(field_count);
        }
        // Compact type: ModelRegistryDecl
        else if constexpr (std::is_same_v<T, ModelRegistryDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.storage_json.empty()) push_str("storage", node.storage_json);
          if (!node.tracking_json.empty()) push_str("tracking", node.tracking_json);
          if (!node.model_card_json.empty()) push_str("model_card", node.model_card_json);
          if (!node.lifecycle_json.empty()) push_str("lifecycle", node.lifecycle_json);

          ds_model_registry_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DS_MODEL_REGISTRY);
          chunk_.write_byte(field_count);
        }
        // Compact type: ExplainabilityConfigDecl
        else if constexpr (std::is_same_v<T, ExplainabilityConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.global_json.empty()) push_str("global", node.global_json);
          if (!node.local_json.empty()) push_str("local", node.local_json);
          if (!node.fairness_json.empty()) push_str("fairness", node.fairness_json);

          explainability_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_EXPLAINABILITY_CONFIG);
          chunk_.write_byte(field_count);
        }
        // Key type 5: CodeInterpreterDecl (full handler)
        else if constexpr (std::is_same_v<T, CodeInterpreterDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.runtime.empty()) push_str("runtime", node.runtime);
          if (!node.version.empty()) push_str("version", node.version);
          if (!node.venv_manager.empty()) push_str("venv_manager", node.venv_manager);
          if (!node.profiles_json.empty()) push_str("profiles", node.profiles_json);
          if (!node.profile_selection.empty()) push_str("profile_selection", node.profile_selection);
          if (!node.sandbox_json.empty()) push_str("sandbox", node.sandbox_json);
          if (!node.auto_test_json.empty()) push_str("auto_test", node.auto_test_json);
          if (!node.data_bridge_json.empty()) push_str("data_bridge", node.data_bridge_json);

          if (!node.runtime.empty() && node.runtime != "python" && node.runtime != "r")
          {
            compiler_warning("[DS-001] code_interpreter '" + node.name +
              "' has invalid runtime '" + node.runtime +
              "'; expected 'python' or 'r'");
          }

          code_interpreter_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_CODE_INTERPRETER);
          chunk_.write_byte(field_count);
        }
        // Compact type: VenvManagerDecl
        else if constexpr (std::is_same_v<T, VenvManagerDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.lifecycle_json.empty()) push_str("lifecycle", node.lifecycle_json);
          if (!node.pool_json.empty()) push_str("pool", node.pool_json);
          if (!node.dependency_resolver_json.empty()) push_str("dependency_resolver", node.dependency_resolver_json);

          venv_manager_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_VENV_MANAGER);
          chunk_.write_byte(field_count);
        }
        // Compact type: NLPPipelineDecl
        else if constexpr (std::is_same_v<T, NLPPipelineDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.preprocessing_json.empty()) push_str("preprocessing", node.preprocessing_json);
          if (!node.tasks_json.empty()) push_str("tasks", node.tasks_json);
          if (!node.embedding_model.empty()) push_str("embedding_model", node.embedding_model);
          if (!node.vector_store.empty()) push_str("vector_store", node.vector_store);

          nlp_pipeline_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_NLP_PIPELINE);
          chunk_.write_byte(field_count);
        }
        // Compact type: ChurnAnalysisDecl
        else if constexpr (std::is_same_v<T, ChurnAnalysisDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.churn_definition_json.empty()) push_str("churn_definition", node.churn_definition_json);
          if (!node.features_json.empty()) push_str("features", node.features_json);
          if (!node.primary_model.empty()) push_str("primary_model", node.primary_model);
          if (!node.calibration.empty()) push_str("calibration", node.calibration);
          if (!node.threshold_optimization.empty()) push_str("threshold_optimization", node.threshold_optimization);
          if (!node.outputs_json.empty()) push_str("outputs", node.outputs_json);

          churn_analysis_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_CHURN_ANALYSIS);
          chunk_.write_byte(field_count);
        }
        // Compact type: CLVModelDecl
        else if constexpr (std::is_same_v<T, CLVModelDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.model_type.empty()) push_str("model_type", node.model_type);
          if (!node.frequency.empty()) push_str("frequency", node.frequency);
          if (!node.recency.empty()) push_str("recency", node.recency);
          if (!node.monetary.empty()) push_str("monetary", node.monetary);
          if (!node.T.empty()) push_str("T", node.T);
          if (!node.prediction_periods_json.empty()) push_str("prediction_periods", node.prediction_periods_json);
          if (node.discount_rate != 0.10) push_str("discount_rate", std::to_string(node.discount_rate));
          if (!node.segments_json.empty()) push_str("segments", node.segments_json);

          clv_model_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_CLV_MODEL);
          chunk_.write_byte(field_count);
        }
        // Compact type: PropensityModelDecl
        else if constexpr (std::is_same_v<T, PropensityModelDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.target_action.empty()) push_str("target_action", node.target_action);
          if (!node.training_window.empty()) push_str("training_window", node.training_window);
          if (!node.features_json.empty()) push_str("features", node.features_json);
          if (!node.algorithm.empty()) push_str("algorithm", node.algorithm);
          if (!node.calibration_method.empty()) push_str("calibration_method", node.calibration_method);
          if (!node.score_output_json.empty()) push_str("score_output", node.score_output_json);
          if (!node.actions_json.empty()) push_str("actions", node.actions_json);

          propensity_model_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_PROPENSITY_MODEL);
          chunk_.write_byte(field_count);
        }
        // Compact type: RecommendationEngineDecl
        else if constexpr (std::is_same_v<T, RecommendationEngineDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.strategy.empty()) push_str("strategy", node.strategy);
          if (!node.collaborative_json.empty()) push_str("collaborative", node.collaborative_json);
          if (!node.content_based_json.empty()) push_str("content_based", node.content_based_json);
          if (!node.blending_json.empty()) push_str("blending", node.blending_json);
          if (!node.rules_json.empty()) push_str("rules", node.rules_json);
          if (!node.metrics.empty()) push_str("metrics", node.metrics);
          if (!node.serving_json.empty()) push_str("serving", node.serving_json);

          recommendation_engine_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_RECOMMENDATION_ENGINE);
          chunk_.write_byte(field_count);
        }
        // Compact type: ExperimentDesignDecl
        else if constexpr (std::is_same_v<T, ExperimentDesignDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.experiment_type.empty()) push_str("experiment_type", node.experiment_type);
          if (!node.control_json.empty()) push_str("control", node.control_json);
          if (!node.treatments_json.empty()) push_str("treatments", node.treatments_json);
          if (!node.unit.empty()) push_str("unit", node.unit);
          if (!node.stratify_by.empty()) push_str("stratify_by", node.stratify_by);
          if (!node.power_analysis_json.empty()) push_str("power_analysis", node.power_analysis_json);
          if (!node.primary_metric_json.empty()) push_str("primary_metric", node.primary_metric_json);
          if (!node.guardrails_json.empty()) push_str("guardrails", node.guardrails_json);
          if (!node.analysis_json.empty()) push_str("analysis", node.analysis_json);

          experiment_design_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_EXPERIMENT_DESIGN);
          chunk_.write_byte(field_count);
        }
        // Compact type: ScenarioAnalysisDecl
        else if constexpr (std::is_same_v<T, ScenarioAnalysisDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.base_model.empty()) push_str("base_model", node.base_model);
          if (!node.scenarios_json.empty()) push_str("scenarios", node.scenarios_json);
          if (!node.simulation_json.empty()) push_str("simulation", node.simulation_json);

          scenario_analysis_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_SCENARIO_ANALYSIS);
          chunk_.write_byte(field_count);
        }
        // Compact type: DecisionSupportDecl
        else if constexpr (std::is_same_v<T, DecisionSupportDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.deliverables_json.empty()) push_str("deliverables", node.deliverables_json);
          if (!node.confidence_json.empty()) push_str("confidence", node.confidence_json);

          decision_support_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DECISION_SUPPORT);
          chunk_.write_byte(field_count);
        }
        // Key type 3: EDAConfigDecl (full handler)
        else if constexpr (std::is_same_v<T, EDAConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.structural_json.empty()) push_str("structural", node.structural_json);
          if (!node.univariate_json.empty()) push_str("univariate", node.univariate_json);
          if (!node.bivariate_json.empty()) push_str("bivariate", node.bivariate_json);
          if (!node.multivariate_json.empty()) push_str("multivariate", node.multivariate_json);
          if (!node.temporal_json.empty()) push_str("temporal", node.temporal_json);
          if (!node.performance_json.empty()) push_str("performance", node.performance_json);
          if (!node.output_json.empty()) push_str("output", node.output_json);

          eda_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_EDA_CONFIG);
          chunk_.write_byte(field_count);
        }
        // Compact type: EDATechniqueSelectorDecl
        else if constexpr (std::is_same_v<T, EDATechniqueSelectorDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.rules_json.empty()) push_str("rules", node.rules_json);
          if (!node.output_json.empty()) push_str("output", node.output_json);

          eda_technique_selector_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_EDA_TECHNIQUE_SELECTOR);
          chunk_.write_byte(field_count);
        }
        // Compact type: SmartConnectorDecl
        else if constexpr (std::is_same_v<T, SmartConnectorDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.discovery_json.empty()) push_str("discovery", node.discovery_json);
          if (!node.metadata_cache_json.empty()) push_str("metadata_cache", node.metadata_cache_json);

          smart_connector_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_SMART_CONNECTOR);
          chunk_.write_byte(field_count);
        }
        // Key type 4: VolumeRouterDecl (full handler)
        else if constexpr (std::is_same_v<T, VolumeRouterDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.volume_probe_json.empty()) push_str("volume_probe", node.volume_probe_json);
          if (!node.routing_rules_json.empty()) push_str("routing_rules", node.routing_rules_json);
          if (!node.auto_escalate_json.empty()) push_str("auto_escalate", node.auto_escalate_json);
          if (!node.sampling_strategy_json.empty()) push_str("sampling_strategy", node.sampling_strategy_json);

          volume_router_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_VOLUME_ROUTER);
          chunk_.write_byte(field_count);
        }
        // Compact type: ComputeConnectorDecl
        else if constexpr (std::is_same_v<T, ComputeConnectorDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.engine.empty()) push_str("engine", node.engine);
          if (!node.connection.empty()) push_str("connection", node.connection);
          if (!node.token.empty()) push_str("token", node.token);
          if (!node.cluster_config_json.empty()) push_str("cluster_config", node.cluster_config_json);
          if (!node.idle_timeout.empty()) push_str("idle_timeout", node.idle_timeout);
          push_str("cost_tracking", node.cost_tracking ? "true" : "false");

          compute_connector_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_COMPUTE_CONNECTOR);
          chunk_.write_byte(field_count);
        }
        // Compact type: FileConnectorDecl
        else if constexpr (std::is_same_v<T, FileConnectorDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.base_path.empty()) push_str("base_path", node.base_path);
          push_str("auto_detect_schema", node.auto_detect_schema ? "true" : "false");
          push_str("auto_detect_delimiter", node.auto_detect_delimiter ? "true" : "false");
          push_str("auto_detect_encoding", node.auto_detect_encoding ? "true" : "false");
          if (!node.supported_formats_json.empty()) push_str("supported_formats", node.supported_formats_json);
          if (!node.large_file_strategy_json.empty()) push_str("large_file_strategy", node.large_file_strategy_json);

          file_connector_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_FILE_CONNECTOR);
          chunk_.write_byte(field_count);
        }
        // Compact type: DistributedComputeConfigDecl
        else if constexpr (std::is_same_v<T, DistributedComputeConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.spark_json.empty()) push_str("spark", node.spark_json);
          if (!node.databricks_json.empty()) push_str("databricks", node.databricks_json);
          if (!node.snowflake_json.empty()) push_str("snowflake", node.snowflake_json);
          if (!node.hadoop_json.empty()) push_str("hadoop", node.hadoop_json);
          if (!node.gpu_json.empty()) push_str("gpu", node.gpu_json);
          if (!node.selection_logic_json.empty()) push_str("selection_logic", node.selection_logic_json);

          distributed_compute_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DISTRIBUTED_COMPUTE_CONFIG);
          chunk_.write_byte(field_count);
        }
        // Compact type: PerformanceConfigDecl
        else if constexpr (std::is_same_v<T, PerformanceConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.phase_slas_json.empty()) push_str("phase_slas", node.phase_slas_json);
          if (!node.total_analysis_sla_json.empty()) push_str("total_analysis_sla", node.total_analysis_sla_json);
          if (!node.cache_json.empty()) push_str("cache", node.cache_json);
          if (!node.parallelism_json.empty()) push_str("parallelism", node.parallelism_json);
          if (!node.lazy_eval_json.empty()) push_str("lazy_eval", node.lazy_eval_json);
          if (!node.memory_json.empty()) push_str("memory", node.memory_json);

          performance_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_PERFORMANCE_CONFIG);
          chunk_.write_byte(field_count);
        }
        // Compact type: DataQualityPipelineDecl
        else if constexpr (std::is_same_v<T, DataQualityPipelineDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.profiling_json.empty()) push_str("profiling", node.profiling_json);
          if (!node.scoring_json.empty()) push_str("scoring", node.scoring_json);
          if (!node.remediation_json.empty()) push_str("remediation", node.remediation_json);
          if (!node.report_json.empty()) push_str("report", node.report_json);
          if (!node.governance_ref.empty()) push_str("governance_ref", node.governance_ref);

          data_quality_pipeline_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DATA_QUALITY_PIPELINE);
          chunk_.write_byte(field_count);
        }
        // Compact type: SelfCorrectionConfigDecl
        else if constexpr (std::is_same_v<T, SelfCorrectionConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.code_errors_json.empty()) push_str("code_errors", node.code_errors_json);
          if (!node.statistical_errors_json.empty()) push_str("statistical_errors", node.statistical_errors_json);
          if (!node.model_errors_json.empty()) push_str("model_errors", node.model_errors_json);
          if (!node.reasoning_errors_json.empty()) push_str("reasoning_errors", node.reasoning_errors_json);

          self_correction_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_SELF_CORRECTION_CONFIG);
          chunk_.write_byte(field_count);
        }
        // Compact type: SelfAssessmentDecl
        else if constexpr (std::is_same_v<T, SelfAssessmentDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.planning_json.empty()) push_str("planning", node.planning_json);
          if (!node.execution_json.empty()) push_str("execution", node.execution_json);
          if (!node.interpretation_json.empty()) push_str("interpretation", node.interpretation_json);
          if (!node.communication_json.empty()) push_str("communication", node.communication_json);
          if (!node.gate_json.empty()) push_str("gate", node.gate_json);

          self_assessment_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_SELF_ASSESSMENT);
          chunk_.write_byte(field_count);
        }
        // Compact type: AdaptiveKnowledgeConfigDecl
        else if constexpr (std::is_same_v<T, AdaptiveKnowledgeConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.knowledge_sources_json.empty()) push_str("knowledge_sources", node.knowledge_sources_json);
          if (!node.adaptation_json.empty()) push_str("adaptation", node.adaptation_json);
          if (!node.learning_json.empty()) push_str("learning", node.learning_json);

          adaptive_knowledge_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_ADAPTIVE_KNOWLEDGE_CONFIG);
          chunk_.write_byte(field_count);
        }
        // Compact type: AnalysisHistoryDecl
        else if constexpr (std::is_same_v<T, AnalysisHistoryDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.knowledge_base.empty()) push_str("knowledge_base", node.knowledge_base);
          if (!node.vector_store.empty()) push_str("vector_store", node.vector_store);
          if (!node.embedding_model.empty()) push_str("embedding_model", node.embedding_model);
          if (!node.retrieval_strategy.empty()) push_str("retrieval_strategy", node.retrieval_strategy);
          if (!node.record_fields_json.empty()) push_str("record_fields", node.record_fields_json);
          if (!node.retention.empty()) push_str("retention", node.retention);
          if (node.max_records != 10000) push_str("max_records", std::to_string(node.max_records));

          analysis_history_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_ANALYSIS_HISTORY);
          chunk_.write_byte(field_count);
        }
        // Compact type: ObservabilityConfigDecl
        else if constexpr (std::is_same_v<T, ObservabilityConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.feature_monitoring_json.empty()) push_str("feature_monitoring", node.feature_monitoring_json);
          if (!node.prediction_monitoring_json.empty()) push_str("prediction_monitoring", node.prediction_monitoring_json);
          if (!node.alerts_json.empty()) push_str("alerts", node.alerts_json);
          if (!node.auto_remediation_json.empty()) push_str("auto_remediation", node.auto_remediation_json);
          if (!node.dataops_ref.empty()) push_str("dataops_ref", node.dataops_ref);

          observability_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_OBSERVABILITY_CONFIG);
          chunk_.write_byte(field_count);
        }
        // Key type 6: DataScientistAgentDecl (full handler)
        else if constexpr (std::is_same_v<T, DataScientistAgentDecl>)
        {
          validate_datascientist_agent(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          auto push_num = [&](const std::string& key, double val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Number(static_cast<double>(val)));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.system.empty()) push_str("system", node.system);
          if (node.temperature != 0.2) push_num("temperature", node.temperature);
          if (!node.endpoint.empty()) push_str("endpoint", node.endpoint);
          if (!node.api_key_env.empty()) push_str("api_key_env", node.api_key_env);
          if (!node.agent_md.empty()) push_str("agent_md", node.agent_md);
          if (!node.problem.empty()) push_str("problem", node.problem);
          if (!node.problem_types.empty()) push_str("problem_types", node.problem_types);
          if (!node.sub_agents_json.empty()) push_str("sub_agents", node.sub_agents_json);
          if (!node.forge.empty()) push_str("forge", node.forge);
          if (!node.data_sources_json.empty()) push_str("data_sources", node.data_sources_json);
          if (!node.eda_config.empty()) push_str("eda_config", node.eda_config);
          if (!node.feature_config.empty()) push_str("feature_config", node.feature_config);
          if (!node.experiment.empty()) push_str("experiment", node.experiment);
          if (!node.automl.empty()) push_str("automl", node.automl);
          if (!node.ensemble.empty()) push_str("ensemble", node.ensemble);
          if (!node.hypotheses.empty()) push_str_list("hypotheses", node.hypotheses);
          if (!node.evaluation.empty()) push_str("evaluation", node.evaluation);
          if (!node.explainability.empty()) push_str("explainability", node.explainability);
          if (!node.code_interpreter.empty()) push_str("code_interpreter", node.code_interpreter);
          if (!node.model_registry.empty()) push_str("model_registry", node.model_registry);
          if (!node.churn.empty()) push_str("churn", node.churn);
          if (!node.clv.empty()) push_str("clv", node.clv);
          if (!node.propensity.empty()) push_str("propensity", node.propensity);
          if (!node.recommendation.empty()) push_str("recommendation", node.recommendation);
          if (!node.experiment_engine.empty()) push_str("experiment_engine", node.experiment_engine);
          if (!node.decision_framework.empty()) push_str("decision_framework", node.decision_framework);
          if (!node.volume_router.empty()) push_str("volume_router", node.volume_router);
          if (!node.distributed_compute.empty()) push_str("distributed_compute", node.distributed_compute);
          if (!node.performance.empty()) push_str("performance", node.performance);
          if (!node.data_quality.empty()) push_str("data_quality", node.data_quality);
          if (!node.self_correction.empty()) push_str("self_correction", node.self_correction);
          if (!node.self_assessment.empty()) push_str("self_assessment", node.self_assessment);
          if (!node.adaptive_knowledge.empty()) push_str("adaptive_knowledge", node.adaptive_knowledge);
          if (!node.deployment.empty()) push_str("deployment", node.deployment);
          if (!node.coordinates_with.empty()) push_str_list("coordinates_with", node.coordinates_with);
          if (!node.handoffs.empty()) push_str_list("handoffs", node.handoffs);
          if (!node.role.empty()) push_str("role", node.role);
          if (!node.purpose.empty()) push_str("purpose", node.purpose);
          if (!node.autonomy.empty()) push_str("autonomy", node.autonomy);
          if (!node.budget.empty()) push_str("budget", node.budget);

          datascientist_agent_defs_.insert(node.name);
          agent_types_[node.name] = AgentKind::DataScientist;
          chunk_.write_op(vm::OpCode::OP_DEFINE_DATASCIENTIST_AGENT);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.1: CausalDiscoveryDecl
        else if constexpr (std::is_same_v<T, CausalDiscoveryDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.llm_discovery_json.empty()) push_str("llm_discovery", node.llm_discovery_json);
          if (!node.algorithmic_discovery_json.empty()) push_str("algorithmic_discovery", node.algorithmic_discovery_json);
          if (!node.merge_strategy_json.empty()) push_str("merge_strategy", node.merge_strategy_json);
          if (!node.validation_json.empty()) push_str("validation", node.validation_json);

          causal_discovery_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_CAUSAL_DISCOVERY);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.1: SCMDecl
        else if constexpr (std::is_same_v<T, SCMDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.variables_json.empty()) push_str("variables", node.variables_json);
          if (!node.exogenous_json.empty()) push_str("exogenous", node.exogenous_json);
          if (!node.latent_confounders_json.empty()) push_str("latent_confounders", node.latent_confounders_json);
          if (!node.dag.empty()) push_str("dag", node.dag);

          scm_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_SCM);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.1: InterventionDecl
        else if constexpr (std::is_same_v<T, InterventionDecl>)
        {
          // CAU-003: Intervention must reference a declared SCM
          if (!node.scm.empty() && scm_defs_.find(node.scm) == scm_defs_.end()) {
            compiler_warning("CAU-003: intervention '" + node.name + "' references undeclared SCM '" + node.scm + "'");
          }
          // CAU-004: Intervention must have an outcome
          if (node.outcome.empty()) {
            compiler_warning("CAU-004: intervention '" + node.name + "' must declare an outcome");
          }
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_bool = [&](const std::string& key, bool val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Bool(val));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.scm.empty()) push_str("scm", node.scm);
          if (!node.do_json.empty()) push_str("do", node.do_json);
          if (!node.outcome.empty()) push_str("outcome", node.outcome);
          if (!node.identification_json.empty()) push_str("identification", node.identification_json);
          if (!node.estimation_json.empty()) push_str("estimation", node.estimation_json);
          if (!node.compare_with_naive) push_bool("compare_with_naive", node.compare_with_naive);

          intervention_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_INTERVENTION);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.1: CounterfactualDecl
        else if constexpr (std::is_same_v<T, CounterfactualDecl>)
        {
          // CAU-005: Counterfactual must reference a declared SCM
          if (!node.scm.empty() && scm_defs_.find(node.scm) == scm_defs_.end()) {
            compiler_warning("CAU-005: counterfactual '" + node.name + "' references undeclared SCM '" + node.scm + "'");
          }
          // CAU-006: Counterfactual must have a question
          if (node.question.empty()) {
            compiler_warning("CAU-006: counterfactual '" + node.name + "' must declare a question");
          }
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.scm.empty()) push_str("scm", node.scm);
          if (!node.evidence_json.empty()) push_str("evidence", node.evidence_json);
          if (!node.question.empty()) push_str("question", node.question);
          if (!node.abduction_json.empty()) push_str("abduction", node.abduction_json);
          if (!node.action_json.empty()) push_str("action", node.action_json);
          if (!node.prediction_json.empty()) push_str("prediction", node.prediction_json);
          if (!node.attribution_json.empty()) push_str("attribution", node.attribution_json);

          counterfactual_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_COUNTERFACTUAL);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.1: BayesianModelDecl
        else if constexpr (std::is_same_v<T, BayesianModelDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.framework.empty()) push_str("framework", node.framework);
          if (!node.version.empty()) push_str("version", node.version);
          if (!node.priors_json.empty()) push_str("priors", node.priors_json);
          if (!node.likelihood_json.empty()) push_str("likelihood", node.likelihood_json);
          if (!node.sampling_json.empty()) push_str("sampling", node.sampling_json);
          if (!node.posterior_json.empty()) push_str("posterior", node.posterior_json);
          if (!node.comparison_json.empty()) push_str("comparison", node.comparison_json);

          bayesian_model_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BAYESIAN_MODEL);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.1: CausalEstimatorDecl
        else if constexpr (std::is_same_v<T, CausalEstimatorDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_bool = [&](const std::string& key, bool val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Bool(val));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.scm.empty()) push_str("scm", node.scm);
          if (!node.treatment.empty()) push_str("treatment", node.treatment);
          if (!node.outcome.empty()) push_str("outcome", node.outcome);
          if (!node.primary_json.empty()) push_str("primary", node.primary_json);
          if (!node.secondary_json.empty()) push_str("secondary", node.secondary_json);
          if (!node.heterogeneous_json.empty()) push_str("heterogeneous", node.heterogeneous_json);
          if (!node.compare_estimators) push_bool("compare_estimators", node.compare_estimators);

          causal_estimator_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_CAUSAL_ESTIMATOR);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.1: QuasiExperimentDecl
        else if constexpr (std::is_same_v<T, QuasiExperimentDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_num = [&](const std::string& key, double val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Number(static_cast<double>(val)));
            field_count++;
          };
          auto push_bool = [&](const std::string& key, bool val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Bool(val));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.method.empty()) push_str("method", node.method);
          if (!node.treatment_time.empty()) push_str("treatment_time", node.treatment_time);
          if (!node.treatment_group.empty()) push_str("treatment_group", node.treatment_group);
          if (!node.control_group.empty()) push_str("control_group", node.control_group);
          if (!node.outcome.empty()) push_str("outcome", node.outcome);
          if (!node.covariates.empty()) push_str("covariates", node.covariates);
          if (!node.parallel_trends_test) push_bool("parallel_trends_test", node.parallel_trends_test);
          if (!node.bayesian) push_bool("bayesian", node.bayesian);
          if (!node.mcmc_json.empty()) push_str("mcmc", node.mcmc_json);
          if (!node.running_variable.empty()) push_str("running_variable", node.running_variable);
          if (node.cutoff != 0.0) push_num("cutoff", node.cutoff);
          if (!node.bandwidth.empty()) push_str("bandwidth", node.bandwidth);

          quasi_experiment_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_QUASI_EXPERIMENT);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.1: CausalSensitivityDecl
        else if constexpr (std::is_same_v<T, CausalSensitivityDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_bool = [&](const std::string& key, bool val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Bool(val));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.estimator.empty()) push_str("estimator", node.estimator);
          if (!node.rosenbaum_json.empty()) push_str("rosenbaum", node.rosenbaum_json);
          if (!node.e_value) push_bool("e_value", node.e_value);
          if (!node.refutations_json.empty()) push_str("refutations", node.refutations_json);
          if (!node.assumptions_json.empty()) push_str("assumptions", node.assumptions_json);
          if (!node.output_json.empty()) push_str("output", node.output_json);

          causal_sensitivity_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_CAUSAL_SENSITIVITY);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.1: CausalDataRequirementsDecl
        else if constexpr (std::is_same_v<T, CausalDataRequirementsDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.temporal_json.empty()) push_str("temporal", node.temporal_json);
          if (!node.required_confounders.empty()) push_str("required_confounders", node.required_confounders);
          if (!node.instruments_json.empty()) push_str("instruments", node.instruments_json);
          if (!node.natural_experiments.empty()) push_str("natural_experiments", node.natural_experiments);
          if (!node.quality_json.empty()) push_str("quality", node.quality_json);

          causal_data_requirements_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_CAUSAL_DATA_REQUIREMENTS);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.1: CausalAgentDecl (full handler)
        else if constexpr (std::is_same_v<T, CausalAgentDecl>)
        {
          validate_causal_agent(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          auto push_num = [&](const std::string& key, double val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Number(static_cast<double>(val)));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.system.empty()) push_str("system", node.system);
          if (node.temperature != 0.3) push_num("temperature", node.temperature);
          if (!node.endpoint.empty()) push_str("endpoint", node.endpoint);
          if (!node.api_key_env.empty()) push_str("api_key_env", node.api_key_env);
          if (!node.agent_md.empty()) push_str("agent_md", node.agent_md);
          if (!node.sub_agents_json.empty()) push_str("sub_agents", node.sub_agents_json);
          if (!node.forge.empty()) push_str("forge", node.forge);
          if (!node.peer_agent.empty()) push_str("peer_agent", node.peer_agent);
          if (!node.discovery.empty()) push_str("discovery", node.discovery);
          if (!node.scm.empty()) push_str("scm", node.scm);
          if (!node.intervention.empty()) push_str("intervention", node.intervention);
          if (!node.counterfactual.empty()) push_str("counterfactual", node.counterfactual);
          if (!node.bayesian_model.empty()) push_str("bayesian_model", node.bayesian_model);
          if (!node.estimator.empty()) push_str("estimator", node.estimator);
          if (!node.sensitivity.empty()) push_str("sensitivity", node.sensitivity);
          if (!node.data_requirements.empty()) push_str("data_requirements", node.data_requirements);
          if (!node.code_interpreter.empty()) push_str("code_interpreter", node.code_interpreter);
          if (!node.coordinates_with.empty()) push_str_list("coordinates_with", node.coordinates_with);
          if (!node.handoffs.empty()) push_str_list("handoffs", node.handoffs);
          if (!node.role.empty()) push_str("role", node.role);
          if (!node.purpose.empty()) push_str("purpose", node.purpose);
          if (!node.autonomy.empty()) push_str("autonomy", node.autonomy);
          if (!node.budget.empty()) push_str("budget", node.budget);

          causal_agent_defs_.insert(node.name);
          agent_types_[node.name] = AgentKind::Causal;
          chunk_.write_op(vm::OpCode::OP_DEFINE_CAUSAL_AGENT);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: DriftMonitorDecl
        else if constexpr (std::is_same_v<T, DriftMonitorDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.reference_dataset.empty()) push_str("reference_dataset", node.reference_dataset);
          if (!node.data_drift_json.empty()) push_str("data_drift", node.data_drift_json);
          if (!node.concept_drift_json.empty()) push_str("concept_drift", node.concept_drift_json);
          if (!node.prediction_drift_json.empty()) push_str("prediction_drift", node.prediction_drift_json);
          if (!node.alerts_json.empty()) push_str("alerts", node.alerts_json);
          if (!node.root_cause_analysis_json.empty()) push_str("root_cause_analysis", node.root_cause_analysis_json);

          drift_monitor_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DRIFT_MONITOR);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: RetrainingPipelineDecl
        else if constexpr (std::is_same_v<T, RetrainingPipelineDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.triggers_json.empty()) push_str("triggers", node.triggers_json);
          if (!node.data_json.empty()) push_str("data", node.data_json);
          if (!node.training_json.empty()) push_str("training", node.training_json);
          if (!node.validation_json.empty()) push_str("validation", node.validation_json);
          if (!node.deployment_json.empty()) push_str("deployment", node.deployment_json);
          if (!node.notifications_json.empty()) push_str("notifications", node.notifications_json);

          retraining_pipeline_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_RETRAINING_PIPELINE);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: MLDeployStrategyDecl
        else if constexpr (std::is_same_v<T, MLDeployStrategyDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.strategy.empty()) push_str("strategy", node.strategy);
          if (!node.config_json.empty()) push_str("config", node.config_json);

          ml_deploy_strategy_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_ML_DEPLOY_STRATEGY);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: ChampionChallengerDecl
        else if constexpr (std::is_same_v<T, ChampionChallengerDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.champion_json.empty()) push_str("champion", node.champion_json);
          if (!node.challenger_json.empty()) push_str("challenger", node.challenger_json);
          if (!node.evaluation_json.empty()) push_str("evaluation", node.evaluation_json);
          if (!node.promotion_json.empty()) push_str("promotion", node.promotion_json);
          if (!node.rollback_json.empty()) push_str("rollback", node.rollback_json);

          champion_challenger_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_CHAMPION_CHALLENGER);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: ServingInfraDecl
        else if constexpr (std::is_same_v<T, ServingInfraDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.mode.empty()) push_str("mode", node.mode);
          if (!node.platform_json.empty()) push_str("platform", node.platform_json);
          if (!node.sla_json.empty()) push_str("sla", node.sla_json);
          if (!node.cost_json.empty()) push_str("cost", node.cost_json);
          if (!node.health_json.empty()) push_str("health", node.health_json);

          serving_infra_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_SERVING_INFRA);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: TrainingInfraDecl
        else if constexpr (std::is_same_v<T, TrainingInfraDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.compute_tiers_json.empty()) push_str("compute_tiers", node.compute_tiers_json);
          if (!node.selection_json.empty()) push_str("selection", node.selection_json);
          if (!node.cost_tracking_json.empty()) push_str("cost_tracking", node.cost_tracking_json);

          training_infra_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TRAINING_INFRA_MLOPS);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: MLOpsRollbackDecl
        else if constexpr (std::is_same_v<T, MLOpsRollbackDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.auto_triggers_json.empty()) push_str("auto_triggers", node.auto_triggers_json);
          if (!node.strategy_json.empty()) push_str("strategy", node.strategy_json);
          if (!node.post_rollback_json.empty()) push_str("post_rollback", node.post_rollback_json);
          if (!node.recovery_json.empty()) push_str("recovery", node.recovery_json);

          mlops_rollback_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_MLOPS_ROLLBACK);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: MonitoringStackDecl
        else if constexpr (std::is_same_v<T, MonitoringStackDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.evidently_json.empty()) push_str("evidently", node.evidently_json);
          if (!node.prometheus_json.empty()) push_str("prometheus", node.prometheus_json);
          if (!node.whylabs_json.empty()) push_str("whylabs", node.whylabs_json);

          monitoring_stack_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_MONITORING_STACK);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: MLflowConfigDecl
        else if constexpr (std::is_same_v<T, MLflowConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.mcp_server.empty()) push_str("mcp_server", node.mcp_server);
          if (!node.tracking_json.empty()) push_str("tracking", node.tracking_json);
          if (!node.registry_json.empty()) push_str("registry", node.registry_json);
          if (!node.lifecycle_json.empty()) push_str("lifecycle", node.lifecycle_json);

          mlflow_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_MLFLOW_CONFIG);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: BusinessKPITrackerDecl
        else if constexpr (std::is_same_v<T, BusinessKPITrackerDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.kpis_json.empty()) push_str("kpis", node.kpis_json);
          if (!node.report_frequency.empty()) push_str("report_frequency", node.report_frequency);
          if (!node.compare_with.empty()) push_str("compare_with", node.compare_with);

          business_kpi_tracker_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BUSINESS_KPI_TRACKER);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: DatasetVersionDecl
        else if constexpr (std::is_same_v<T, DatasetVersionDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.versioning_tool.empty()) push_str("versioning_tool", node.versioning_tool);
          if (!node.source.empty()) push_str("source", node.source);
          if (!node.query.empty()) push_str("query", node.query);
          if (!node.hash_method.empty()) push_str("hash_method", node.hash_method);
          if (!node.storage.empty()) push_str("storage", node.storage);
          if (!node.lineage_json.empty()) push_str("lineage", node.lineage_json);
          if (!node.schema_validation_json.empty()) push_str("schema_validation", node.schema_validation_json);

          dataset_version_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DATASET_VERSION);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: FeedbackLoopDecl
        else if constexpr (std::is_same_v<T, FeedbackLoopDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.production_metrics_json.empty()) push_str("production_metrics", node.production_metrics_json);
          if (!node.recommendations_json.empty()) push_str("recommendations", node.recommendations_json);
          if (!node.trigger_ds_agent_json.empty()) push_str("trigger_ds_agent", node.trigger_ds_agent_json);

          feedback_loop_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_FEEDBACK_LOOP);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: DecisionEngineDecl
        else if constexpr (std::is_same_v<T, DecisionEngineDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.retrain_policy_json.empty()) push_str("retrain_policy", node.retrain_policy_json);
          if (!node.rollback_policy.empty()) push_str("rollback_policy", node.rollback_policy);
          if (!node.scaling_policy_json.empty()) push_str("scaling_policy", node.scaling_policy_json);
          if (!node.human_in_the_loop_json.empty()) push_str("human_in_the_loop", node.human_in_the_loop_json);

          decision_engine_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DECISION_ENGINE);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: EventBusDecl
        else if constexpr (std::is_same_v<T, EventBusDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.emits_json.empty()) push_str("emits", node.emits_json);
          if (!node.listens_json.empty()) push_str("listens", node.listens_json);
          if (!node.routing_json.empty()) push_str("routing", node.routing_json);

          event_bus_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_EVENT_BUS);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: DriftRCADecl
        else if constexpr (std::is_same_v<T, DriftRCADecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.causal_agent.empty()) push_str("causal_agent", node.causal_agent);
          if (!node.investigation_json.empty()) push_str("investigation", node.investigation_json);
          if (!node.actions_json.empty()) push_str("actions", node.actions_json);

          drift_rca_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DRIFT_RCA);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.2: MLOpsAgentDecl
        else if constexpr (std::is_same_v<T, MLOpsAgentDecl>)
        {
          validate_mlops_agent(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          auto push_num = [&](const std::string& key, double val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Number(static_cast<double>(val)));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.system.empty()) push_str("system", node.system);
          if (node.temperature != 0.2) push_num("temperature", node.temperature);
          if (!node.endpoint.empty()) push_str("endpoint", node.endpoint);
          if (!node.api_key_env.empty()) push_str("api_key_env", node.api_key_env);
          if (!node.agent_md.empty()) push_str("agent_md", node.agent_md);
          if (!node.sub_agents_json.empty()) push_str("sub_agents", node.sub_agents_json);
          if (!node.drift_monitor.empty()) push_str("drift_monitor", node.drift_monitor);
          if (!node.retraining_pipeline.empty()) push_str("retraining_pipeline", node.retraining_pipeline);
          if (!node.deployment_strategy.empty()) push_str("deployment_strategy", node.deployment_strategy);
          if (!node.champion_challenger.empty()) push_str("champion_challenger", node.champion_challenger);
          if (!node.serving_infra.empty()) push_str("serving_infra", node.serving_infra);
          if (!node.training_infra.empty()) push_str("training_infra", node.training_infra);
          if (!node.rollback_policy.empty()) push_str("rollback_policy", node.rollback_policy);
          if (!node.monitoring_stack.empty()) push_str("monitoring_stack", node.monitoring_stack);
          if (!node.mlflow.empty()) push_str("mlflow", node.mlflow);
          if (!node.business_kpi_tracker.empty()) push_str("business_kpi_tracker", node.business_kpi_tracker);
          if (!node.feedback_loop.empty()) push_str("feedback_loop", node.feedback_loop);
          if (!node.decision_engine.empty()) push_str("decision_engine", node.decision_engine);
          if (!node.event_bus.empty()) push_str("event_bus", node.event_bus);
          if (!node.coordinates_with.empty()) push_str_list("coordinates_with", node.coordinates_with);
          if (!node.handoffs.empty()) push_str_list("handoffs", node.handoffs);
          if (!node.role.empty()) push_str("role", node.role);
          if (!node.purpose.empty()) push_str("purpose", node.purpose);
          if (!node.autonomy.empty()) push_str("autonomy", node.autonomy);
          if (!node.budget.empty()) push_str("budget", node.budget);

          mlops_agent_defs_.insert(node.name);
          agent_types_[node.name] = AgentKind::MLOps;
          chunk_.write_op(vm::OpCode::OP_DEFINE_MLOPS_AGENT);
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: RequirementsElicitationDecl (sub-type 0)
        else if constexpr (std::is_same_v<T, RequirementsElicitationDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.stakeholders_json.empty()) push_str("stakeholders", node.stakeholders_json);
          if (!node.methods_json.empty()) push_str("methods", node.methods_json);
          if (!node.output_json.empty()) push_str("output", node.output_json);

          requirements_elicitation_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(0);  // sub-type 0 = RequirementsElicitation
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: BRDGeneratorDecl (sub-type 1)
        else if constexpr (std::is_same_v<T, BRDGeneratorDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.project_json.empty()) push_str("project", node.project_json);
          if (!node.objectives_json.empty()) push_str("objectives", node.objectives_json);
          if (!node.scope_json.empty()) push_str("scope", node.scope_json);
          if (!node.benefits_json.empty()) push_str("benefits", node.benefits_json);
          if (!node.constraints_json.empty()) push_str("constraints", node.constraints_json);
          if (!node.assumptions_json.empty()) push_str("assumptions", node.assumptions_json);
          if (!node.risks_json.empty()) push_str("risks", node.risks_json);
          if (!node.output_json.empty()) push_str("output", node.output_json);

          brd_generator_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(1);  // sub-type 1 = BRDGenerator
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: FunctionalSpecDecl (sub-type 2)
        else if constexpr (std::is_same_v<T, FunctionalSpecDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.data_requirements_json.empty()) push_str("data_requirements", node.data_requirements_json);
          if (!node.etl_requirements_json.empty()) push_str("etl_requirements", node.etl_requirements_json);
          if (!node.ml_requirements_json.empty()) push_str("ml_requirements", node.ml_requirements_json);
          if (!node.analytics_requirements_json.empty()) push_str("analytics_requirements", node.analytics_requirements_json);

          functional_spec_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(2);  // sub-type 2 = FunctionalSpec
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: NonfunctionalSpecDecl (sub-type 3)
        else if constexpr (std::is_same_v<T, NonfunctionalSpecDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.performance_json.empty()) push_str("performance", node.performance_json);
          if (!node.reliability_json.empty()) push_str("reliability", node.reliability_json);
          if (!node.security_json.empty()) push_str("security", node.security_json);
          if (!node.scalability_json.empty()) push_str("scalability", node.scalability_json);
          if (!node.data_quality_json.empty()) push_str("data_quality", node.data_quality_json);
          if (!node.maintainability_json.empty()) push_str("maintainability", node.maintainability_json);

          nonfunctional_spec_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(3);  // sub-type 3 = NonfunctionalSpec
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: AcceptanceCriteriaGenDecl (sub-type 4)
        else if constexpr (std::is_same_v<T, AcceptanceCriteriaGenDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_bool = [&](const std::string& key, bool val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Bool(val));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.patterns_json.empty()) push_str("patterns", node.patterns_json);
          push_bool("auto_generate", node.auto_generate);
          push_bool("review_required", node.review_required);
          if (!node.quality_json.empty()) push_str("quality", node.quality_json);

          acceptance_criteria_gen_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(4);  // sub-type 4 = AcceptanceCriteriaGen
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: DataRequirementsBADecl (sub-type 5)
        else if constexpr (std::is_same_v<T, DataRequirementsBADecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.source_to_target_json.empty()) push_str("source_to_target", node.source_to_target_json);
          if (!node.target_schema_json.empty()) push_str("target_schema", node.target_schema_json);
          if (!node.quality_rules_json.empty()) push_str("quality_rules", node.quality_rules_json);

          data_requirements_ba_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(5);  // sub-type 5 = DataRequirementsBA
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: ImpactAnalysisBADecl (sub-type 6)
        else if constexpr (std::is_same_v<T, ImpactAnalysisBADecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.upstream_json.empty()) push_str("upstream", node.upstream_json);
          if (!node.downstream_json.empty()) push_str("downstream", node.downstream_json);
          if (!node.change_scenarios_json.empty()) push_str("change_scenarios", node.change_scenarios_json);
          if (!node.lineage_json.empty()) push_str("lineage", node.lineage_json);

          impact_analysis_ba_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(6);  // sub-type 6 = ImpactAnalysisBA
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: TraceabilityMatrixDecl (sub-type 7)
        else if constexpr (std::is_same_v<T, TraceabilityMatrixDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.entries_json.empty()) push_str("entries", node.entries_json);
          if (!node.auto_trace_json.empty()) push_str("auto_trace", node.auto_trace_json);
          if (!node.reports_json.empty()) push_str("reports", node.reports_json);

          traceability_matrix_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(7);  // sub-type 7 = TraceabilityMatrix
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: ETLRequirementSpecDecl (sub-type 8)
        else if constexpr (std::is_same_v<T, ETLRequirementSpecDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.pipeline_json.empty()) push_str("pipeline", node.pipeline_json);
          if (!node.feature_groups_json.empty()) push_str("feature_groups", node.feature_groups_json);
          if (!node.quality_gates_json.empty()) push_str("quality_gates", node.quality_gates_json);
          if (!node.upstream_dependencies.empty()) push_str("upstream_dependencies", node.upstream_dependencies);
          if (!node.downstream_consumers.empty()) push_str("downstream_consumers", node.downstream_consumers);

          etl_requirement_spec_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(8);  // sub-type 8 = ETLRequirementSpec
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: MLRequirementSpecDecl (sub-type 9)
        else if constexpr (std::is_same_v<T, MLRequirementSpecDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.problem_json.empty()) push_str("problem", node.problem_json);
          if (!node.success_criteria_json.empty()) push_str("success_criteria", node.success_criteria_json);
          if (!node.feature_requirements_json.empty()) push_str("feature_requirements", node.feature_requirements_json);
          if (!node.serving_json.empty()) push_str("serving", node.serving_json);
          if (!node.explainability_json.empty()) push_str("explainability", node.explainability_json);
          if (!node.monitoring_json.empty()) push_str("monitoring", node.monitoring_json);

          ml_requirement_spec_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(9);  // sub-type 9 = MLRequirementSpec
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: GovernanceRequirementSpecDecl (sub-type 10)
        else if constexpr (std::is_same_v<T, GovernanceRequirementSpecDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.data_classification_json.empty()) push_str("data_classification", node.data_classification_json);
          if (!node.access_requirements_json.empty()) push_str("access_requirements", node.access_requirements_json);
          if (!node.compliance_json.empty()) push_str("compliance", node.compliance_json);
          if (!node.quality_sla_json.empty()) push_str("quality_sla", node.quality_sla_json);

          governance_requirement_spec_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(10);  // sub-type 10 = GovernanceRequirementSpec
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: AnalyticsRequirementSpecDecl (sub-type 11)
        else if constexpr (std::is_same_v<T, AnalyticsRequirementSpecDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.reports_json.empty()) push_str("reports", node.reports_json);
          if (!node.kpi_definitions_json.empty()) push_str("kpi_definitions", node.kpi_definitions_json);

          analytics_requirement_spec_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(11);  // sub-type 11 = AnalyticsRequirementSpec
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: StakeholderAnalysisDecl (sub-type 12)
        else if constexpr (std::is_same_v<T, StakeholderAnalysisDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.stakeholders_json.empty()) push_str("stakeholders", node.stakeholders_json);
          if (!node.raci_matrix_json.empty()) push_str("raci_matrix", node.raci_matrix_json);

          stakeholder_analysis_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(12);  // sub-type 12 = StakeholderAnalysis
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: UserStoryGeneratorDecl (sub-type 13)
        else if constexpr (std::is_same_v<T, UserStoryGeneratorDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.source.empty()) push_str("source", node.source);
          if (!node.epics_json.empty()) push_str("epics", node.epics_json);
          if (!node.stories_json.empty()) push_str("stories", node.stories_json);
          if (!node.generation_json.empty()) push_str("generation", node.generation_json);

          user_story_generator_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(13);  // sub-type 13 = UserStoryGenerator
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: ScopeManagementDecl (sub-type 14)
        else if constexpr (std::is_same_v<T, ScopeManagementDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.bcar_json.empty()) push_str("bcar", node.bcar_json);
          if (!node.change_management_json.empty()) push_str("change_management", node.change_management_json);

          scope_management_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(14);  // sub-type 14 = ScopeManagement
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: ChangeImpactAnalyzerDecl (sub-type 15)
        else if constexpr (std::is_same_v<T, ChangeImpactAnalyzerDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.analysis_json.empty()) push_str("analysis", node.analysis_json);
          if (!node.output_json.empty()) push_str("output", node.output_json);

          change_impact_analyzer_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(15);  // sub-type 15 = ChangeImpactAnalyzer
          chunk_.write_byte(field_count);
        }
        // v0.9.8.3: DataBAAgentDecl (sub-type 16)
        else if constexpr (std::is_same_v<T, DataBAAgentDecl>)
        {
          validate_databa_agent(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          auto push_num = [&](const std::string& key, double val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Number(val));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.system.empty()) push_str("system", node.system);
          if (node.temperature != 0.3) push_num("temperature", node.temperature);
          if (!node.endpoint.empty()) push_str("endpoint", node.endpoint);
          if (!node.api_key_env.empty()) push_str("api_key_env", node.api_key_env);
          if (!node.agent_md.empty()) push_str("agent_md", node.agent_md);
          if (!node.downstream_agents_json.empty()) push_str("downstream_agents", node.downstream_agents_json);
          if (!node.elicitation.empty()) push_str("elicitation", node.elicitation);
          if (!node.brd.empty()) push_str("brd", node.brd);
          if (!node.functional_spec.empty()) push_str("functional_spec", node.functional_spec);
          if (!node.nfr_spec.empty()) push_str("nfr_spec", node.nfr_spec);
          if (!node.data_requirements.empty()) push_str("data_requirements", node.data_requirements);
          if (!node.impact_analysis.empty()) push_str("impact_analysis", node.impact_analysis);
          if (!node.traceability.empty()) push_str("traceability", node.traceability);
          if (!node.etl_spec.empty()) push_str("etl_spec", node.etl_spec);
          if (!node.ml_spec.empty()) push_str("ml_spec", node.ml_spec);
          if (!node.governance_spec.empty()) push_str("governance_spec", node.governance_spec);
          if (!node.analytics_spec.empty()) push_str("analytics_spec", node.analytics_spec);
          if (!node.stakeholders.empty()) push_str("stakeholders", node.stakeholders);
          if (!node.user_stories.empty()) push_str("user_stories", node.user_stories);
          if (!node.scope.empty()) push_str("scope", node.scope);
          if (!node.coordinates_with.empty()) push_str_list("coordinates_with", node.coordinates_with);
          if (!node.handoffs.empty()) push_str_list("handoffs", node.handoffs);
          if (!node.role.empty()) push_str("role", node.role);
          if (!node.purpose.empty()) push_str("purpose", node.purpose);
          if (!node.autonomy.empty()) push_str("autonomy", node.autonomy);
          if (!node.budget.empty()) push_str("budget", node.budget);

          databa_agent_defs_.insert(node.name);
          agent_types_[node.name] = AgentKind::DataBA;
          chunk_.write_op(vm::OpCode::OP_DEFINE_BA_DECLARATION);
          chunk_.write_byte(16);  // sub-type 16 = DataBAAgent
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: TestStrategyDecl (sub-type 0)
        else if constexpr (std::is_same_v<T, TestStrategyDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.source_spec.empty()) push_str("source_spec", node.source_spec);
          if (!node.source_nfr.empty()) push_str("source_nfr", node.source_nfr);
          if (!node.levels_json.empty()) push_str("levels", node.levels_json);
          if (!node.test_data_json.empty()) push_str("test_data", node.test_data_json);
          if (!node.gates_json.empty()) push_str("gates", node.gates_json);
          if (!node.reporting_json.empty()) push_str("reporting", node.reporting_json);

          test_strategy_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(0);  // sub-type 0 = TestStrategy
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: TestCaseGeneratorDecl (sub-type 1)
        else if constexpr (std::is_same_v<T, TestCaseGeneratorDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.source.empty()) push_str("source", node.source);
          if (!node.generation_json.empty()) push_str("generation", node.generation_json);
          if (!node.output_json.empty()) push_str("output", node.output_json);

          test_case_generator_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(1);  // sub-type 1 = TestCaseGenerator
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: TestCaseDecl (sub-type 2)
        else if constexpr (std::is_same_v<T, TestCaseDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.requirement.empty()) push_str("requirement", node.requirement);
          if (!node.acceptance_criteria.empty()) push_str("acceptance_criteria", node.acceptance_criteria);
          if (!node.type.empty()) push_str("type", node.type);
          if (!node.priority.empty()) push_str("priority", node.priority);
          if (!node.preconditions_json.empty()) push_str("preconditions", node.preconditions_json);
          if (!node.steps_json.empty()) push_str("steps", node.steps_json);
          if (!node.expected_result.empty()) push_str("expected_result", node.expected_result);
          if (!node.automation_json.empty()) push_str("automation", node.automation_json);

          test_case_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(2);  // sub-type 2 = TestCase
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: ETLTestSuiteDecl (sub-type 3)
        else if constexpr (std::is_same_v<T, ETLTestSuiteDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.pipeline.empty()) push_str("pipeline", node.pipeline);
          if (!node.connection.empty()) push_str("connection", node.connection);
          if (!node.tests_json.empty()) push_str("tests", node.tests_json);

          etl_test_suite_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(3);  // sub-type 3 = ETLTestSuite
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: DWTestSuiteDecl (sub-type 4)
        else if constexpr (std::is_same_v<T, DWTestSuiteDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.connection.empty()) push_str("connection", node.connection);
          if (!node.tests_json.empty()) push_str("tests", node.tests_json);

          dw_test_suite_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(4);  // sub-type 4 = DWTestSuite
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: MLTestSuiteDecl (sub-type 5)
        else if constexpr (std::is_same_v<T, MLTestSuiteDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.tests_json.empty()) push_str("tests", node.tests_json);

          ml_test_suite_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(5);  // sub-type 5 = MLTestSuite
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: APITestSuiteDecl (sub-type 6)
        else if constexpr (std::is_same_v<T, APITestSuiteDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.endpoint.empty()) push_str("endpoint", node.endpoint);
          if (!node.auth_json.empty()) push_str("auth", node.auth_json);
          if (!node.tests_json.empty()) push_str("tests", node.tests_json);

          api_test_suite_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(6);  // sub-type 6 = APITestSuite
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: PerformanceTestSuiteDecl (sub-type 7)
        else if constexpr (std::is_same_v<T, PerformanceTestSuiteDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.pipeline_performance_json.empty()) push_str("pipeline_performance", node.pipeline_performance_json);
          if (!node.query_performance_json.empty()) push_str("query_performance", node.query_performance_json);
          if (!node.api_performance_json.empty()) push_str("api_performance", node.api_performance_json);

          performance_test_suite_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(7);  // sub-type 7 = PerformanceTestSuite
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: EdgeCaseTestsDecl (sub-type 8)
        else if constexpr (std::is_same_v<T, EdgeCaseTestsDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.generation_json.empty()) push_str("generation", node.generation_json);
          if (!node.tests_json.empty()) push_str("tests", node.tests_json);

          edge_case_tests_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(8);  // sub-type 8 = EdgeCaseTests
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: SITSuiteDecl (sub-type 9)
        else if constexpr (std::is_same_v<T, SITSuiteDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.scope.empty()) push_str("scope", node.scope);
          if (!node.tests_json.empty()) push_str("tests", node.tests_json);

          sit_suite_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(9);  // sub-type 9 = SITSuite
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: UATSuiteDecl (sub-type 10)
        else if constexpr (std::is_same_v<T, UATSuiteDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.business_validation_json.empty()) push_str("business_validation", node.business_validation_json);
          if (!node.data_quality_uat_json.empty()) push_str("data_quality_uat", node.data_quality_uat_json);

          uat_suite_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(10);  // sub-type 10 = UATSuite
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: RegressionSuiteDecl (sub-type 11)
        else if constexpr (std::is_same_v<T, RegressionSuiteDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.baseline_json.empty()) push_str("baseline", node.baseline_json);
          if (!node.checks_json.empty()) push_str("checks", node.checks_json);
          if (!node.trigger.empty()) push_str("trigger", node.trigger);

          regression_suite_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(11);  // sub-type 11 = RegressionSuite
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: QualityGateDecl (sub-type 12)
        else if constexpr (std::is_same_v<T, QualityGateDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.data_quality_gate_json.empty()) push_str("data_quality_gate", node.data_quality_gate_json);
          if (!node.model_quality_gate_json.empty()) push_str("model_quality_gate", node.model_quality_gate_json);
          if (!node.api_quality_gate_json.empty()) push_str("api_quality_gate", node.api_quality_gate_json);
          if (!node.performance_gate_json.empty()) push_str("performance_gate", node.performance_gate_json);
          if (!node.uat_gate_json.empty()) push_str("uat_gate", node.uat_gate_json);
          if (!node.deployment_decision_json.empty()) push_str("deployment_decision", node.deployment_decision_json);

          quality_gate_test_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(12);  // sub-type 12 = QualityGate
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: TestReportConfigDecl (sub-type 13)
        else if constexpr (std::is_same_v<T, TestReportConfigDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.execution_json.empty()) push_str("execution", node.execution_json);
          if (!node.report_json.empty()) push_str("report", node.report_json);
          if (!node.notify_json.empty()) push_str("notify", node.notify_json);

          test_report_config_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(13);  // sub-type 13 = TestReportConfig
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: DefectManagementDecl (sub-type 14)
        else if constexpr (std::is_same_v<T, DefectManagementDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.on_failure_json.empty()) push_str("on_failure", node.on_failure_json);
          if (!node.rca_json.empty()) push_str("rca", node.rca_json);
          if (!node.tracking_json.empty()) push_str("tracking", node.tracking_json);

          defect_management_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(14);  // sub-type 14 = DefectManagement
          chunk_.write_byte(field_count);
        }
        // v0.9.8.4: DataTestAgentDecl (sub-type 15)
        else if constexpr (std::is_same_v<T, DataTestAgentDecl>)
        {
          validate_datatest_agent(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          auto push_num = [&](const std::string& key, double val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Number(val));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.system.empty()) push_str("system", node.system);
          if (node.temperature != 0.2) push_num("temperature", node.temperature);
          if (!node.agent_md.empty()) push_str("agent_md", node.agent_md);
          if (!node.sub_agents_json.empty()) push_str("sub_agents", node.sub_agents_json);
          if (!node.forge.empty()) push_str("forge", node.forge);
          if (!node.test_strategy.empty()) push_str("test_strategy", node.test_strategy);
          if (!node.test_generator.empty()) push_str("test_generator", node.test_generator);
          if (!node.etl_tests.empty()) push_str("etl_tests", node.etl_tests);
          if (!node.dw_tests.empty()) push_str("dw_tests", node.dw_tests);
          if (!node.ml_tests.empty()) push_str("ml_tests", node.ml_tests);
          if (!node.api_tests.empty()) push_str("api_tests", node.api_tests);
          if (!node.performance_tests.empty()) push_str("performance_tests", node.performance_tests);
          if (!node.edge_tests.empty()) push_str("edge_tests", node.edge_tests);
          if (!node.sit_suite.empty()) push_str("sit_suite", node.sit_suite);
          if (!node.uat_suite.empty()) push_str("uat_suite", node.uat_suite);
          if (!node.regression_suite.empty()) push_str("regression_suite", node.regression_suite);
          if (!node.quality_gate.empty()) push_str("quality_gate", node.quality_gate);
          if (!node.report_config.empty()) push_str("report_config", node.report_config);
          if (!node.defect_mgmt.empty()) push_str("defect_mgmt", node.defect_mgmt);
          if (!node.coordinates_with.empty()) push_str_list("coordinates_with", node.coordinates_with);
          if (!node.handoffs.empty()) push_str_list("handoffs", node.handoffs);
          if (!node.role.empty()) push_str("role", node.role);
          if (!node.purpose.empty()) push_str("purpose", node.purpose);
          if (!node.autonomy.empty()) push_str("autonomy", node.autonomy);
          if (!node.budget.empty()) push_str("budget", node.budget);

          datatest_agent_defs_.insert(node.name);
          agent_types_[node.name] = AgentKind::DataTest;
          chunk_.write_op(vm::OpCode::OP_DEFINE_TEST_DECLARATION);
          chunk_.write_byte(15);  // sub-type 15 = DataTestAgent
          chunk_.write_byte(field_count);
        }
        // ═══════════════════════════════════════════════════════════════
        // v0.9.9: Data Intelligent Orchestrator — consolidated opcode
        // ═══════════════════════════════════════════════════════════════
        // v0.9.9: AgentRegistryDecl (sub-type 0)
        else if constexpr (std::is_same_v<T, AgentRegistryDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.agents_json.empty()) push_str("agents", node.agents_json);

          agent_registry_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(0);  // sub-type 0 = AgentRegistry
          chunk_.write_byte(field_count);
        }
        // v0.9.9: AgentContractsDecl (sub-type 1)
        else if constexpr (std::is_same_v<T, AgentContractsDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.contracts_json.empty()) push_str("contracts", node.contracts_json);

          agent_contracts_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(1);  // sub-type 1 = AgentContracts
          chunk_.write_byte(field_count);
        }
        // v0.9.9: RACIMatrixDecl (sub-type 2)
        else if constexpr (std::is_same_v<T, RACIMatrixDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.tasks_json.empty()) push_str("tasks", node.tasks_json);

          raci_matrix_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(2);  // sub-type 2 = RACIMatrix
          chunk_.write_byte(field_count);
        }
        // v0.9.9: TaskUnderstandingDecl (sub-type 3)
        else if constexpr (std::is_same_v<T, TaskUnderstandingDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.intent_classifier_json.empty()) push_str("intent_classifier", node.intent_classifier_json);
          if (!node.detail_extraction_json.empty()) push_str("detail_extraction", node.detail_extraction_json);

          task_understanding_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(3);  // sub-type 3 = TaskUnderstanding
          chunk_.write_byte(field_count);
        }
        // v0.9.9: TaskDecomposerDecl (sub-type 4)
        else if constexpr (std::is_same_v<T, TaskDecomposerDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.strategy_json.empty()) push_str("strategy", node.strategy_json);
          if (!node.output_json.empty()) push_str("output", node.output_json);

          task_decomposer_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(4);  // sub-type 4 = TaskDecomposer
          chunk_.write_byte(field_count);
        }
        // v0.9.9: CrewFormationDecl (sub-type 5)
        else if constexpr (std::is_same_v<T, CrewFormationDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.strategy_json.empty()) push_str("strategy", node.strategy_json);

          crew_formation_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(5);  // sub-type 5 = CrewFormation
          chunk_.write_byte(field_count);
        }
        // v0.9.9: PatternSelectorDecl (sub-type 6)
        else if constexpr (std::is_same_v<T, PatternSelectorDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.patterns_json.empty()) push_str("patterns", node.patterns_json);
          if (!node.selection_json.empty()) push_str("selection", node.selection_json);

          pattern_selector_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(6);  // sub-type 6 = PatternSelector
          chunk_.write_byte(field_count);
        }
        // v0.9.9: ExecutionManagerDIODecl (sub-type 7)
        else if constexpr (std::is_same_v<T, ExecutionManagerDIODecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.modes_json.empty()) push_str("modes", node.modes_json);
          if (!node.resources_json.empty()) push_str("resources", node.resources_json);
          if (!node.monitoring_json.empty()) push_str("monitoring", node.monitoring_json);

          execution_manager_dio_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(7);  // sub-type 7 = ExecutionManagerDIO
          chunk_.write_byte(field_count);
        }
        // v0.9.9: DIOStateMachineDecl (sub-type 8)
        else if constexpr (std::is_same_v<T, DIOStateMachineDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.states_json.empty()) push_str("states", node.states_json);
          if (!node.transitions_json.empty()) push_str("transitions", node.transitions_json);
          if (!node.persistence_json.empty()) push_str("persistence", node.persistence_json);

          dio_state_machine_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(8);  // sub-type 8 = DIOStateMachine
          chunk_.write_byte(field_count);
        }
        // v0.9.9: DIOErrorHandlingDecl (sub-type 9)
        else if constexpr (std::is_same_v<T, DIOErrorHandlingDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.strategies_json.empty()) push_str("strategies", node.strategies_json);
          if (!node.self_healing_json.empty()) push_str("self_healing", node.self_healing_json);

          dio_error_handling_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(9);  // sub-type 9 = DIOErrorHandling
          chunk_.write_byte(field_count);
        }
        // v0.9.9: ResultSynthesizerDecl (sub-type 10)
        else if constexpr (std::is_same_v<T, ResultSynthesizerDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.synthesis_json.empty()) push_str("synthesis", node.synthesis_json);
          if (!node.delivery_json.empty()) push_str("delivery", node.delivery_json);

          result_synthesizer_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(10);  // sub-type 10 = ResultSynthesizer
          chunk_.write_byte(field_count);
        }
        // v0.9.9: InfrastructureProfileDecl (sub-type 11)
        else if constexpr (std::is_same_v<T, InfrastructureProfileDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.data_warehouse_json.empty()) push_str("data_warehouse", node.data_warehouse_json);
          if (!node.data_lake_json.empty()) push_str("data_lake", node.data_lake_json);
          if (!node.databases_json.empty()) push_str("databases", node.databases_json);
          if (!node.streaming_json.empty()) push_str("streaming", node.streaming_json);
          if (!node.data_science_json.empty()) push_str("data_science", node.data_science_json);
          if (!node.governance_json.empty()) push_str("governance", node.governance_json);
          if (!node.cicd_json.empty()) push_str("cicd", node.cicd_json);

          infrastructure_profile_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(11);  // sub-type 11 = InfrastructureProfile
          chunk_.write_byte(field_count);
        }
        // v0.9.9: RoleFrameworkDecl (sub-type 12)
        else if constexpr (std::is_same_v<T, RoleFrameworkDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.roles_json.empty()) push_str("roles", node.roles_json);
          if (!node.dio_role_json.empty()) push_str("dio_role", node.dio_role_json);

          role_framework_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(12);  // sub-type 12 = RoleFramework
          chunk_.write_byte(field_count);
        }
        // v0.9.9: DelegationProtocolDecl (sub-type 13)
        else if constexpr (std::is_same_v<T, DelegationProtocolDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.delegation_json.empty()) push_str("delegation", node.delegation_json);

          delegation_protocol_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(13);  // sub-type 13 = DelegationProtocol
          chunk_.write_byte(field_count);
        }
        // v0.9.9: DIOAccountabilityDecl (sub-type 14)
        else if constexpr (std::is_same_v<T, DIOAccountabilityDecl>)
        {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.escalation_json.empty()) push_str("escalation", node.escalation_json);

          dio_accountability_defs_.insert(node.name);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(14);  // sub-type 14 = DIOAccountability
          chunk_.write_byte(field_count);
        }
        // v0.9.9: DIOAgentDecl (sub-type 15)
        else if constexpr (std::is_same_v<T, DIOAgentDecl>)
        {
          validate_dio_agent(node);
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& key, const std::string& val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::String(val.c_str(), val.size()));
            field_count++;
          };
          auto push_str_list = [&](const std::string& n, const std::vector<std::string>& list) {
            std::string joined;
            for (size_t i = 0; i < list.size(); ++i) { if (i > 0) joined += ","; joined += list[i]; }
            push_str(n, joined);
          };
          auto push_num = [&](const std::string& key, double val) {
            chunk_.emit_constant(vm::Value::String(key.c_str(), key.size()));
            chunk_.emit_constant(vm::Value::Number(val));
            field_count++;
          };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.system.empty()) push_str("system", node.system);
          if (node.temperature != 0.2) push_num("temperature", node.temperature);
          if (!node.mode.empty()) push_str("mode", node.mode);
          if (!node.task.empty()) push_str("task", node.task);
          if (!node.agent_md.empty()) push_str("agent_md", node.agent_md);
          if (!node.infrastructure.empty()) push_str("infrastructure", node.infrastructure);
          if (!node.agent_registry.empty()) push_str("agent_registry", node.agent_registry);
          if (!node.raci_matrix.empty()) push_str("raci_matrix", node.raci_matrix);
          if (!node.pattern_selector.empty()) push_str("pattern_selector", node.pattern_selector);
          if (!node.crew_formation.empty()) push_str("crew_formation", node.crew_formation);
          if (!node.execution_manager.empty()) push_str("execution_manager", node.execution_manager);
          if (!node.state_machine.empty()) push_str("state_machine", node.state_machine);
          if (!node.error_handling.empty()) push_str("error_handling", node.error_handling);
          if (!node.result_synthesizer.empty()) push_str("result_synthesizer", node.result_synthesizer);
          if (!node.managed_agents_json.empty()) push_str("managed_agents", node.managed_agents_json);
          if (!node.guardrails_json.empty()) push_str("guardrails", node.guardrails_json);
          if (!node.coordinates_with.empty()) push_str_list("coordinates_with", node.coordinates_with);
          if (!node.role.empty()) push_str("role", node.role);
          if (!node.purpose.empty()) push_str("purpose", node.purpose);
          if (!node.autonomy.empty()) push_str("autonomy", node.autonomy);
          if (!node.budget.empty()) push_str("budget", node.budget);

          dio_agent_defs_.insert(node.name);
          agent_types_[node.name] = AgentKind::DIO;
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(15);  // sub-type 15 = DIOAgent
          chunk_.write_byte(field_count);
        }
        // ═══ v1.0: OWASP Security, MCP, Cloud, Eval, Special Agents ═══
        // All v1.0 constructs follow the same emit pattern: push fields, emit opcode, write field_count
#define V10_EMIT_SIMPLE(DeclType, OpCodeVal, FieldName) \
        else if constexpr (std::is_same_v<T, DeclType>) { \
          uint8_t field_count = 0; \
          auto push_str = [&](const std::string& k, const std::string& v) { \
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size())); \
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size())); \
            field_count++; }; \
          push_str("name", node.name); \
          if (!node.FieldName.empty()) push_str(#FieldName, node.FieldName); \
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION); \
          chunk_.write_byte(15); /* reuse DIO consolidated pattern for now */ \
          chunk_.write_byte(field_count); \
        }

        V10_EMIT_SIMPLE(GoalIntegrityDecl, OP_DEFINE_GOAL_INTEGRITY, verification_json)
        V10_EMIT_SIMPLE(ToolValidatorDecl, OP_DEFINE_TOOL_VALIDATOR, rate_limits_json)
        V10_EMIT_SIMPLE(AgentIdentityDecl, OP_DEFINE_AGENT_IDENTITY, scope_json)
        V10_EMIT_SIMPLE(SupplyChainPolicyDecl, OP_DEFINE_SUPPLY_CHAIN_POLICY, agent_md_signing_json)
        V10_EMIT_SIMPLE(CodeSandboxDecl, OP_DEFINE_CODE_SANDBOX, resources_json)
        V10_EMIT_SIMPLE(MemoryIntegrityDecl, OP_DEFINE_MEMORY_INTEGRITY, provenance_json)
        V10_EMIT_SIMPLE(MessageSecurityDecl, OP_DEFINE_MESSAGE_SECURITY, signing_json)
        V10_EMIT_SIMPLE(CircuitBreakerV10Decl, OP_DEFINE_CIRCUIT_BREAKER_DECL, isolation_json)
        V10_EMIT_SIMPLE(HumanGateDecl, OP_DEFINE_HUMAN_GATE, workflow_json)
        V10_EMIT_SIMPLE(AgentAttestationDecl, OP_DEFINE_AGENT_ATTESTATION, baseline_json)
        V10_EMIT_SIMPLE(MCPAllowlistDecl, OP_DEFINE_MCP_ALLOWLIST, servers_json)
        V10_EMIT_SIMPLE(ToolPinningDecl, OP_DEFINE_TOOL_PINNING, method)
        V10_EMIT_SIMPLE(ContextGuardDecl, OP_DEFINE_CONTEXT_GUARD, cross_task_sharing)
        V10_EMIT_SIMPLE(AIBOMConfigDecl, OP_DEFINE_AIBOM_CONFIG, components_json)
        V10_EMIT_SIMPLE(GymEvaluatorDecl, OP_DEFINE_GYM_EVALUATOR, graders_json)
        V10_EMIT_SIMPLE(GatewayDecl, OP_DEFINE_GATEWAY, routes_json)
        V10_EMIT_SIMPLE(ModelRouterDecl, OP_DEFINE_MODEL_ROUTER, routes_json)
        V10_EMIT_SIMPLE(MarketplaceV10Decl, OP_DEFINE_MARKETPLACE_DECL, package_format_json)

        // ═══ v1.1: NeamOS Foundation ═══
        // Simple keywords use consolidated DIO handler with sub_type=16
#define V11_EMIT_SIMPLE(DeclType, FieldName) \
        else if constexpr (std::is_same_v<T, DeclType>) { \
          uint8_t field_count = 0; \
          auto push_str = [&](const std::string& k, const std::string& v) { \
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size())); \
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size())); \
            field_count++; }; \
          push_str("name", node.name); \
          if (!node.FieldName.empty()) push_str(#FieldName, node.FieldName); \
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION); \
          chunk_.write_byte(16); /* v1.1 consolidated sub-type */ \
          chunk_.write_byte(field_count); \
        }

        V11_EMIT_SIMPLE(KnowledgeCardDecl, fields_json)
        V11_EMIT_SIMPLE(ContextAssemblyDecl, target_agent_ref)
        V11_EMIT_SIMPLE(AgentPersonaDecl, personality_json)
        V11_EMIT_SIMPLE(LocaleConfigDecl, string_table)
        V11_EMIT_SIMPLE(GovernanceRuleDecl, condition_json)
        V11_EMIT_SIMPLE(AgentAdapterDecl, capabilities_json)
        V11_EMIT_SIMPLE(BlueprintDecl, parameters_json)

        // ═══ v1.2: NeamProd ═══
#define V12_EMIT_SIMPLE(DeclType, FieldName) \
        else if constexpr (std::is_same_v<T, DeclType>) { \
          uint8_t field_count = 0; \
          auto push_str = [&](const std::string& k, const std::string& v) { \
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size())); \
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size())); \
            field_count++; }; \
          push_str("name", node.name); \
          if (!node.FieldName.empty()) push_str(#FieldName, node.FieldName); \
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION); \
          chunk_.write_byte(20); /* v1.2 consolidated sub-type */ \
          chunk_.write_byte(field_count); \
        }

        V12_EMIT_SIMPLE(PluginDecl, hooks_json)
        V12_EMIT_SIMPLE(SessionServiceDecl, connection_json)
        V12_EMIT_SIMPLE(EvalTestDecl, criteria_json)
        V12_EMIT_SIMPLE(EvalSetDecl, thresholds_json)
        V12_EMIT_SIMPLE(ArtifactStoreDecl, metadata_json)
        V12_EMIT_SIMPLE(StreamConfigDecl, voice_json)
        V12_EMIT_SIMPLE(A2AConfigDecl, agent_card_json)

        // ═══ v1.3: NeamLab — Auto Research Agent ═══
#define V13_EMIT_SIMPLE(DeclType, FieldName) \
        else if constexpr (std::is_same_v<T, DeclType>) { \
          uint8_t field_count = 0; \
          auto push_str = [&](const std::string& k, const std::string& v) { \
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size())); \
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size())); \
            field_count++; }; \
          push_str("name", node.name); \
          if (!node.FieldName.empty()) push_str(#FieldName, node.FieldName); \
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION); \
          chunk_.write_byte(21); /* v1.3 consolidated sub-type */ \
          chunk_.write_byte(field_count); \
        }

        V13_EMIT_SIMPLE(ProgramDecl, mission)
        V13_EMIT_SIMPLE(ExperimentLoopDecl, until_json)
        V13_EMIT_SIMPLE(MetricExtractorDecl, pattern)

        else if constexpr (std::is_same_v<T, ResearchAgentDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          auto push_num = [&](const std::string& k, double v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::Number(v));
            field_count++; };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          push_num("temperature", node.temperature);
          if (!node.budget.empty()) push_str("budget", node.budget);
          if (!node.program_ref.empty()) push_str("program", node.program_ref);
          if (!node.metric_ref.empty()) push_str("metric", node.metric_ref);
          if (!node.experiment_log_ref.empty()) push_str("experiment_log", node.experiment_log_ref);
          if (!node.mutable_artifacts_json.empty()) push_str("mutable_artifacts", node.mutable_artifacts_json);
          if (!node.immutable_artifacts_json.empty()) push_str("immutable_artifacts", node.immutable_artifacts_json);
          if (!node.iteration_budget_json.empty()) push_str("iteration_budget", node.iteration_budget_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(22); /* sub-type 22 = ResearchAgent */
          chunk_.write_byte(field_count);
        }

        // ═══ v1.4: NeamWiki — Compiled LLM Wiki ═══
        else if constexpr (std::is_same_v<T, WikiDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(23); /* sub-type 23 = Wiki */
          chunk_.write_byte(field_count);
        }
        // ═══ v1.4.5: NeamHarness — unified harness declaration ═══
        else if constexpr (std::is_same_v<T, HarnessDecl>) {
          // v1.4.5 Phase 1 validation: H-001 harness has ≥1 sub_agent.
          // The parser stores all fields as JSON; parse it here to inspect.
          if (!node.fields_json.empty()) {
            try {
              auto j = nlohmann::json::parse(node.fields_json);
              // H-001: sub_agents must be declared and non-empty
              auto it = j.find("sub_agents");
              if (it == j.end() || !it->is_object() || it->empty()) {
                throw std::runtime_error(
                    "H-001: harness '" + node.name + "' has no sub_agents");
              }
            } catch (const nlohmann::json::parse_error&) {
              // Fall through: malformed JSON from parser; let bytecode layer
              // surface the issue. Don't block compile on parse-side artifacts.
            }
          } else {
            throw std::runtime_error(
                "H-001: harness '" + node.name + "' has no body");
          }

          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(25); /* sub-type 25 = Harness */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, HandoffDecl>) {
          // v1.4.5 Phase 1 validation:
          //   H-015: handoff must declare schema_version (FR-HF-6)
          if (!node.fields_json.empty()) {
            try {
              auto j = nlohmann::json::parse(node.fields_json);
              if (!j.contains("schema_version")) {
                throw std::runtime_error(
                    "H-015: handoff '" + node.name +
                    "' must declare schema_version (e.g., \"1.0.0\")");
              }
            } catch (const nlohmann::json::parse_error&) {
              // Malformed JSON; let VM surface the issue.
            }
          } else {
            throw std::runtime_error(
                "H-015: handoff '" + node.name + "' has no body");
          }

          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(26); /* sub-type 26 = Handoff */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, ToolRegistryDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(27); /* sub-type 27 = ToolRegistry */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, AssertionRegistryDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(28); /* sub-type 28 = AssertionRegistry */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, HarnessBenchmarkDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(29); /* sub-type 29 = HarnessBenchmark */
          chunk_.write_byte(field_count);
        }
        // ═══ v1.5: NeamEvolve — Self-Evolving Agent ═══
        else if constexpr (std::is_same_v<T, EvolveAgentDecl>) {
          // E-001: belief required.  E-003: handoff required.
          // E-013: at least one role:"evaluator" forge agent in sub_agents.
          // E-016: safety.program required.
          if (!node.fields_json.empty()) {
            try {
              auto j = nlohmann::json::parse(node.fields_json);
              std::string belief_ref = j.value("belief", std::string{});
              if (belief_ref.empty())
                throw std::runtime_error("E-001: evolve agent '" + node.name + "' missing required `belief:` reference");
              std::string handoff_ref = j.value("handoff", std::string{});
              if (handoff_ref.empty())
                throw std::runtime_error("E-003: evolve agent '" + node.name + "' missing required `handoff:` reference");
              auto sa = j.find("sub_agents");
              if (sa == j.end() || !sa->is_object() || sa->empty())
                throw std::runtime_error("E-013: evolve agent '" + node.name + "' must declare non-empty sub_agents");
              // E-016: safety.program required
              auto safety = j.find("safety");
              std::string program_ref;
              std::string human_gate_ref;
              bool allow_design_ops = false;
              if (safety != j.end() && safety->is_object()) {
                program_ref     = safety->value("program", std::string{});
                human_gate_ref  = safety->value("human_gate", std::string{});
                allow_design_ops= safety->value("allow_design_ops", false);
              }
              if (program_ref.empty())
                throw std::runtime_error("E-016: evolve agent '" + node.name + "' missing safety.program reference (alignment anchor)");
              // E-010: if design ops enabled, human_gate is mandatory.
              if (allow_design_ops && human_gate_ref.empty())
                throw std::runtime_error("E-010: evolve agent '" + node.name + "' enables design ops but is missing safety.human_gate reference");
            } catch (const nlohmann::json::parse_error&) {
              // malformed JSON — let VM surface it
            }
          } else {
            throw std::runtime_error("E-001: evolve agent '" + node.name + "' has no body");
          }
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(31); /* sub-type 31 = EvolveAgent */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, BeliefDecl>) {
          // E-002 (constraints ref existence — checked at link/runtime since target may not be loaded yet),
          // E-007 (rollback when trigger != manual), E-012 (trigger value), E-015 (max_revisions range).
          if (!node.fields_json.empty()) {
            try {
              auto j = nlohmann::json::parse(node.fields_json);
              std::string initial = j.value("initial", std::string{});
              if (initial.empty())
                throw std::runtime_error("P-BL-001: belief '" + node.name + "' missing `initial:`");
              std::string constraints = j.value("constraints", std::string{});
              if (constraints.empty())
                throw std::runtime_error("E-002: belief '" + node.name + "' missing `constraints:` (must reference an assertion_registry)");
              std::string trigger = j.value("revision_trigger", std::string{"manual"});
              if (trigger != "every_N_runs" && trigger != "performance_plateau" && trigger != "manual")
                throw std::runtime_error("E-012: belief '" + node.name + "' revision_trigger must be one of {every_N_runs, performance_plateau, manual}");
              bool rollback = j.value("rollback", true);
              if (!rollback && trigger != "manual")
                throw std::runtime_error("E-007: belief '" + node.name + "' rollback must be true when revision_trigger != \"manual\"");
              int max_rev = j.value("max_revisions_per_session", 10);
              if (max_rev < 1 || max_rev > 100)
                throw std::runtime_error("E-015: belief '" + node.name + "' max_revisions_per_session must be in [1,100]");
            } catch (const nlohmann::json::parse_error&) {
              // malformed JSON — let VM surface it
            }
          } else {
            throw std::runtime_error("P-BL-001: belief '" + node.name + "' has no body");
          }
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(32); /* sub-type 32 = Belief */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, SkillLibraryDecl>) {
          // E-005: verify block required.
          // E-006: verify.sandbox MUST be true if allow_runtime_acquisition.
          // E-014: deprecate.after_failures in [1,1000].
          if (!node.fields_json.empty()) {
            try {
              auto j = nlohmann::json::parse(node.fields_json);
              if (!j.contains("verify") || !j["verify"].is_object())
                throw std::runtime_error("E-005: skill_library '" + node.name + "' missing `verify:` block");
              auto verify = j["verify"];
              bool sandbox = verify.value("sandbox", true);
              bool allow_acq = j.value("allow_runtime_acquisition", true);
              if (allow_acq && !sandbox)
                throw std::runtime_error("E-006: skill_library '" + node.name + "' verify.sandbox must be true when allow_runtime_acquisition is true");
              if (j.contains("deprecate") && j["deprecate"].is_object()) {
                int threshold = j["deprecate"].value("after_failures", 5);
                if (threshold < 1 || threshold > 1000)
                  throw std::runtime_error("E-014: skill_library '" + node.name + "' deprecate.after_failures must be in [1,1000]");
              }
            } catch (const nlohmann::json::parse_error&) {
              /* tolerated */
            }
          } else {
            throw std::runtime_error("E-005: skill_library '" + node.name + "' has no body");
          }
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(33); /* sub-type 33 = SkillLibrary */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, CurriculumDecl>) {
          // E-009: difficulty_metric, advance_threshold, fallback_threshold required + ordering.
          if (!node.fields_json.empty()) {
            try {
              auto j = nlohmann::json::parse(node.fields_json);
              std::string mode = j.value("mode", std::string{"auto"});
              if (mode != "auto" && mode != "co_evolve" && mode != "manual" && mode != "eval_set_iterator")
                throw std::runtime_error("E-009: curriculum '" + node.name + "' invalid mode (must be auto|co_evolve|manual|eval_set_iterator)");
              std::string metric = j.value("difficulty_metric", std::string{});
              if (metric.empty())
                throw std::runtime_error("E-009: curriculum '" + node.name + "' missing `difficulty_metric:`");
              if (!j.contains("advance_threshold") || !j.contains("fallback_threshold"))
                throw std::runtime_error("E-009: curriculum '" + node.name + "' missing thresholds (advance_threshold + fallback_threshold)");
              double adv = j.value("advance_threshold", 0.8);
              double fb  = j.value("fallback_threshold", 0.4);
              if (!(fb < adv))
                throw std::runtime_error("E-009: curriculum '" + node.name + "' fallback_threshold must be < advance_threshold");
            } catch (const nlohmann::json::parse_error&) {
              /* tolerated */
            }
          } else {
            throw std::runtime_error("E-009: curriculum '" + node.name + "' has no body");
          }
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(34); /* sub-type 34 = Curriculum */
          chunk_.write_byte(field_count);
        }
        // ═══ v1.6: NeamMesh — Agentic Process Automation (sub-types 35-39) ═══
        else if constexpr (std::is_same_v<T, ProcessDecl>) {
          // P-FR-PR-1: process must declare `start` and at least one `tasks` entry.
          // P-FR-PR-2: every task referenced must exist in `tasks`.
          // (Cross-decl reference resolution happens at runtime via process_registry;
          //  here we only enforce single-decl shape rules — keep validation cheap.)
          if (node.fields_json.empty()) {
            throw std::runtime_error("P-FR-PR-1: process '" + node.name + "' has no body");
          }
          try {
            auto j = nlohmann::json::parse(node.fields_json);
            if (!j.contains("start") || !j["start"].is_string())
              throw std::runtime_error("P-FR-PR-1: process '" + node.name + "' missing required `start:` task ref");
            if (!j.contains("tasks") || !j["tasks"].is_object() || j["tasks"].empty())
              throw std::runtime_error("P-FR-PR-1: process '" + node.name + "' must declare non-empty `tasks:`");
            // ADR-005 graph well-formedness: start must be a key in tasks (or in events).
            std::string start = j.value("start", std::string{});
            bool start_found = j["tasks"].contains(start);
            if (!start_found && j.contains("events") && j["events"].is_object())
              start_found = j["events"].contains(start);
            if (!start_found)
              throw std::runtime_error("P-FR-PR-2: process '" + node.name + "' start ref '" + start + "' not found in tasks/events");
          } catch (const nlohmann::json::parse_error&) {
            /* tolerated — runtime will surface */
          }
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(35); /* sub-type 35 = Process */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, TaskDecl>) {
          // P-FR-TK-1: task must declare `agent`.  scope/input/output optional.
          // Capability monotonicity (task.scope ⊆ agent.tool_registry.scoping) is
          // verified at runtime when the agent record is resolved (ADR-005).
          if (node.fields_json.empty()) {
            throw std::runtime_error("P-FR-TK-1: task '" + node.name + "' has no body");
          }
          try {
            auto j = nlohmann::json::parse(node.fields_json);
            if (!j.contains("agent") || !j["agent"].is_string() || j.value("agent", std::string{}).empty())
              throw std::runtime_error("P-FR-TK-1: task '" + node.name + "' missing required `agent:` reference");
            // P-FR-TK-3: retries (if present) must be in [0,10].
            if (j.contains("retries")) {
              int r = j.value("retries", 0);
              if (r < 0 || r > 10)
                throw std::runtime_error("P-FR-TK-3: task '" + node.name + "' retries must be in [0,10]");
            }
          } catch (const nlohmann::json::parse_error&) { /* tolerated */ }
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(36); /* sub-type 36 = Task */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, DecisionDecl>) {
          // P-FR-DC-1: decision must declare `expr` (the condition source) and `branches`.
          // P-FR-DC-2: branches MUST contain at least two keys.
          if (node.fields_json.empty()) {
            throw std::runtime_error("P-FR-DC-1: decision '" + node.name + "' has no body");
          }
          try {
            auto j = nlohmann::json::parse(node.fields_json);
            if (!j.contains("expr") || !j["expr"].is_string())
              throw std::runtime_error("P-FR-DC-1: decision '" + node.name + "' missing required `expr:`");
            if (!j.contains("branches") || !j["branches"].is_object() || j["branches"].size() < 2)
              throw std::runtime_error("P-FR-DC-2: decision '" + node.name + "' must declare ≥ 2 branches");
          } catch (const nlohmann::json::parse_error&) { /* tolerated */ }
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(37); /* sub-type 37 = Decision */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, EventDecl>) {
          // P-FR-EV-1: event must declare `type` ∈ {start, intermediate, end, timer, signal, message}.
          if (node.fields_json.empty()) {
            throw std::runtime_error("P-FR-EV-1: event '" + node.name + "' has no body");
          }
          try {
            auto j = nlohmann::json::parse(node.fields_json);
            std::string et = j.value("type", std::string{});
            if (et.empty())
              throw std::runtime_error("P-FR-EV-1: event '" + node.name + "' missing required `type:`");
            static const char* kAllowed[] = {"start","intermediate","end","timer","signal","message"};
            bool ok = false;
            for (const char* a : kAllowed) if (et == a) { ok = true; break; }
            if (!ok)
              throw std::runtime_error("P-FR-EV-1: event '" + node.name +
                  "' type must be one of {start,intermediate,end,timer,signal,message} (got '" + et + "')");
          } catch (const nlohmann::json::parse_error&) { /* tolerated */ }
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(38); /* sub-type 38 = Event */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, PoolDecl>) {
          // P-FR-PL-1: pool must declare `lanes` (BPMN swim-lane participants).
          if (node.fields_json.empty()) {
            throw std::runtime_error("P-FR-PL-1: pool '" + node.name + "' has no body");
          }
          try {
            auto j = nlohmann::json::parse(node.fields_json);
            if (!j.contains("lanes") || !j["lanes"].is_object() || j["lanes"].empty())
              throw std::runtime_error("P-FR-PL-1: pool '" + node.name + "' must declare non-empty `lanes:`");
          } catch (const nlohmann::json::parse_error&) { /* tolerated */ }
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.fields_json.empty()) push_str("fields", node.fields_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(39); /* sub-type 39 = Pool */
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, WikiAgentDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          auto push_num = [&](const std::string& k, double v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::Number(v));
            field_count++; };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          push_num("temperature", node.temperature);
          if (!node.budget.empty()) push_str("budget", node.budget);
          if (!node.wikis_json.empty()) push_str("wikis", node.wikis_json);
          if (!node.operations_json.empty()) push_str("operations", node.operations_json);
          if (!node.research_config_json.empty()) push_str("research_config", node.research_config_json);
          if (!node.lint_policies_json.empty()) push_str("lint_policies", node.lint_policies_json);
          if (!node.graph_config_json.empty()) push_str("graph_config", node.graph_config_json);
          if (!node.output_formats_json.empty()) push_str("output_formats", node.output_formats_json);
          if (!node.governance_json.empty()) push_str("governance", node.governance_json);
          if (!node.plugin_hooks_json.empty()) push_str("plugin_hooks", node.plugin_hooks_json);
          if (!node.knowledge_cards_json.empty()) push_str("knowledge_cards", node.knowledge_cards_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(24); /* sub-type 24 = WikiAgent */
          chunk_.write_byte(field_count);
        }

        // v1.0: Special agents
        else if constexpr (std::is_same_v<T, SecuritySentinelAgentDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          auto push_num = [&](const std::string& k, double v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::Number(v));
            field_count++; };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          if (node.temperature != 0.2) push_num("temperature", node.temperature);
          if (!node.budget.empty()) push_str("budget", node.budget);
          if (!node.monitors_json.empty()) push_str("monitors", node.monitors_json);
          if (!node.actions_json.empty()) push_str("actions", node.actions_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(15);
          chunk_.write_byte(field_count);
          // name registered via VM dispatch
        }
        else if constexpr (std::is_same_v<T, ProtocolBridgeAgentDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.budget.empty()) push_str("budget", node.budget);
          if (!node.protocols_json.empty()) push_str("protocols", node.protocols_json);
          if (!node.firewall_json.empty()) push_str("firewall", node.firewall_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(15);
          chunk_.write_byte(field_count);
          // name registered via VM dispatch
        }
        else if constexpr (std::is_same_v<T, CostGuardianAgentDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          if (!node.budget.empty()) push_str("budget", node.budget);
          if (!node.tracking_json.empty()) push_str("tracking", node.tracking_json);
          if (!node.optimization_json.empty()) push_str("optimization", node.optimization_json);
          if (!node.alerts_json.empty()) push_str("alerts", node.alerts_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(15);
          chunk_.write_byte(field_count);
          // name registered via VM dispatch
        }

        // v1.1: NeamOS Foundation agents
        else if constexpr (std::is_same_v<T, KnowledgeWeaverAgentDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          auto push_num = [&](const std::string& k, double v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::Number(v));
            field_count++; };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          push_num("temperature", node.temperature);
          if (!node.budget.empty()) push_str("budget", node.budget);
          if (!node.fabric_json.empty()) push_str("fabric", node.fabric_json);
          if (!node.monitors_json.empty()) push_str("monitors", node.monitors_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(17); // sub-type 17 = KnowledgeWeaver
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, AdaptAgentDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          auto push_num = [&](const std::string& k, double v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::Number(v));
            field_count++; };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          push_num("temperature", node.temperature);
          if (!node.budget.empty()) push_str("budget", node.budget);
          if (!node.monitors_json.empty()) push_str("monitors", node.monitors_json);
          if (!node.proposals_json.empty()) push_str("proposals", node.proposals_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(18); // sub-type 18 = AdaptAgent
          chunk_.write_byte(field_count);
        }
        else if constexpr (std::is_same_v<T, StorytellerAgentDecl>) {
          uint8_t field_count = 0;
          auto push_str = [&](const std::string& k, const std::string& v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::String(v.c_str(), v.size()));
            field_count++; };
          auto push_num = [&](const std::string& k, double v) {
            chunk_.emit_constant(vm::Value::String(k.c_str(), k.size()));
            chunk_.emit_constant(vm::Value::Number(v));
            field_count++; };
          push_str("name", node.name);
          if (!node.provider.empty()) push_str("provider", node.provider);
          if (!node.model.empty()) push_str("model", node.model);
          push_num("temperature", node.temperature);
          if (!node.budget.empty()) push_str("budget", node.budget);
          if (!node.sub_agents_json.empty()) push_str("sub_agents", node.sub_agents_json);
          if (!node.safety_json.empty()) push_str("safety", node.safety_json);
          chunk_.write_op(vm::OpCode::OP_DEFINE_DIO_DECLARATION);
          chunk_.write_byte(19); // sub-type 19 = Storyteller
          chunk_.write_byte(field_count);
        }

#undef V13_EMIT_SIMPLE
#undef V12_EMIT_SIMPLE
#undef V11_EMIT_SIMPLE
#undef V10_EMIT_SIMPLE
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
            case BinaryOp::In:
              chunk_.write_op(OpCode::OP_CONTAINS);
              break;
            case BinaryOp::NotIn:
              chunk_.write_op(OpCode::OP_CONTAINS);
              chunk_.write_op(OpCode::OP_NOT);
              break;
            default:
              throw std::runtime_error(
                  "Unhandled binary operator: " +
                  std::to_string(static_cast<int>(node.op)));
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
        // v0.7.0: Index assignment (x[i] = val)
        else if constexpr (std::is_same_v<T, IndexAssignExpr>)
        {
          emit_expression(*node.base);
          emit_expression(*node.index);
          emit_expression(*node.value);
          chunk_.write_op(OpCode::OP_SET_INDEX);
        }
        // v0.7.0: Tuple literal
        else if constexpr (std::is_same_v<T, TupleExpr>)
        {
          for (const auto& element : node.elements)
          {
            emit_expression(*element);
          }
          chunk_.write_op(OpCode::OP_BUILD_TUPLE);
          chunk_.write_short(static_cast<uint16_t>(node.elements.size()));
        }
        // v0.7.0: F-string
        else if constexpr (std::is_same_v<T, FStringExpr>)
        {
          for (const auto& segment : node.segments)
          {
            if (segment.is_expr)
            {
              emit_expression(*segment.expr);
            }
            else
            {
              chunk_.emit_constant(vm::Value::String(segment.text.c_str(), segment.text.size()));
            }
          }
          chunk_.write_op(OpCode::OP_FORMAT_STRING);
          chunk_.write_short(static_cast<uint16_t>(node.segments.size()));
        }
        // v0.7.0: Slice expression
        else if constexpr (std::is_same_v<T, SliceExpr>)
        {
          emit_expression(*node.base);
          uint8_t flags = 0;
          if (node.start)
          {
            emit_expression(*node.start);
            flags |= 0x01;
          }
          if (node.end)
          {
            emit_expression(*node.end);
            flags |= 0x02;
          }
          if (node.step)
          {
            emit_expression(*node.step);
            flags |= 0x04;
          }
          chunk_.write_op(OpCode::OP_SLICE);
          chunk_.write_byte(flags);
        }
        // v0.7.1: Copy-with expression
        else if constexpr (std::is_same_v<T, CopyWithExpr>)
        {
          emit_expression(*node.object);
          for (const auto& [name, value] : node.overrides)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, name)));
            emit_expression(*value);
          }
          chunk_.write_op(OpCode::OP_COPY_WITH);
          chunk_.write_byte(static_cast<uint8_t>(node.overrides.size()));
        }
        // v0.7.1: Named construction
        else if constexpr (std::is_same_v<T, NamedConstructExpr>)
        {
          // Push field name/value pairs, then OP_CONSTRUCT_NAMED
          for (const auto& [name, value] : node.fields)
          {
            chunk_.write_op(OpCode::OP_CONST);
            chunk_.write_short(static_cast<uint16_t>(
                emit_string_constant(chunk_, name)));
            emit_expression(*value);
          }
          chunk_.write_op(OpCode::OP_CONSTRUCT_NAMED);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.type_name)));
          chunk_.write_byte(static_cast<uint8_t>(node.fields.size()));
        }
        // v0.7.1: Property assignment (p.x = val)
        else if constexpr (std::is_same_v<T, SetPropertyExpr>)
        {
          emit_expression(*node.object);
          emit_expression(*node.value);
          chunk_.write_op(OpCode::OP_SET_PROPERTY);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.name)));
        }
        // v0.7.1 Phase 2: Match expression
        else if constexpr (std::is_same_v<T, MatchExpr>)
        {
          // Compile match subject onto stack.
          emit_expression(*node.subject);

          // Track the subject as a hidden local so binding slots align correctly.
          auto subject_slot = static_cast<uint16_t>(locals_.size());
          locals_.push_back(Local{"$match_subject", scope_depth_});

          std::vector<std::size_t> exit_jumps;
          auto arm_locals_start = locals_.size();  // includes $match_subject

          for (std::size_t i = 0; i < node.arms.size(); ++i)
          {
            const auto& arm = node.arms[i];
            bool is_last = (i == node.arms.size() - 1);

            if (arm.pattern_name == "_")
            {
              // Wildcard — always matches. Pop subject, emit body.
              chunk_.write_op(OpCode::OP_POP);
              emit_expression(*arm.body);
              // Body result replaces the subject on the stack.
              if (!is_last)
              {
                exit_jumps.push_back(emit_jump(chunk_, OpCode::OP_JUMP));
              }
              // Restore locals for next arm (though wildcard is usually last)
              locals_.resize(arm_locals_start);
            }
            else
            {
              // Copy subject for testing (GET_LOCAL pushes a copy)
              chunk_.write_op(OpCode::OP_GET_LOCAL);
              chunk_.write_short(subject_slot);

              // Push variant name constant for MATCH_VARIANT
              chunk_.write_op(OpCode::OP_CONST);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, arm.pattern_name)));

              chunk_.write_op(OpCode::OP_MATCH_VARIANT);
              chunk_.write_short(static_cast<uint16_t>(
                  emit_string_constant(chunk_, arm.pattern_name)));
              chunk_.write_byte(static_cast<uint8_t>(arm.bindings.size()));
              auto skip_jump = emit_jump(chunk_, OpCode::OP_JUMP_IF_FALSE);

              // Match succeeded — pop the true
              chunk_.write_op(OpCode::OP_POP);

              // Register bindings as locals (fields are on stack above subject)
              for (const auto& binding_name : arm.bindings)
              {
                locals_.push_back(Local{binding_name, scope_depth_});
              }

              if (arm.guard)
              {
                emit_expression(*arm.guard);
                auto guard_skip = emit_jump(chunk_, OpCode::OP_JUMP_IF_FALSE);
                chunk_.write_op(OpCode::OP_POP);

                // Evaluate body
                emit_expression(*arm.body);

                // Stack: [..., subject, field0,...,fieldN, result]
                // Write result into subject slot, then clean up
                chunk_.write_op(OpCode::OP_SET_LOCAL);
                chunk_.write_short(subject_slot);
                chunk_.write_op(OpCode::OP_POP);  // pop result dup
                for (std::size_t b = 0; b < arm.bindings.size(); ++b)
                  chunk_.write_op(OpCode::OP_POP);  // pop bindings
                // Stack: [..., result(in subject slot)]

                exit_jumps.push_back(emit_jump(chunk_, OpCode::OP_JUMP));

                // Guard failed — pop guard result + bindings, try next arm
                patch_jump(chunk_, guard_skip);
                chunk_.write_op(OpCode::OP_POP);  // pop false guard
                for (std::size_t b = 0; b < arm.bindings.size(); ++b)
                  chunk_.write_op(OpCode::OP_POP);  // pop bindings
              }
              else
              {
                // Evaluate body
                emit_expression(*arm.body);

                // Stack: [..., subject, field0,...,fieldN, result]
                // Write result into subject slot, then clean up
                chunk_.write_op(OpCode::OP_SET_LOCAL);
                chunk_.write_short(subject_slot);
                chunk_.write_op(OpCode::OP_POP);  // pop result dup
                for (std::size_t b = 0; b < arm.bindings.size(); ++b)
                  chunk_.write_op(OpCode::OP_POP);  // pop bindings
                // Stack: [..., result(in subject slot)]

                exit_jumps.push_back(emit_jump(chunk_, OpCode::OP_JUMP));
              }

              // Patch skip: match failed — pop false
              patch_jump(chunk_, skip_jump);
              chunk_.write_op(OpCode::OP_POP);

              // Restore locals for next arm
              locals_.resize(arm_locals_start);
            }
          }

          // Remove $match_subject local. The result value stays on the stack.
          locals_.pop_back();

          // Patch all exit jumps to here
          for (auto jump : exit_jumps)
          {
            patch_jump(chunk_, jump);
          }
        }
        // v0.8 Phase 8: Spawn expression
        else if constexpr (std::is_same_v<T, SpawnExpr>)
        {
          // Push agent name as string constant
          chunk_.write_op(OpCode::OP_CONST);
          chunk_.write_short(static_cast<uint16_t>(
              emit_string_constant(chunk_, node.agent_name)));
          // Push config arg (first arg or nil)
          if (!node.arguments.empty())
          {
            emit_expression(*node.arguments[0]);
          }
          else
          {
            chunk_.write_op(OpCode::OP_NIL);
          }
          chunk_.write_op(OpCode::OP_SPAWN_AGENT);
        }
      },
      expr.node);
}
// v0.8 Phase 1: NeamClaw trait helpers

bool Compiler::is_neamclaw_trait(const std::string& name) const
{
  static const std::unordered_set<std::string> claw_traits = {
      "Channelable", "Schedulable", "Sandboxable",
      "Monitorable", "Orchestrable", "Searchable"};
  return claw_traits.count(name) > 0;
}

void Compiler::validate_neamclaw_trait_compat(const std::string& trait,
                                              const std::string& type)
{
  auto it = agent_types_.find(type);
  if (it == agent_types_.end())
  {
    // Not an agent type — allow (could be a struct implementing the trait)
    return;
  }

  AgentKind kind = it->second;

  // Channelable and Schedulable are claw-only
  if (trait == "Channelable" || trait == "Schedulable")
  {
    if (kind != AgentKind::Claw)
    {
      throw std::runtime_error(
          "Trait '" + trait + "' can only be implemented by claw agents, but '" +
          type + "' is " +
          (kind == AgentKind::Forge ? "a forge agent" : "a stateless agent"));
    }
  }

  // Orchestrable on stateless agents is a warning (likely mistake)
  if (trait == "Orchestrable" && kind == AgentKind::Stateless)
  {
    compiler_warning("Trait 'Orchestrable' on stateless agent '" + type +
                     "' — consider using a claw or forge agent instead");
  }
}

void Compiler::compiler_warning(const std::string& message) const
{
  std::fprintf(stderr, "[neamc warning] %s\n", message.c_str());
}

// ============================================================================
// v0.9.2: Migration Agent serialization helpers
// ============================================================================

std::string Compiler::serialize_wave_config(const WaveConfig& cfg)
{
  nlohmann::json j;
  j["mode"] = cfg.mode;
  j["max_tables_per_wave"] = cfg.max_tables_per_wave;
  j["max_parallel_extractions"] = cfg.max_parallel_extractions;
  if (!cfg.manual_waves.empty())
  {
    auto waves_arr = nlohmann::json::array();
    for (const auto& [name, tables] : cfg.manual_waves)
      waves_arr.push_back({{"name", name}, {"tables", tables}});
    j["manual_waves"] = waves_arr;
  }
  if (!cfg.dependencies.empty())
    j["dependencies"] = cfg.dependencies;
  return j.dump();
}

std::string Compiler::serialize_movement_config(const MovementConfig& cfg)
{
  nlohmann::json j;
  switch (cfg.strategy)
  {
    case DataMovementStrategy::FULL_DUMP:     j["strategy"] = "full_dump"; break;
    case DataMovementStrategy::INCREMENTAL:   j["strategy"] = "incremental"; break;
    case DataMovementStrategy::PARALLEL_RUN:  j["strategy"] = "parallel_run"; break;
    case DataMovementStrategy::TRICKLE:       j["strategy"] = "trickle"; break;
    case DataMovementStrategy::BLUE_GREEN:    j["strategy"] = "blue_green"; break;
  }
  j["extraction_threads"] = cfg.extraction_threads;
  j["load_threads"] = cfg.load_threads;
  j["partition_strategy"] = cfg.partition_strategy;
  j["staging_format"] = cfg.staging_format;
  j["checkpoint_interval"] = cfg.checkpoint_interval;
  if (!cfg.cdc.mechanism.empty())
  {
    j["cdc"]["mechanism"] = cfg.cdc.mechanism;
    j["cdc"]["tool"] = cfg.cdc.tool;
    j["cdc"]["lag_threshold"] = cfg.cdc.lag_threshold;
    j["cdc"]["lag_critical"] = cfg.cdc.lag_critical;
  }
  return j.dump();
}

std::string Compiler::serialize_schema_translation_config(const SchemaTranslationConfig& cfg)
{
  nlohmann::json j;
  j["type_mapping"] = cfg.type_mapping;
  j["stored_procedures"] = cfg.stored_procedures;
  j["views"] = cfg.views;
  j["materialized_views"] = cfg.materialized_views;
  j["indexes"] = cfg.indexes;
  j["sequences"] = cfg.sequences;
  if (cfg.oracle_specific)
  {
    auto& ps = *cfg.oracle_specific;
    j["oracle_specific"]["empty_string_handling"] = ps.empty_string_handling;
    j["oracle_specific"]["date_to_timestamp"] = ps.date_to_timestamp;
    j["oracle_specific"]["clob_threshold"] = ps.clob_threshold;
    j["oracle_specific"]["number_no_precision"] = ps.number_no_precision;
  }
  if (cfg.teradata_specific)
  {
    auto& ps = *cfg.teradata_specific;
    j["teradata_specific"]["empty_string_handling"] = ps.empty_string_handling;
    j["teradata_specific"]["date_to_timestamp"] = ps.date_to_timestamp;
    j["teradata_specific"]["clob_threshold"] = ps.clob_threshold;
    j["teradata_specific"]["number_no_precision"] = ps.number_no_precision;
  }
  return j.dump();
}

std::string Compiler::serialize_validation_config(const ValidationConfig& cfg)
{
  nlohmann::json j;
  j["mode"] = cfg.mode;
  j["reconciliation"]["row_counts"] = cfg.reconciliation.row_counts;
  j["reconciliation"]["column_aggregates"] = cfg.reconciliation.column_aggregates;
  j["reconciliation"]["hash_comparison"] = cfg.reconciliation.hash_comparison;
  j["reconciliation"]["statistical_distribution"] = cfg.reconciliation.statistical_distribution;
  j["reconciliation"]["boundary_values"] = cfg.reconciliation.boundary_values;
  j["reconciliation"]["golden_queries"] = cfg.reconciliation.golden_queries;
  j["reconciliation"]["referential_integrity"] = cfg.reconciliation.referential_integrity;
  j["tolerances"]["financial_columns"] = cfg.tolerances.financial_columns;
  j["tolerances"]["floating_point"] = cfg.tolerances.floating_point;
  j["tolerances"]["timestamp_precision"] = cfg.tolerances.timestamp_precision;
  if (!cfg.golden_queries.empty())
    j["golden_queries_list"] = cfg.golden_queries;
  j["continuous"]["enabled"] = cfg.continuous_enabled;
  j["continuous"]["interval"] = cfg.continuous_interval;
  return j.dump();
}

std::string Compiler::serialize_cutover_config(const CutoverConfig& cfg)
{
  nlohmann::json j;
  switch (cfg.strategy)
  {
    case CutoverStrategy::BIG_BANG:    j["strategy"] = "big_bang"; break;
    case CutoverStrategy::BLUE_GREEN:  j["strategy"] = "blue_green"; break;
    case CutoverStrategy::CANARY:      j["strategy"] = "canary"; break;
    case CutoverStrategy::TRICKLE:     j["strategy"] = "trickle"; break;
  }
  j["rollback"]["window"] = cfg.rollback.window;
  j["rollback"]["auto_trigger"] = cfg.rollback.auto_trigger;
  j["rollback"]["trigger_conditions"] = cfg.rollback.trigger_conditions;
  return j.dump();
}

std::string Compiler::serialize_self_heal_config(const SelfHealMigrationConfig& cfg)
{
  nlohmann::json j;
  j["enabled"] = cfg.enabled;
  j["missing_rows"] = cfg.missing_rows;
  j["duplicate_rows"] = cfg.duplicate_rows;
  j["type_conversion_errors"] = cfg.type_conversion_errors;
  j["network_failures"] = cfg.network_failures;
  j["checkpoint_resume"] = cfg.checkpoint_resume;
  j["guardrails"]["max_auto_fix_rows"] = cfg.guardrails.max_auto_fix_rows;
  j["guardrails"]["max_auto_fix_percentage"] = cfg.guardrails.max_auto_fix_percentage;
  j["guardrails"]["max_retries_per_table"] = cfg.guardrails.max_retries_per_table;
  j["guardrails"]["require_dry_run"] = cfg.guardrails.require_dry_run;
  j["guardrails"]["audit_all_remediations"] = cfg.guardrails.audit_all_remediations;
  return j.dump();
}

std::string Compiler::serialize_assessment_config(const AssessmentConfig& cfg)
{
  nlohmann::json j;
  j["auto_discover"] = cfg.auto_discover;
  j["profile_data"] = cfg.profile_data;
  j["risk_analysis"] = cfg.risk_analysis;
  j["report_format"] = cfg.report_format;
  return j.dump();
}

std::string Compiler::serialize_governance_migration_config(const GovernanceMigrationConfig& cfg)
{
  nlohmann::json j;
  j["preserve_classification"] = cfg.preserve_classification;
  j["pii_detection"] = cfg.pii_detection;
  j["staging_region"] = cfg.staging_region;
  j["target_region"] = cfg.target_region;
  j["log_all_sql"] = cfg.log_all_sql;
  j["log_all_data_movement"] = cfg.log_all_data_movement;
  j["audit_retention"] = cfg.audit_retention;
  return j.dump();
}

// ============================================================================
// v0.9.2: Migration Agent compile-time validation
// ============================================================================

void Compiler::validate_migration_agent(const MigrationAgentDecl& decl)
{
  auto err = [&](const std::string& msg) {
    throw std::runtime_error(msg);
  };

  // MIG-001: must declare both source and target
  if (decl.source.empty())
    err("MIG-001: migration agent '" + decl.name + "' must declare 'source'.");
  if (decl.target.empty())
    err("MIG-001: migration agent '" + decl.name + "' must declare 'target'.");

  // MIG-005: rollback window must be > 0
  if (decl.cutover.rollback.window.empty() || decl.cutover.rollback.window == "0")
    err("MIG-005: migration agent '" + decl.name + "' cutover.rollback.window must be > 0.");

  // MIG-006: validation must include at least row_counts
  if (!decl.validation.reconciliation.row_counts)
    err("MIG-006: migration agent '" + decl.name + "' validation must include row_counts.");

  // MIG-007: guardrails required if self_heal enabled
  if (decl.self_heal.enabled && decl.self_heal.guardrails.max_auto_fix_rows <= 0)
    err("MIG-007: migration agent '" + decl.name + "' self_heal.guardrails.max_auto_fix_rows must be > 0.");

  // MIG-008: budget must be declared
  if (!decl.budget)
    err("MIG-008: migration agent '" + decl.name + "' must declare 'budget'.");

  // MIG-009: staging required for full_dump or incremental
  if (!decl.staging &&
      (decl.movement.strategy == DataMovementStrategy::FULL_DUMP ||
       decl.movement.strategy == DataMovementStrategy::INCREMENTAL))
    err("MIG-009: migration agent '" + decl.name + "' requires 'staging' for full_dump/incremental movement.");

  // MIG-010: governance audit trail
  if (decl.governance && !decl.governance->log_all_data_movement)
    err("MIG-010: migration agent '" + decl.name + "' governance must have log_all_data_movement=true.");

  // MIG-012: wave dependencies must be DAG
  if (!decl.waves.dependencies.empty())
  {
    if (!validate_wave_dag(decl.waves.dependencies))
      err("MIG-012: migration agent '" + decl.name + "' wave dependencies contain a cycle.");
  }

  // MIG-013: financial_columns tolerance must be "exact"
  if (decl.validation.tolerances.financial_columns != "exact")
    err("MIG-013: migration agent '" + decl.name + "' tolerances.financial_columns must be 'exact'.");

  // MIG-015: blue_green cutover + full_dump warning
  if (decl.cutover.strategy == CutoverStrategy::BLUE_GREEN &&
      decl.movement.strategy == DataMovementStrategy::FULL_DUMP)
    compiler_warning("MIG-015: migration agent '" + decl.name +
                     "' blue_green cutover with full_dump requires a second full load.");
}

bool Compiler::validate_wave_dag(
    const std::unordered_map<std::string, std::vector<std::string>>& deps)
{
  std::unordered_map<std::string, int> in_degree;
  std::unordered_set<std::string> all_nodes;

  for (const auto& [node, preds] : deps)
  {
    all_nodes.insert(node);
    if (in_degree.find(node) == in_degree.end()) in_degree[node] = 0;
    for (const auto& pred : preds)
    {
      all_nodes.insert(pred);
      in_degree[node]++;
    }
  }
  for (const auto& n : all_nodes)
  {
    if (in_degree.find(n) == in_degree.end()) in_degree[n] = 0;
  }

  std::queue<std::string> q;
  for (const auto& [n, deg] : in_degree)
  {
    if (deg == 0) q.push(n);
  }

  int visited = 0;
  while (!q.empty())
  {
    auto cur = q.front(); q.pop();
    visited++;
    for (const auto& [node, preds] : deps)
    {
      for (const auto& pred : preds)
      {
        if (pred == cur)
        {
          in_degree[node]--;
          if (in_degree[node] == 0) q.push(node);
        }
      }
    }
  }
  return visited == static_cast<int>(all_nodes.size());
}

// ============================================================================
// v0.9.3: DataOps Agent compile-time validation
// ============================================================================

void Compiler::validate_dataops_agent(const DataOpsAgentDecl& decl)
{
  auto err = [&](const std::string& msg) {
    throw std::runtime_error(msg);
  };

  // OPS-001: must declare at least one platform
  if (decl.platforms.empty())
    err("OPS-001: dataops agent '" + decl.name + "' must declare at least one 'platforms' reference.");

  // OPS-002: must declare incident_policy
  if (!decl.incident_policy)
    err("OPS-002: dataops agent '" + decl.name + "' must declare 'incident_policy'.");

  // OPS-003: referenced platforms must be defined
  for (const auto& ref : decl.platforms)
  {
    if (platform_defs_.find(ref) == platform_defs_.end())
      err("OPS-003: dataops agent '" + decl.name + "' references undefined platform '" + ref + "'.");
  }

  // OPS-004: referenced schedulers must be defined
  for (const auto& ref : decl.schedulers)
  {
    if (scheduler_defs_.find(ref) == scheduler_defs_.end())
      err("OPS-004: dataops agent '" + decl.name + "' references undefined scheduler '" + ref + "'.");
  }

  // OPS-005: referenced audit_tables must be defined
  for (const auto& ref : decl.audit_tables)
  {
    if (audit_table_defs_.find(ref) == audit_table_defs_.end())
      err("OPS-005: dataops agent '" + decl.name + "' references undefined audit_table '" + ref + "'.");
  }

  // OPS-006: referenced log_sources must be defined
  for (const auto& ref : decl.log_sources)
  {
    if (log_source_defs_.find(ref) == log_source_defs_.end())
      err("OPS-006: dataops agent '" + decl.name + "' references undefined log_source '" + ref + "'.");
  }

  // OPS-007: referenced correlations must be defined
  for (const auto& ref : decl.correlations)
  {
    if (correlation_defs_.find(ref) == correlation_defs_.end())
      err("OPS-007: dataops agent '" + decl.name + "' references undefined correlation '" + ref + "'.");
  }

  // OPS-008: referenced incident_policy must be defined
  if (decl.incident_policy && incident_policy_defs_.find(*decl.incident_policy) == incident_policy_defs_.end())
    err("OPS-008: dataops agent '" + decl.name + "' references undefined incident_policy '" + *decl.incident_policy + "'.");

  // OPS-009: budget must be declared
  if (!decl.budget)
    err("OPS-009: dataops agent '" + decl.name + "' must declare 'budget'.");

  // OPS-010: continuous mode requires at least one monitoring source
  if (decl.mode == DataOpsMode::CONTINUOUS &&
      decl.schedulers.empty() && decl.audit_tables.empty() && decl.log_sources.empty())
    err("OPS-010: dataops agent '" + decl.name + "' in continuous mode must have at least one monitoring source.");
}

// ============================================================================
// v0.9.3: DataOps serialization helpers
// ============================================================================

std::string Compiler::serialize_audit_column_map(const AuditColumnMap& map)
{
  nlohmann::json j;
  j["job_id"] = map.job_id;
  j["timestamp"] = map.timestamp;
  j["status"] = map.status;
  {
    nlohmann::json sv;
    sv["success"] = map.status_values.success;
    sv["failure"] = map.status_values.failure;
    sv["running"] = map.status_values.running;
    sv["skipped"] = map.status_values.skipped;
    j["status_values"] = sv;
  }
  if (map.rows_in) j["rows_in"] = *map.rows_in;
  if (map.rows_out) j["rows_out"] = *map.rows_out;
  if (map.error) j["error"] = *map.error;
  if (map.duration) j["duration"] = *map.duration;
  return j.dump();
}

std::string Compiler::serialize_audit_anomalies(const AuditAnomalyConfig& cfg)
{
  nlohmann::json j;
  j["row_count_drop"] = cfg.row_count_drop;
  j["row_count_spike"] = cfg.row_count_spike;
  j["duration_spike"] = cfg.duration_spike;
  j["failure_rate"] = cfg.failure_rate;
  j["zero_rows_consecutive"] = cfg.zero_rows_consecutive;
  return j.dump();
}

std::string Compiler::serialize_log_alerts(const LogAlertConfig& cfg)
{
  nlohmann::json j;
  j["query_timeout"] = cfg.query_timeout;
  j["warehouse_credit_spike"] = cfg.warehouse_credit_spike;
  if (!cfg.ora_errors.empty()) j["ora_errors"] = cfg.ora_errors;
  j["dead_tuples_ratio"] = cfg.dead_tuples_ratio;
  j["oom_errors"] = cfg.oom_errors;
  j["failed_logins"] = cfg.failed_logins;
  j["full_table_scans"] = cfg.full_table_scans;
  if (cfg.replication_lag) j["replication_lag"] = *cfg.replication_lag;
  return j.dump();
}

std::string Compiler::serialize_health_checks(const PlatformHealthConfig& cfg)
{
  nlohmann::json j;
  j["storage_growth"] = cfg.storage_growth;
  j["warehouse_utilization"] = cfg.warehouse_utilization;
  j["partition_health"] = cfg.partition_health;
  j["clustering_health"] = cfg.clustering_health;
  j["row_count_baseline"] = cfg.row_count_baseline;
  j["schema_drift"] = cfg.schema_drift;
  if (cfg.query_perf_p50) j["query_perf_p50"] = *cfg.query_perf_p50;
  if (cfg.query_perf_p95) j["query_perf_p95"] = *cfg.query_perf_p95;
  if (cfg.query_perf_p99) j["query_perf_p99"] = *cfg.query_perf_p99;
  if (!cfg.freshness.empty())
  {
    nlohmann::json f;
    for (const auto& [k, v] : cfg.freshness) f[k] = v;
    j["freshness"] = f;
  }
  return j.dump();
}

std::string Compiler::serialize_finops(const PlatformFinOpsConfig& cfg)
{
  nlohmann::json j;
  j["daily_budget"] = cfg.daily_budget;
  if (!cfg.warehouse_budgets.empty())
  {
    nlohmann::json wb;
    for (const auto& [k, v] : cfg.warehouse_budgets) wb[k] = v;
    j["warehouse_budgets"] = wb;
  }
  if (cfg.auto_suspend_idle) j["auto_suspend_idle"] = *cfg.auto_suspend_idle;
  if (cfg.auto_kill_queries) j["auto_kill_queries"] = *cfg.auto_kill_queries;
  j["cost_anomaly_threshold"] = cfg.cost_anomaly_threshold;
  return j.dump();
}

std::string Compiler::serialize_severity_levels(const std::vector<SeverityLevel>& levels)
{
  nlohmann::json j = nlohmann::json::array();
  for (const auto& level : levels)
  {
    nlohmann::json lj;
    lj["name"] = level.level_name;
    lj["conditions"] = level.conditions;
    lj["response"] = level.response;
    if (level.escalation) lj["escalation"] = *level.escalation;
    if (!level.channels.empty()) lj["channels"] = level.channels;
    j.push_back(lj);
  }
  return j.dump();
}

std::string Compiler::serialize_auto_heal(const AutoHealConfig& cfg)
{
  nlohmann::json j;
  j["enabled"] = cfg.enabled;
  j["max_auto_retries"] = cfg.max_auto_retries;
  j["retry_backoff"] = cfg.retry_backoff;
  j["allowed_actions"] = cfg.allowed_actions;
  j["requires_approval"] = cfg.requires_approval;
  {
    nlohmann::json g;
    g["max_cost_per_action"] = cfg.guardrails.max_cost_per_action;
    g["max_retries_per_hour"] = cfg.guardrails.max_retries_per_hour;
    g["no_actions_during"] = cfg.guardrails.no_actions_during;
    g["require_dry_run"] = cfg.guardrails.require_dry_run;
    j["guardrails"] = g;
  }
  return j.dump();
}

std::string Compiler::serialize_correlation_scope(const CorrelationScope& scope)
{
  nlohmann::json j;
  j["schedulers"] = scope.schedulers;
  j["audit_tables"] = scope.audit_tables;
  j["log_sources"] = scope.log_sources;
  if (!scope.job_pattern.empty()) j["job_patterns"] = scope.job_pattern;
  if (!scope.table_pattern.empty()) j["table_patterns"] = scope.table_pattern;
  return j.dump();
}

std::string Compiler::serialize_correlation_sla(const CorrelationSLAConfig& sla)
{
  nlohmann::json j;
  j["deadline"] = sla.deadline;
  if (sla.timezone) j["timezone"] = *sla.timezone;
  j["business_days_only"] = sla.business_days_only;
  if (sla.escalation) j["escalation"] = *sla.escalation;
  return j.dump();
}

// ═══════════════════════════════════════════════════════════════
// v0.9.4 Governance Agent Validation & Serialization
// ═══════════════════════════════════════════════════════════════

std::string Compiler::serialize_auto_classify(const AutoClassifyConfig& cfg)
{
  nlohmann::json j;
  j["enabled"] = cfg.enabled;
  j["provider"] = cfg.provider;
  j["model"] = cfg.model;
  if (!cfg.patterns_json.empty()) j["patterns"] = nlohmann::json::parse(cfg.patterns_json);
  j["semantic"] = {
    {"column_name_analysis", cfg.semantic.column_name_analysis},
    {"sample_value_analysis", cfg.semantic.sample_value_analysis},
    {"cross_column_inference", cfg.semantic.cross_column_inference},
    {"confidence_threshold", cfg.semantic.confidence_threshold}
  };
  j["drift_detection"] = {
    {"enabled", cfg.drift_detection.enabled},
    {"scan_interval", cfg.drift_detection.scan_interval},
    {"alert_on_new_pii", cfg.drift_detection.alert_on_new_pii},
    {"alert_on_reclassification", cfg.drift_detection.alert_on_reclassification}
  };
  return j.dump();
}

void Compiler::validate_governance_agent(const GovernanceAgentDecl& decl)
{
  // GOV-001: Must declare at least catalog or classification
  if (!decl.catalog && !decl.classification)
  {
    compiler_warning("GOV-001: governance agent '" + decl.name +
                     "' should declare at least 'catalog' or 'classification'");
  }
  // GOV-013: Budget required
  if (!decl.budget)
  {
    compiler_warning("GOV-013: governance agent '" + decl.name +
                     "' should have a 'budget' (continuous scanning consumes tokens)");
  }
  // Track
  agent_types_[decl.name] = AgentKind::Governance;
}

void Compiler::validate_classification_policy(const ClassificationPolicyDecl& decl)
{
  // GOV-003: Must define at least two sensitivity levels
  if (decl.levels.size() < 2)
  {
    compiler_warning("GOV-003: classification_policy '" + decl.name +
                     "' should define at least two sensitivity levels");
  }
  // GOV-020: Confidence threshold range
  if (decl.auto_classify.enabled && decl.auto_classify.semantic.confidence_threshold > 0)
  {
    double ct = decl.auto_classify.semantic.confidence_threshold;
    if (ct < 0.5 || ct > 1.0)
    {
      compiler_warning("GOV-020: classification_policy '" + decl.name +
                       "' auto_classify confidence_threshold should be between 0.5 and 1.0");
    }
  }
}

void Compiler::validate_access_policy(const AccessPolicyDecl& decl)
{
  // GOV-004: RBAC/hybrid must define at least one role
  if ((decl.model == AccessModel::RBAC || decl.model == AccessModel::HYBRID_RBAC_ABAC)
      && decl.roles.empty())
  {
    compiler_warning("GOV-004: access_policy '" + decl.name +
                     "' must define at least one role when model is 'rbac' or 'hybrid_rbac_abac'");
  }
}

void Compiler::validate_quality_policy(const QualityPolicyDecl& decl)
{
  // GOV-006: Scoring weights must sum to 1.0
  if (decl.scoring.enabled)
  {
    double sum = decl.scoring.accuracy_weight + decl.scoring.completeness_weight +
                 decl.scoring.consistency_weight + decl.scoring.timeliness_weight +
                 decl.scoring.validity_weight + decl.scoring.uniqueness_weight;
    if (std::abs(1.0 - sum) > 0.01)
    {
      compiler_warning("GOV-006: quality_policy '" + decl.name +
                       "' scoring weights must sum to 1.0, got " + std::to_string(sum));
    }
  }
  // GOV-018: Sample size > 0
  if (decl.profiling.enabled && decl.profiling.sample_size <= 0)
  {
    compiler_warning("GOV-018: quality_policy '" + decl.name +
                     "' profiling sample_size must be > 0");
  }
}

void Compiler::validate_lifecycle_policy(const LifecyclePolicyDecl& decl)
{
  (void)decl; // Validation deferred to runtime for duration parsing
}

void Compiler::validate_compliance_policy(const CompliancePolicyDecl& decl)
{
  // GOV-005: Regulations from supported list
  static const std::unordered_set<std::string> supported = {
    "GDPR", "CCPA", "HIPAA", "BCBS_239", "SOX", "DORA", "EU_AI_ACT", "EU_DATA_ACT"
  };
  for (const auto& reg : decl.regulations)
  {
    if (supported.find(reg) == supported.end())
    {
      compiler_warning("GOV-005: compliance_policy regulation '" + reg + "' is not in supported list");
    }
  }
}

void Compiler::validate_master_data(const MasterDataDecl& decl)
{
  // GOV-010: Confidence threshold 0.0-1.0
  if (decl.matching.confidence_threshold < 0.0 || decl.matching.confidence_threshold > 1.0)
  {
    compiler_warning("GOV-010: master_data '" + decl.name +
                     "' matching confidence_threshold must be between 0.0 and 1.0");
  }
}

void Compiler::validate_data_product(const DataProductDecl& decl)
{
  // GOV-009: Contract SLA validation
  if (!decl.contract.schema_version.empty())
  {
    if (decl.contract.sla.freshness.empty() || decl.contract.sla.quality_score <= 0.0)
    {
      compiler_warning("GOV-009: data_product '" + decl.name +
                       "' contract SLA should define 'freshness' and 'quality_score'");
    }
  }
}

void Compiler::validate_modeling_agent(const ModelingAgentDecl& decl)
{
  // MOD-001: Must have provider and model
  if (!decl.provider || !decl.model)
  {
    compiler_warning("MOD-001: modeling agent '" + decl.name +
                     "' must declare 'provider' and 'model'");
  }
  // MOD-002: Must have at least one schema source
  if (decl.sources.empty())
  {
    compiler_warning("MOD-002: modeling agent '" + decl.name +
                     "' should have at least one 'sources' reference");
  }
  // MOD-003: Budget recommended for continuous operations
  if (!decl.budget)
  {
    compiler_warning("MOD-003: modeling agent '" + decl.name +
                     "' should have a 'budget' (modeling operations consume tokens)");
  }
  // MOD-004: Validate referenced schema sources exist
  for (const auto& src : decl.sources)
  {
    if (schema_source_defs_.find(src) == schema_source_defs_.end())
    {
      compiler_warning("MOD-004: modeling agent '" + decl.name +
                       "' references unknown schema_source '" + src + "'");
    }
  }
  // MOD-005: Validate referenced modeling tools exist
  for (const auto& tool : decl.modeling_tools)
  {
    if (modeling_tool_defs_.find(tool) == modeling_tool_defs_.end())
    {
      compiler_warning("MOD-005: modeling agent '" + decl.name +
                       "' references unknown modeling_tool '" + tool + "'");
    }
  }
  // Track
  agent_types_[decl.name] = AgentKind::Modeling;
}

// ═══════════════════════════════════════════════════════════════
// v0.9.6 Analyst Agent validation
// ═══════════════════════════════════════════════════════════════

void Compiler::validate_analyst_agent(const AnalystAgentDecl& decl)
{
  // ANA-001: Name must not be empty
  if (decl.name.empty())
  {
    compiler_warning("ANA-001: analyst agent name must not be empty");
  }
  // ANA-002: Duplicate check
  if (analyst_agent_defs_.count(decl.name))
  {
    compiler_warning("ANA-002: duplicate analyst agent definition '" + decl.name + "'");
  }
  // ANA-003: Must have at least one connection
  if (decl.connections.empty())
  {
    compiler_warning("ANA-003: analyst agent '" + decl.name +
                     "' has no connections — queries will fail at runtime");
  }
  // ANA-004: Validate referenced connections exist
  for (const auto& conn : decl.connections)
  {
    if (sql_connection_defs_.find(conn) == sql_connection_defs_.end())
    {
      compiler_warning("ANA-004: analyst agent '" + decl.name +
                       "' references unknown sql_connection '" + conn + "'");
    }
  }
  // ANA-005: Should have a budget
  if (!decl.budget)
  {
    compiler_warning("ANA-005: analyst agent '" + decl.name +
                     "' should have a 'budget' (analyst operations consume tokens and compute)");
  }
  // ANA-006: Validate optimizer reference
  if (!decl.optimizer.empty() && query_optimizer_defs_.find(decl.optimizer) == query_optimizer_defs_.end())
  {
    compiler_warning("ANA-006: analyst agent '" + decl.name +
                     "' references unknown query_optimizer '" + decl.optimizer + "'");
  }
  // ANA-007: Validate execution_policy reference
  if (!decl.execution_policy.empty() && execution_policy_defs_.find(decl.execution_policy) == execution_policy_defs_.end())
  {
    compiler_warning("ANA-007: analyst agent '" + decl.name +
                     "' references unknown execution_policy '" + decl.execution_policy + "'");
  }
  // ANA-008: Validate query_library reference
  if (!decl.query_library.empty() && query_library_defs_.find(decl.query_library) == query_library_defs_.end())
  {
    compiler_warning("ANA-008: analyst agent '" + decl.name +
                     "' references unknown query_library '" + decl.query_library + "'");
  }
  // ANA-009: Validate output_format references
  for (const auto& fmt : decl.output_formats)
  {
    if (output_format_defs_.find(fmt) == output_format_defs_.end())
    {
      compiler_warning("ANA-009: analyst agent '" + decl.name +
                       "' references unknown output_format '" + fmt + "'");
    }
  }
  // ANA-010: Validate domain_context reference
  if (!decl.domain_context.empty() && domain_context_defs_.find(decl.domain_context) == domain_context_defs_.end())
  {
    compiler_warning("ANA-010: analyst agent '" + decl.name +
                     "' references unknown domain_context '" + decl.domain_context + "'");
  }
  // Track
  agent_types_[decl.name] = AgentKind::Analyst;
}

void Compiler::validate_deploy_config(const DeployConfigDecl& decl)
{
  // DEP-001: target must reference a declared deploy_target
  if (!decl.target.empty() && deploy_target_defs_.find(decl.target) == deploy_target_defs_.end())
  {
    compiler_warning("[DEP-001] deploy_config '" + decl.name +
      "' references unknown deploy_target '" + decl.target + "'");
  }
  // DEP-002: strategy must be valid
  if (!decl.strategy.empty() &&
      decl.strategy != "rolling" && decl.strategy != "blue_green" && decl.strategy != "canary")
  {
    compiler_warning("[DEP-002] deploy_config '" + decl.name +
      "' has invalid strategy '" + decl.strategy +
      "'; expected 'rolling', 'blue_green', or 'canary'");
  }
  // DEP-010: rollback_policy must reference a declared rollback_policy
  if (!decl.rollback_policy.empty() &&
      rollback_policy_defs_.find(decl.rollback_policy) == rollback_policy_defs_.end())
  {
    compiler_warning("[DEP-010] deploy_config '" + decl.name +
      "' references unknown rollback_policy '" + decl.rollback_policy + "'");
  }
}

// v0.9.8: DataScientistAgent validation
void Compiler::validate_datascientist_agent(const DataScientistAgentDecl& decl)
{
  // DS-010: Name must not be empty
  if (decl.name.empty())
  {
    compiler_warning("DS-010: datascientist agent name must not be empty");
  }
  // DS-011: Duplicate check
  if (datascientist_agent_defs_.count(decl.name))
  {
    compiler_warning("DS-011: duplicate datascientist agent definition '" + decl.name + "'");
  }
  // DS-012: Should have a budget
  if (decl.budget.empty())
  {
    compiler_warning("DS-012: datascientist agent '" + decl.name +
                     "' should have a 'budget' (ML experiments consume significant compute)");
  }
  // DS-013: Validate code_interpreter reference
  if (!decl.code_interpreter.empty() &&
      code_interpreter_defs_.find(decl.code_interpreter) == code_interpreter_defs_.end())
  {
    compiler_warning("DS-013: datascientist agent '" + decl.name +
                     "' references unknown code_interpreter '" + decl.code_interpreter + "'");
  }
  // DS-014: Validate experiment reference
  if (!decl.experiment.empty() &&
      ml_experiment_defs_.find(decl.experiment) == ml_experiment_defs_.end())
  {
    compiler_warning("DS-014: datascientist agent '" + decl.name +
                     "' references unknown ml_experiment '" + decl.experiment + "'");
  }
  // DS-015: Validate eda_config reference
  if (!decl.eda_config.empty() &&
      eda_config_defs_.find(decl.eda_config) == eda_config_defs_.end())
  {
    compiler_warning("DS-015: datascientist agent '" + decl.name +
                     "' references unknown eda_config '" + decl.eda_config + "'");
  }
  // DS-016: Validate volume_router reference
  if (!decl.volume_router.empty() &&
      volume_router_defs_.find(decl.volume_router) == volume_router_defs_.end())
  {
    compiler_warning("DS-016: datascientist agent '" + decl.name +
                     "' references unknown volume_router '" + decl.volume_router + "'");
  }
  // DS-017: Validate problem reference
  if (!decl.problem.empty() &&
      problem_statement_defs_.find(decl.problem) == problem_statement_defs_.end())
  {
    compiler_warning("DS-017: datascientist agent '" + decl.name +
                     "' references unknown problem_statement '" + decl.problem + "'");
  }
  // DS-018: Validate model_registry reference
  if (!decl.model_registry.empty() &&
      ds_model_registry_defs_.find(decl.model_registry) == ds_model_registry_defs_.end())
  {
    compiler_warning("DS-018: datascientist agent '" + decl.name +
                     "' references unknown model_registry '" + decl.model_registry + "'");
  }
  // Track
  agent_types_[decl.name] = AgentKind::DataScientist;
}

void Compiler::validate_causal_agent(const CausalAgentDecl& decl) {
  if (decl.name.empty()) {
    compiler_warning("CAU-001: causal agent name must not be empty");
  }
  if (causal_agent_defs_.count(decl.name)) {
    compiler_warning("CAU-001: duplicate causal agent definition '" + decl.name + "'");
  }
  if (decl.budget.empty()) {
    compiler_warning("CAU-001: causal agent '" + decl.name + "' should have a 'budget'");
  }
  agent_types_[decl.name] = AgentKind::Causal;
}

void Compiler::validate_mlops_agent(const MLOpsAgentDecl& decl) {
  if (decl.name.empty()) compiler_warning("MOP-015: mlops agent name must not be empty");
  if (mlops_agent_defs_.count(decl.name)) compiler_warning("MOP-015: duplicate mlops agent '" + decl.name + "'");
  if (decl.budget.empty()) compiler_warning("MOP-001: mlops agent '" + decl.name + "' should have a 'budget'");
  if (decl.drift_monitor.empty()) compiler_warning("MOP-002: mlops agent '" + decl.name + "' should have a 'drift_monitor'");
  agent_types_[decl.name] = AgentKind::MLOps;
}

void Compiler::validate_databa_agent(const DataBAAgentDecl& decl) {
  if (decl.name.empty()) compiler_warning("BA-011: databa agent name must not be empty");
  if (databa_agent_defs_.count(decl.name)) compiler_warning("BA-011: duplicate databa agent '" + decl.name + "'");
  if (decl.budget.empty()) compiler_warning("BA-001: databa agent '" + decl.name + "' should have a 'budget'");
  agent_types_[decl.name] = AgentKind::DataBA;
}

void Compiler::validate_datatest_agent(const DataTestAgentDecl& decl) {
  if (decl.name.empty()) compiler_warning("TST-001: datatest agent name must not be empty");
  if (datatest_agent_defs_.count(decl.name)) compiler_warning("TST-001: duplicate datatest agent '" + decl.name + "'");
  if (decl.budget.empty()) compiler_warning("TST-002: datatest agent '" + decl.name + "' should have a 'budget'");
  if (decl.test_strategy.empty()) compiler_warning("TST-003: datatest agent '" + decl.name + "' should have a 'test_strategy'");
  agent_types_[decl.name] = AgentKind::DataTest;
}

void Compiler::validate_dio_agent(const DIOAgentDecl& decl) {
  if (decl.name.empty()) compiler_warning("DIO-001: dio agent name must not be empty");
  if (dio_agent_defs_.count(decl.name)) compiler_warning("DIO-001: duplicate dio agent '" + decl.name + "'");
  if (decl.budget.empty()) compiler_warning("DIO-002: dio agent '" + decl.name + "' should have a 'budget'");
  if (decl.agent_registry.empty()) compiler_warning("DIO-003: dio agent '" + decl.name + "' should have an 'agent_registry'");
  if (decl.infrastructure.empty()) compiler_warning("DIO-010: dio agent '" + decl.name + "' should have an 'infrastructure' profile");
  agent_types_[decl.name] = AgentKind::DIO;
}

}  // namespace neamc
