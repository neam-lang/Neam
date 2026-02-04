// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Type constraints
//

#pragma once

#include <string>
#include <variant>
#include <vector>

#include "neamc/diagnostic.hpp"
#include "neamc/types/type_repr.hpp"

namespace neamc::types
{
struct EqualityConstraint
{
  Type left;
  Type right;
  SourceLocation origin;
};

struct SubtypeConstraint
{
  Type subtype;
  Type supertype;
  SourceLocation origin;
};

struct HasTraitConstraint
{
  Type type;
  std::string trait_name;
  SourceLocation origin;
};

struct HasFieldConstraint
{
  Type type;
  std::string field_name;
  Type field_type;
  SourceLocation origin;
};

struct HasMethodConstraint
{
  Type type;
  std::string method_name;
  std::shared_ptr<FunctionType> signature;
  SourceLocation origin;
};

using Constraint = std::variant<EqualityConstraint, SubtypeConstraint, HasTraitConstraint,
                                HasFieldConstraint, HasMethodConstraint>;

class ConstraintCollector
{
public:
  void add_equality(Type left, Type right, SourceLocation loc);
  void add_subtype(Type sub, Type super, SourceLocation loc);
  void add_trait_bound(Type type, std::string trait, SourceLocation loc);
  void add_field(Type type, std::string field, Type field_type, SourceLocation loc);
  void add_method(Type type, std::string method, std::shared_ptr<FunctionType> sig, SourceLocation loc);

  const std::vector<Constraint>& constraints() const;
  void clear();

private:
  std::vector<Constraint> constraints_;
};

}  // namespace neamc::types
