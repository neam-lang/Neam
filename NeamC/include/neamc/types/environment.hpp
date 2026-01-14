//
// NeamC - Type environment and trait registry
//

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "neamc/types/type_repr.hpp"
#include "neamc/types/unification.hpp"

namespace neamc::types
{
class TypeEnvironment
{
public:
  TypeEnvironment();
  explicit TypeEnvironment(std::shared_ptr<TypeEnvironment> parent);

  void bind(const std::string& name, TypeScheme scheme);
  std::optional<TypeScheme> lookup(const std::string& name) const;

  void push_scope();
  void pop_scope();
  std::shared_ptr<TypeEnvironment> child_scope();

  std::set<uint64_t> free_vars() const;

  void apply_substitution(const Substitution& subst);

private:
  std::vector<std::map<std::string, TypeScheme>> scopes_;
  std::shared_ptr<TypeEnvironment> parent_{};
};

class TraitRegistry
{
public:
  struct TraitDef
  {
    std::string name;
    std::vector<std::string> methods;
    std::vector<std::string> supertraits;
  };

  void register_trait(TraitDef def);
  std::optional<TraitDef> get_trait(const std::string& name) const;
  bool has_trait(const std::string& name) const;

  bool implements(const Type& type, const std::string& trait) const;
  std::vector<std::string> traits_of(const Type& type) const;

private:
  std::map<std::string, TraitDef> traits_;
  std::map<std::string, std::vector<std::string>> implementations_;
};

}  // namespace neamc::types
