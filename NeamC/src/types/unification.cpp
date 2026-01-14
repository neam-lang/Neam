//
// NeamC - Unification implementation
//

#include "neamc/types/unification.hpp"

#include <algorithm>
#include <sstream>

#include "neamc/types/environment.hpp"

namespace neamc::types
{
void Substitution::bind(uint64_t var_id, Type type)
{
  bindings_[var_id] = std::move(type);
}

std::optional<Type> Substitution::lookup(uint64_t var_id) const
{
  auto it = bindings_.find(var_id);
  if (it == bindings_.end())
  {
    return std::nullopt;
  }
  return it->second;
}

Type Substitution::apply(const Type& t) const
{
  return std::visit(
      [&](auto&& node) -> Type
      {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, std::shared_ptr<TypeVariable>>)
        {
          auto it = bindings_.find(node->id);
          if (it != bindings_.end())
          {
            return apply(it->second);
          }
          if (node->bound.has_value())
          {
            auto bound = apply(*node->bound);
            auto next = std::make_shared<TypeVariable>(*node);
            next->bound = bound;
            return next;
          }
          return node;
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<ConcreteType>>)
        {
          return node;
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionType>>)
        {
          std::vector<Type> params;
          params.reserve(node->param_types.size());
          for (const auto& param : node->param_types)
          {
            params.push_back(apply(param));
          }
          return FunctionType::create(std::move(params), apply(node->return_type), node->is_async);
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<GenericType>>)
        {
          std::vector<Type> args;
          args.reserve(node->type_args.size());
          for (const auto& arg : node->type_args)
          {
            args.push_back(apply(arg));
          }
          return GenericType::create(node->base_name, std::move(args));
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<ConstrainedType>>)
        {
          auto type_var = std::static_pointer_cast<TypeVariable>(apply(node->type_var));
          return ConstrainedType::create(type_var, node->trait_bounds);
        }
        return node;
      },
      t);
}

Substitution Substitution::compose(const Substitution& other) const
{
  Substitution result = other;
  for (const auto& [id, type] : bindings_)
  {
    result.bind(id, other.apply(type));
  }
  return result;
}

bool Substitution::contains(uint64_t var_id) const
{
  return bindings_.count(var_id) > 0;
}

void Substitution::clear()
{
  bindings_.clear();
}

const std::map<uint64_t, Type>& Substitution::bindings() const
{
  return bindings_;
}

Unifier::Unifier(const TraitRegistry& traits) : traits_(traits) {}

UnifyResult<Substitution> Unifier::unify(const Type& t1, const Type& t2, SourceLocation loc)
{
  auto left = current_subst_.apply(t1);
  auto right = current_subst_.apply(t2);

  if (types_equal(left, right))
  {
    return Substitution{};
  }

  if (auto var = std::get_if<std::shared_ptr<TypeVariable>>(&left))
  {
    return unify_var(*var, right, loc);
  }
  if (auto var = std::get_if<std::shared_ptr<TypeVariable>>(&right))
  {
    return unify_var(*var, left, loc);
  }
  if (auto c1 = std::get_if<std::shared_ptr<ConcreteType>>(&left))
  {
    if (auto c2 = std::get_if<std::shared_ptr<ConcreteType>>(&right))
    {
      return unify_concrete(*c1, *c2, loc);
    }
  }
  if (auto f1 = std::get_if<std::shared_ptr<FunctionType>>(&left))
  {
    if (auto f2 = std::get_if<std::shared_ptr<FunctionType>>(&right))
    {
      return unify_function(*f1, *f2, loc);
    }
  }
  if (auto g1 = std::get_if<std::shared_ptr<GenericType>>(&left))
  {
    if (auto g2 = std::get_if<std::shared_ptr<GenericType>>(&right))
    {
      return unify_generic(*g1, *g2, loc);
    }
  }

  UnificationError error{
      UnificationError::Kind::kTypeMismatch,
      left,
      right,
      loc,
      "Type mismatch: expected " + type_to_string(left) + ", found " + type_to_string(right)};
  return error;
}

