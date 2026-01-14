//
// NeamC - Type representation implementation
//

#include "neamc/types/type_repr.hpp"

#include <atomic>
#include <sstream>
#include <type_traits>
#include <unordered_map>

#include "neamc/types/environment.hpp"
#include "neamc/types/unification.hpp"

namespace neamc::types
{
namespace
{
std::string kind_name(ConcreteType::Kind kind)
{
  switch (kind)
  {
    case ConcreteType::Kind::kVoid:
      return "void";
    case ConcreteType::Kind::kBool:
      return "bool";
    case ConcreteType::Kind::kNumber:
      return "number";
    case ConcreteType::Kind::kString:
      return "string";
    case ConcreteType::Kind::kAny:
      return "any";
    case ConcreteType::Kind::kNever:
      return "never";
    case ConcreteType::Kind::kNamed:
      return "named";
  }
  return "unknown";
}

std::unordered_map<uint64_t, std::shared_ptr<TypeVariable>> collect_vars(const Type& t)
{
  std::unordered_map<uint64_t, std::shared_ptr<TypeVariable>> vars;
  std::visit(
      [&](auto&& node)
      {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, std::shared_ptr<TypeVariable>>)
        {
          vars.emplace(node->id, node);
          if (node->bound.has_value())
          {
            auto inner = collect_vars(*node->bound);
            vars.insert(inner.begin(), inner.end());
          }
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<ConcreteType>>)
        {
          (void)node;
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionType>>)
        {
          for (const auto& param : node->param_types)
          {
            auto inner = collect_vars(param);
            vars.insert(inner.begin(), inner.end());
          }
          auto inner = collect_vars(node->return_type);
          vars.insert(inner.begin(), inner.end());
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<GenericType>>)
        {
          for (const auto& arg : node->type_args)
          {
            auto inner = collect_vars(arg);
            vars.insert(inner.begin(), inner.end());
          }
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<ConstrainedType>>)
        {
          vars.emplace(node->type_var->id, node->type_var);
        }
      },
      t);
  return vars;
}

}  // namespace

uint64_t TypeVariable::next_id()
{
  static std::atomic<uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

std::shared_ptr<TypeVariable> TypeVariable::fresh(std::string name)
{
  auto var = std::make_shared<TypeVariable>();
  var->id = next_id();
  var->debug_name = std::move(name);
  return var;
}

std::shared_ptr<ConcreteType> ConcreteType::void_type()
{
  return std::make_shared<ConcreteType>(ConcreteType{Kind::kVoid, {}});
}

std::shared_ptr<ConcreteType> ConcreteType::bool_type()
{
  return std::make_shared<ConcreteType>(ConcreteType{Kind::kBool, {}});
}

std::shared_ptr<ConcreteType> ConcreteType::number_type()
{
  return std::make_shared<ConcreteType>(ConcreteType{Kind::kNumber, {}});
}

std::shared_ptr<ConcreteType> ConcreteType::string_type()
{
  return std::make_shared<ConcreteType>(ConcreteType{Kind::kString, {}});
}

std::shared_ptr<ConcreteType> ConcreteType::any_type()
{
  return std::make_shared<ConcreteType>(ConcreteType{Kind::kAny, {}});
}

std::shared_ptr<ConcreteType> ConcreteType::never_type()
{
  return std::make_shared<ConcreteType>(ConcreteType{Kind::kNever, {}});
}

std::shared_ptr<ConcreteType> ConcreteType::named(std::string name)
{
  return std::make_shared<ConcreteType>(ConcreteType{Kind::kNamed, std::move(name)});
}

std::shared_ptr<FunctionType> FunctionType::create(std::vector<Type> params, Type ret, bool async)
{
  auto fn = std::make_shared<FunctionType>();
  fn->param_types = std::move(params);
  fn->return_type = std::move(ret);
  fn->is_async = async;
  return fn;
}

std::shared_ptr<GenericType> GenericType::create(std::string base, std::vector<Type> args)
{
  auto g = std::make_shared<GenericType>();
  g->base_name = std::move(base);
  g->type_args = std::move(args);
  return g;
}

std::shared_ptr<GenericType> GenericType::list(Type elem)
{
  return create("List", {std::move(elem)});
}

std::shared_ptr<GenericType> GenericType::map(Type key, Type value)
{
  return create("Map", {std::move(key), std::move(value)});
}

std::shared_ptr<GenericType> GenericType::set(Type elem)
{
  return create("Set", {std::move(elem)});
}

std::shared_ptr<GenericType> GenericType::option(Type inner)
{
  return create("Option", {std::move(inner)});
}

std::shared_ptr<GenericType> GenericType::result(Type ok, Type err)
{
  return create("Result", {std::move(ok), std::move(err)});
}

std::shared_ptr<GenericType> GenericType::future(Type inner)
{
  return create("Future", {std::move(inner)});
}

std::shared_ptr<ConstrainedType> ConstrainedType::create(std::shared_ptr<TypeVariable> var,
                                                         std::vector<std::string> bounds)
{
  auto c = std::make_shared<ConstrainedType>();
  c->type_var = std::move(var);
  c->trait_bounds = std::move(bounds);
  return c;
}

Type TypeScheme::instantiate() const
{
  Substitution subst;
  for (const auto& var : quantified)
  {
    subst.bind(var->id, TypeVariable::fresh(var->debug_name));
  }
  return substitute(body, subst);
}

TypeScheme TypeScheme::generalize(Type t, const TypeEnvironment& env)
{
  auto env_vars = env.free_vars();
  auto free = free_type_vars(t);

  std::vector<std::shared_ptr<TypeVariable>> quantified;
  auto var_map = collect_vars(t);
  for (const auto id : free)
  {
    if (env_vars.count(id) == 0)
    {
      auto it = var_map.find(id);
      if (it != var_map.end())
      {
        quantified.push_back(it->second);
      }
    }
  }

  return TypeScheme{std::move(quantified), std::move(t)};
}

std::string type_to_string(const Type& t)
{
  std::ostringstream out;
  std::visit(
      [&](auto&& node)
      {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, std::shared_ptr<TypeVariable>>)
        {
          if (!node->debug_name.empty())
          {
            out << node->debug_name;
          }
          else
          {
            out << "T" << node->id;
          }
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<ConcreteType>>)
        {
          if (node->kind == ConcreteType::Kind::kNamed)
          {
            out << node->name;
          }
          else
          {
            out << kind_name(node->kind);
          }
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionType>>)
        {
          out << "(";
          for (size_t i = 0; i < node->param_types.size(); ++i)
          {
            if (i > 0)
            {
              out << ", ";
            }
            out << type_to_string(node->param_types[i]);
          }
          out << ") -> ";
          if (node->is_async)
          {
            out << "async ";
          }
          out << type_to_string(node->return_type);
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<GenericType>>)
        {
          out << node->base_name;
          if (!node->type_args.empty())
          {
            out << "<";
            for (size_t i = 0; i < node->type_args.size(); ++i)
            {
              if (i > 0)
              {
                out << ", ";
              }
              out << type_to_string(node->type_args[i]);
            }
            out << ">";
          }
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<ConstrainedType>>)
        {
          out << type_to_string(node->type_var);
          if (!node->trait_bounds.empty())
          {
            out << ": ";
            for (size_t i = 0; i < node->trait_bounds.size(); ++i)
            {
              if (i > 0)
              {
                out << " + ";
              }
              out << node->trait_bounds[i];
            }
          }
        }
      },
      t);
  return out.str();
}

