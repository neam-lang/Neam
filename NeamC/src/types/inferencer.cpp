// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Type inference implementation (minimal scaffold)
//

#include "neamc/types/inferencer.hpp"

namespace neamc::types
{
TypeInferencer::TypeInferencer(TypeEnvironment& env, TraitRegistry& traits)
    : env_(env), traits_(traits), unifier_(traits)
{
}

Type TypeInferencer::infer(const Expression* expr)
{
  if (!expr)
  {
    return ConcreteType::any_type();
  }
  auto fresh = TypeVariable::fresh();
  expr_types_[expr] = fresh;
  return fresh;
}

void TypeInferencer::infer_statement(const Statement* stmt)
{
  if (!stmt)
  {
    return;
  }
  if (const auto* expr_stmt = std::get_if<ExpressionStmt>(&stmt->node))
  {
    infer(expr_stmt->expression.get());
  }
}

void TypeInferencer::infer_function(const SkillDecl& skill)
{
  (void)skill;
}

void TypeInferencer::infer_agent(const AgentDecl& agent)
{
  (void)agent;
}

std::optional<std::vector<UnificationError>> TypeInferencer::solve()
{
  auto result = unifier_.solve(constraints_.constraints());
  if (std::holds_alternative<UnificationError>(result))
  {
    errors_.push_back(std::get<UnificationError>(result));
  }
  if (errors_.empty())
  {
    return std::nullopt;
  }
  return errors_;
}

std::optional<Type> TypeInferencer::get_type(const Expression* expr) const
{
  auto it = expr_types_.find(expr);
  if (it == expr_types_.end())
  {
    return std::nullopt;
  }
  return it->second;
}

const std::vector<UnificationError>& TypeInferencer::errors() const
{
  return errors_;
}

void run_type_unification(CompilationUnit& unit)
{
  (void)unit;
}

}  // namespace neamc::types
