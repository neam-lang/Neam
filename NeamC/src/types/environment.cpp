// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Type environment implementation
//

#include "neamc/types/environment.hpp"

#include <algorithm>

namespace neamc::types
{
TypeEnvironment::TypeEnvironment()
{
  scopes_.push_back({});
}

TypeEnvironment::TypeEnvironment(std::shared_ptr<TypeEnvironment> parent) : parent_(std::move(parent))
{
  scopes_.push_back({});
}

void TypeEnvironment::bind(const std::string& name, TypeScheme scheme)
{
  if (scopes_.empty())
  {
    scopes_.push_back({});
  }
  scopes_.back()[name] = std::move(scheme);
}

std::optional<TypeScheme> TypeEnvironment::lookup(const std::string& name) const
{
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it)
  {
    auto found = it->find(name);
    if (found != it->end())
    {
      return found->second;
    }
  }
  if (parent_)
  {
    return parent_->lookup(name);
  }
  return std::nullopt;
}

void TypeEnvironment::push_scope()
{
  scopes_.push_back({});
}

void TypeEnvironment::pop_scope()
{
  if (!scopes_.empty())
  {
    scopes_.pop_back();
  }
}

std::shared_ptr<TypeEnvironment> TypeEnvironment::child_scope()
{
  return std::make_shared<TypeEnvironment>(std::make_shared<TypeEnvironment>(*this));
}

std::set<uint64_t> TypeEnvironment::free_vars() const
{
  std::set<uint64_t> vars;
  for (const auto& scope : scopes_)
  {
    for (const auto& [name, scheme] : scope)
    {
      (void)name;
      auto free = free_type_vars(scheme.body);
      for (const auto& quantified : scheme.quantified)
      {
        free.erase(quantified->id);
      }
      vars.insert(free.begin(), free.end());
    }
  }
  if (parent_)
  {
    auto parent_vars = parent_->free_vars();
    vars.insert(parent_vars.begin(), parent_vars.end());
  }
  return vars;
}

void TypeEnvironment::apply_substitution(const Substitution& subst)
{
  for (auto& scope : scopes_)
  {
    for (auto& [name, scheme] : scope)
    {
      (void)name;
      scheme.body = substitute(scheme.body, subst);
    }
  }
  if (parent_)
  {
    parent_->apply_substitution(subst);
  }
}

void TraitRegistry::register_trait(TraitDef def)
{
  traits_[def.name] = std::move(def);
}

std::optional<TraitRegistry::TraitDef> TraitRegistry::get_trait(const std::string& name) const
{
  auto it = traits_.find(name);
  if (it == traits_.end())
  {
    return std::nullopt;
  }
  return it->second;
}

bool TraitRegistry::has_trait(const std::string& name) const
{
  return traits_.count(name) > 0;
}

bool TraitRegistry::implements(const Type& type, const std::string& trait) const
{
  if (!has_trait(trait))
  {
    return false;
  }

  std::string type_name;
  if (auto concrete = std::get_if<std::shared_ptr<ConcreteType>>(&type))
  {
    type_name = (*concrete)->kind == ConcreteType::Kind::kNamed ? (*concrete)->name
                                                                : type_to_string(Type{*concrete});
  }
  else if (auto generic = std::get_if<std::shared_ptr<GenericType>>(&type))
  {
    type_name = (*generic)->base_name;
  }
  else
  {
    return false;
  }

  auto it = implementations_.find(type_name);
  if (it == implementations_.end())
  {
    return false;
  }
  return std::find(it->second.begin(), it->second.end(), trait) != it->second.end();
}

std::vector<std::string> TraitRegistry::traits_of(const Type& type) const
{
  std::string type_name;
  if (auto concrete = std::get_if<std::shared_ptr<ConcreteType>>(&type))
  {
    type_name = (*concrete)->kind == ConcreteType::Kind::kNamed ? (*concrete)->name
                                                                : type_to_string(Type{*concrete});
  }
  else if (auto generic = std::get_if<std::shared_ptr<GenericType>>(&type))
  {
    type_name = (*generic)->base_name;
  }
  else
  {
    return {};
  }

  auto it = implementations_.find(type_name);
  if (it == implementations_.end())
  {
    return {};
  }
  return it->second;
}

}  // namespace neamc::types