bool types_equal(const Type& a, const Type& b)
{
  if (a.index() != b.index())
  {
    return false;
  }

  return std::visit(
      [&](auto&& left, auto&& right) -> bool
      {
        using T = std::decay_t<decltype(left)>;
        if constexpr (std::is_same_v<T, std::shared_ptr<TypeVariable>>)
        {
          return left->id == right->id;
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<ConcreteType>>)
        {
          return left->kind == right->kind && left->name == right->name;
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionType>>)
        {
          if (left->is_async != right->is_async || left->param_types.size() != right->param_types.size())
          {
            return false;
          }
          for (size_t i = 0; i < left->param_types.size(); ++i)
          {
            if (!types_equal(left->param_types[i], right->param_types[i]))
            {
              return false;
            }
          }
          return types_equal(left->return_type, right->return_type);
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<GenericType>>)
        {
          if (left->base_name != right->base_name || left->type_args.size() != right->type_args.size())
          {
            return false;
          }
          for (size_t i = 0; i < left->type_args.size(); ++i)
          {
            if (!types_equal(left->type_args[i], right->type_args[i]))
            {
              return false;
            }
          }
          return true;
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<ConstrainedType>>)
        {
          return left->type_var->id == right->type_var->id && left->trait_bounds == right->trait_bounds;
        }
        return false;
      },
      a, b);
}

Type substitute(const Type& t, const Substitution& subst)
{
  return subst.apply(t);
}

std::set<uint64_t> free_type_vars(const Type& t)
{
  std::set<uint64_t> vars;
  std::visit(
      [&](auto&& node)
      {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, std::shared_ptr<TypeVariable>>)
        {
          vars.insert(node->id);
          if (node->bound.has_value())
          {
            auto inner = free_type_vars(*node->bound);
            vars.insert(inner.begin(), inner.end());
          }
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<ConcreteType>>)
        {
          (void)node;
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<FunctionType>>)
        {
          for (const auto& param : node->param_types)
          {
            auto inner = free_type_vars(param);
            vars.insert(inner.begin(), inner.end());
          }
          auto inner = free_type_vars(node->return_type);
          vars.insert(inner.begin(), inner.end());
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<GenericType>>)
        {
          for (const auto& arg : node->type_args)
          {
            auto inner = free_type_vars(arg);
            vars.insert(inner.begin(), inner.end());
          }
        }
        else if constexpr (std::is_same_v<T, std::shared_ptr<ConstrainedType>>)
        {
          vars.insert(node->type_var->id);
        }
      },
      t);
  return vars;
}

}  // namespace neamc::types
