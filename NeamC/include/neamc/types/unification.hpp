//
// NeamC - Type unification
//

#pragma once

#include <map>
#include <optional>
#include <set>
#include <variant>
#include <vector>

#include "neamc/diagnostic.hpp"
#include "neamc/types/constraints.hpp"
#include "neamc/types/type_repr.hpp"

namespace neamc::types
{
class TraitRegistry;

class Substitution
{
public:
  void bind(uint64_t var_id, Type type);
  std::optional<Type> lookup(uint64_t var_id) const;
  Type apply(const Type& t) const;
  Substitution compose(const Substitution& other) const;
  bool contains(uint64_t var_id) const;
  void clear();

  const std::map<uint64_t, Type>& bindings() const;

private:
  std::map<uint64_t, Type> bindings_;
};

struct UnificationError
{
  enum class Kind
  {
    kOccursCheck,
    kTypeMismatch,
    kTraitNotSatisfied,
    kFieldMissing,
    kMethodMissing,
    kArityMismatch
  };

  Kind kind;
  Type expected;
  Type actual;
  SourceLocation location;
  std::string message;
};

template <typename T>
using UnifyResult = std::variant<T, UnificationError>;

class Unifier
{
public:
  explicit Unifier(const TraitRegistry& traits);

  UnifyResult<Substitution> unify(const Type& t1, const Type& t2, SourceLocation loc);
  UnifyResult<Substitution> solve(const std::vector<Constraint>& constraints);
  bool satisfies_trait(const Type& type, const std::string& trait);

private:
  UnifyResult<Substitution> unify_var(const std::shared_ptr<TypeVariable>& var, const Type& t,
                                     SourceLocation loc);
  UnifyResult<Substitution> unify_concrete(const std::shared_ptr<ConcreteType>& c1,
                                          const std::shared_ptr<ConcreteType>& c2,
                                          SourceLocation loc);
  UnifyResult<Substitution> unify_function(const std::shared_ptr<FunctionType>& f1,
                                          const std::shared_ptr<FunctionType>& f2,
                                          SourceLocation loc);
  UnifyResult<Substitution> unify_generic(const std::shared_ptr<GenericType>& g1,
                                         const std::shared_ptr<GenericType>& g2,
                                         SourceLocation loc);

  bool occurs_in(uint64_t var_id, const Type& t);
  bool check_trait_bound(const Type& type, const std::string& trait);

  const TraitRegistry& traits_;
  Substitution current_subst_{};
};

}  // namespace neamc::types
