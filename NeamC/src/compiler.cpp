//
// NeamC - Compiler backend implementation
//

#include "neamc/compiler.hpp"

#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
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
      },
      expr.node);
}
}  // namespace neamc
