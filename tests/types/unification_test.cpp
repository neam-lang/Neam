#include <gtest/gtest.h>

#include "neamc/types/environment.hpp"
#include "neamc/types/unification.hpp"

namespace neamc::types
{
TEST(UnificationTest, UnifyConcreteTypes)
{
  TraitRegistry traits;
  Unifier unifier(traits);

  auto t1 = ConcreteType::number_type();
  auto t2 = ConcreteType::number_type();

  auto result = unifier.unify(t1, t2, {});
  EXPECT_TRUE(std::holds_alternative<Substitution>(result));
}

TEST(UnificationTest, UnifyTypeVariables)
{
  TraitRegistry traits;
  Unifier unifier(traits);

  auto var = TypeVariable::fresh("T");
  auto concrete = ConcreteType::string_type();

  auto result = unifier.unify(var, concrete, {});
  EXPECT_TRUE(std::holds_alternative<Substitution>(result));

  auto subst = std::get<Substitution>(result);
  auto resolved = subst.apply(var);
  EXPECT_EQ(type_to_string(resolved), "string");
}

TEST(UnificationTest, OccursCheckFails)
{
  TraitRegistry traits;
  Unifier unifier(traits);

  auto var = TypeVariable::fresh("T");
  auto list_of_var = GenericType::list(var);

  auto result = unifier.unify(var, list_of_var, {});
  EXPECT_TRUE(std::holds_alternative<UnificationError>(result));

  auto error = std::get<UnificationError>(result);
  EXPECT_EQ(error.kind, UnificationError::Kind::kOccursCheck);
}

TEST(UnificationTest, UnifyGenerics)
{
  TraitRegistry traits;
  Unifier unifier(traits);

  auto var = TypeVariable::fresh("T");
  auto list1 = GenericType::list(var);
  auto list2 = GenericType::list(ConcreteType::number_type());

  auto result = unifier.unify(list1, list2, {});
  EXPECT_TRUE(std::holds_alternative<Substitution>(result));

  auto subst = std::get<Substitution>(result);
  auto resolved = subst.apply(var);
  EXPECT_EQ(type_to_string(resolved), "number");
}
}  // namespace neamc::types
