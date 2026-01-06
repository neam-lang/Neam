//
// Neam Virtual Machine - Value model
//

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace neamc::vm
{
class Bytecode;
class Value;

struct AgentRef
{
  std::string name;
};

using StringRef = std::shared_ptr<std::string>;
using AgentHandle = std::shared_ptr<AgentRef>;
struct Function
{
  std::string name;
  std::size_t arity = 0;
  std::shared_ptr<class Bytecode> chunk;
};

struct NativeFunction;

using FunctionHandle = std::shared_ptr<Function>;
using NativeHandle = std::shared_ptr<NativeFunction>;

enum class ValueType
{
  Nil,
  Bool,
  Number,
  String,
  Agent,
  Function,
  Native
};

class Value
{
public:
  using Storage =
      std::variant<std::monostate, bool, double, StringRef, AgentHandle, FunctionHandle, NativeHandle>;

  Value() = default;
  static Value Nil() { return Value{}; }
  static Value Bool(bool v) { return Value{v}; }
  static Value Number(double v) { return Value{v}; }
  static Value String(const std::string& v) { return Value{std::make_shared<std::string>(v)}; }
  static Value Agent(const std::string& name) { return Value{std::make_shared<AgentRef>(AgentRef{name})}; }
  static Value FunctionValue(Function fn);
  static Value Native(NativeFunction fn);

  ValueType type() const;

  bool is_nil() const { return std::holds_alternative<std::monostate>(storage_); }
  bool is_bool() const { return std::holds_alternative<bool>(storage_); }
  bool is_number() const { return std::holds_alternative<double>(storage_); }
  bool is_string() const { return std::holds_alternative<StringRef>(storage_); }
  bool is_agent() const { return std::holds_alternative<AgentHandle>(storage_); }
  bool is_function() const { return std::holds_alternative<FunctionHandle>(storage_); }
  bool is_native() const { return std::holds_alternative<NativeHandle>(storage_); }

  bool as_bool() const;
  double as_number() const;
  const std::string& as_string() const;
  const AgentRef& as_agent() const;
  const Function& as_function() const;
  const NativeFunction& as_native() const;

  const Storage& raw() const { return storage_; }

private:
  explicit Value(Storage storage) : storage_(std::move(storage)) {}

  Storage storage_{};
};

using NativeFn = std::function<Value(const std::vector<Value>&)>;

struct NativeFunction
{
  std::string name;
  std::size_t arity = 0;
  NativeFn callable;
};

}  // namespace neamc::vm
