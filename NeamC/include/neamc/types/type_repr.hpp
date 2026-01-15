//
// NeamC - Type representation
//

#pragma once

#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace neamc::types
{
class Substitution;
class TypeEnvironment;

struct TypeVariable;
struct ConcreteType;
struct FunctionType;
struct GenericType;
struct ConstrainedType;

using Type = std::variant<std::shared_ptr<TypeVariable>, std::shared_ptr<ConcreteType>,
                          std::shared_ptr<FunctionType>, std::shared_ptr<GenericType>,
                          std::shared_ptr<ConstrainedType>>;

struct TypeVariable
{
  uint64_t id;
  std::string debug_name;
  std::optional<Type> bound;

  static uint64_t next_id();
  static std::shared_ptr<TypeVariable> fresh(std::string name = "");
};

struct ConcreteType
{
  enum class Kind
  {
    kVoid,
    kBool,
    kNumber,
    kString,
    kAny,
    kNever,
    kNamed
  };

  Kind kind;
  std::string name;

  static std::shared_ptr<ConcreteType> void_type();
  static std::shared_ptr<ConcreteType> bool_type();
  static std::shared_ptr<ConcreteType> number_type();
  static std::shared_ptr<ConcreteType> string_type();
  static std::shared_ptr<ConcreteType> any_type();
  static std::shared_ptr<ConcreteType> never_type();
  static std::shared_ptr<ConcreteType> named(std::string name);
};

struct FunctionType
{
  std::vector<Type> param_types;
  Type return_type;
  bool is_async{false};

  static std::shared_ptr<FunctionType> create(std::vector<Type> params, Type ret, bool async = false);
};

struct GenericType
{
  std::string base_name;
  std::vector<Type> type_args;

  static std::shared_ptr<GenericType> list(Type elem);
  static std::shared_ptr<GenericType> map(Type key, Type value);
  static std::shared_ptr<GenericType> set(Type elem);
  static std::shared_ptr<GenericType> option(Type inner);
  static std::shared_ptr<GenericType> result(Type ok, Type err);
  static std::shared_ptr<GenericType> future(Type inner);
  static std::shared_ptr<GenericType> create(std::string base, std::vector<Type> args);
};

struct ConstrainedType
{
  std::shared_ptr<TypeVariable> type_var;
  std::vector<std::string> trait_bounds;

  static std::shared_ptr<ConstrainedType> create(std::shared_ptr<TypeVariable> var,
                                                 std::vector<std::string> bounds);
};

enum class Linearity
{
  kUnrestricted,
  kAffine,
  kLinear
};

struct TypeScheme
{
  std::vector<std::shared_ptr<TypeVariable>> quantified;
  Type body;
  Linearity linearity{Linearity::kUnrestricted};

  Type instantiate() const;
  static TypeScheme generalize(Type t, const TypeEnvironment& env);
};

std::string type_to_string(const Type& t);
bool types_equal(const Type& a, const Type& b);
Type substitute(const Type& t, const Substitution& subst);
std::set<uint64_t> free_type_vars(const Type& t);

}  // namespace neamc::types