UnifyResult<Substitution> Unifier::solve(const std::vector<Constraint>& constraints)
{
  Substitution accumulated;
  for (const auto& constraint : constraints)
  {
    auto result = std::visit(
        [&](auto&& c) -> UnifyResult<Substitution>
        {
          using T = std::decay_t<decltype(c)>;
          if constexpr (std::is_same_v<T, EqualityConstraint>)
          {
            return unify(c.left, c.right, c.origin);
          }
          else if constexpr (std::is_same_v<T, SubtypeConstraint>)
          {
            return unify(c.subtype, c.supertype, c.origin);
          }
          else if constexpr (std::is_same_v<T, HasTraitConstraint>)
          {
            if (!check_trait_bound(c.type, c.trait_name))
            {
              return UnificationError{UnificationError::Kind::kTraitNotSatisfied,
                                      c.type,
                                      c.type,
                                      c.origin,
                                      "Trait bound not satisfied: " + c.trait_name};
            }
            return Substitution{};
          }
          else if constexpr (std::is_same_v<T, HasFieldConstraint>)
          {
            return UnificationError{UnificationError::Kind::kFieldMissing,
                                    c.type,
                                    c.field_type,
                                    c.origin,
                                    "Field not found: " + c.field_name};
          }
          else if constexpr (std::is_same_v<T, HasMethodConstraint>)
          {
            return UnificationError{UnificationError::Kind::kMethodMissing,
                                    c.type,
                                    c.signature,
                                    c.origin,
                                    "Method not found: " + c.method_name};
          }
          return Substitution{};
        },
        constraint);

    if (std::holds_alternative<UnificationError>(result))
    {
      return std::get<UnificationError>(result);
    }

    auto subst = std::get<Substitution>(result);
    current_subst_ = current_subst_.compose(subst);
    accumulated = accumulated.compose(subst);
  }

  return accumulated;
}

bool Unifier::satisfies_trait(const Type& type, const std::string& trait)
{
  return check_trait_bound(type, trait);
}

UnifyResult<Substitution> Unifier::unify_var(const std::shared_ptr<TypeVariable>& var, const Type& t,
                                             SourceLocation loc)
{
  if (auto tv = std::get_if<std::shared_ptr<TypeVariable>>(&t))
  {
    if (var->id == (*tv)->id)
    {
      return Substitution{};
    }
  }

  if (occurs_in(var->id, t))
  {
    return UnificationError{UnificationError::Kind::kOccursCheck,
                            Type{var},
                            t,
                            loc,
                            "Occurs check failed: " + type_to_string(Type{var}) + " in " + type_to_string(t)};
  }

  Substitution subst;
  subst.bind(var->id, t);
  return subst;
}

UnifyResult<Substitution> Unifier::unify_concrete(const std::shared_ptr<ConcreteType>& c1,
                                                  const std::shared_ptr<ConcreteType>& c2,
                                                  SourceLocation loc)
{
  if (c1->kind == c2->kind && c1->name == c2->name)
  {
    return Substitution{};
  }
  return UnificationError{UnificationError::Kind::kTypeMismatch,
                          Type{c1},
                          Type{c2},
                          loc,
                          "Type mismatch: expected " + type_to_string(Type{c1}) + ", found " + type_to_string(Type{c2})};
}

UnifyResult<Substitution> Unifier::unify_function(const std::shared_ptr<FunctionType>& f1,
                                                  const std::shared_ptr<FunctionType>& f2,
                                                  SourceLocation loc)
{
  if (f1->param_types.size() != f2->param_types.size())
  {
    return UnificationError{UnificationError::Kind::kArityMismatch,
                            f1,
                            f2,
                            loc,
                            "Function arity mismatch"};
  }

  Substitution result;
  for (size_t i = 0; i < f1->param_types.size(); ++i)
  {
    auto step = unify(f1->param_types[i], f2->param_types[i], loc);
    if (std::holds_alternative<UnificationError>(step))
    {
      return std::get<UnificationError>(step);
    }
    result = result.compose(std::get<Substitution>(step));
  }

  auto ret = unify(f1->return_type, f2->return_type, loc);
  if (std::holds_alternative<UnificationError>(ret))
  {
    return std::get<UnificationError>(ret);
  }

  result = result.compose(std::get<Substitution>(ret));
  return result;
}

UnifyResult<Substitution> Unifier::unify_generic(const std::shared_ptr<GenericType>& g1,
                                                 const std::shared_ptr<GenericType>& g2,
                                                 SourceLocation loc)
{
  if (g1->base_name != g2->base_name || g1->type_args.size() != g2->type_args.size())
  {
    return UnificationError{UnificationError::Kind::kArityMismatch,
                            g1,
                            g2,
                            loc,
                            "Generic type mismatch: " + g1->base_name};
  }

  Substitution result;
  for (size_t i = 0; i < g1->type_args.size(); ++i)
  {
    auto step = unify(g1->type_args[i], g2->type_args[i], loc);
    if (std::holds_alternative<UnificationError>(step))
    {
      return std::get<UnificationError>(step);
    }
    result = result.compose(std::get<Substitution>(step));
  }
  return result;
}

bool Unifier::occurs_in(uint64_t var_id, const Type& t)
{
  auto applied = current_subst_.apply(t);
  auto vars = free_type_vars(applied);
  return vars.count(var_id) > 0;
}

bool Unifier::check_trait_bound(const Type& type, const std::string& trait)
{
  if (auto constrained = std::get_if<std::shared_ptr<ConstrainedType>>(&type))
  {
    const auto& bounds = (*constrained)->trait_bounds;
    if (std::find(bounds.begin(), bounds.end(), trait) == bounds.end())
    {
      return false;
    }
    return true;
  }

  return traits_.implements(type, trait);
}

}  // namespace neamc::types
