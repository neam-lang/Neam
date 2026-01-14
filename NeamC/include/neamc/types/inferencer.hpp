//
// NeamC - Type inference interface
//

#pragma once

#include <map>
#include <optional>
#include <vector>

#include "neamc/ast.hpp"
#include "neamc/pipeline.hpp"
#include "neamc/types/constraints.hpp"
#include "neamc/types/environment.hpp"
#include "neamc/types/unification.hpp"

namespace neamc::types
{
class TypeInferencer
{
public:
  TypeInferencer(TypeEnvironment& env, TraitRegistry& traits);

  Type infer(const Expression* expr);
  void infer_statement(const Statement* stmt);
  void infer_function(const SkillDecl& skill);
  void infer_agent(const AgentDecl& agent);

  std::optional<std::vector<UnificationError>> solve();

  std::optional<Type> get_type(const Expression* expr) const;
  const std::vector<UnificationError>& errors() const;

private:
  TypeEnvironment& env_;
  TraitRegistry& traits_;
  ConstraintCollector constraints_{};
  Unifier unifier_;
  std::map<const Expression*, Type> expr_types_;
  std::vector<UnificationError> errors_;
};

void run_type_unification(CompilationUnit& unit);

}  // namespace neamc::types
