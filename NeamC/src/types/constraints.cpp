//
// NeamC - Constraint collector implementation
//

#include "neamc/types/constraints.hpp"

namespace neamc::types
{
void ConstraintCollector::add_equality(Type left, Type right, SourceLocation loc)
{
  constraints_.push_back(EqualityConstraint{std::move(left), std::move(right), std::move(loc)});
}

void ConstraintCollector::add_subtype(Type sub, Type super, SourceLocation loc)
{
  constraints_.push_back(SubtypeConstraint{std::move(sub), std::move(super), std::move(loc)});
}

void ConstraintCollector::add_trait_bound(Type type, std::string trait, SourceLocation loc)
{
  constraints_.push_back(HasTraitConstraint{std::move(type), std::move(trait), std::move(loc)});
}

void ConstraintCollector::add_field(Type type, std::string field, Type field_type, SourceLocation loc)
{
  constraints_.push_back(
      HasFieldConstraint{std::move(type), std::move(field), std::move(field_type), std::move(loc)});
}

void ConstraintCollector::add_method(Type type, std::string method,
                                    std::shared_ptr<FunctionType> sig, SourceLocation loc)
{
  constraints_.push_back(
      HasMethodConstraint{std::move(type), std::move(method), std::move(sig), std::move(loc)});
}

const std::vector<Constraint>& ConstraintCollector::constraints() const
{
  return constraints_;
}

void ConstraintCollector::clear()
{
  constraints_.clear();
}

}  // namespace neamc::types
