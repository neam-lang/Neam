//
// Neam Virtual Machine - Value model
//

#pragma once

#include <memory>
#include <string>
#include <variant>

namespace neamc::vm
{
struct AgentRef
{
  std::string name;
};

using StringRef = std::shared_ptr<std::string>;
using AgentHandle = std::shared_ptr<AgentRef>;

enum class ValueType
{
  Nil,
  Bool,
  Number,
  String,
  Agent
};

class Value
{
public:
  using Storage = std::variant<std::monostate, bool, double, StringRef, AgentHandle>;

  Value() = default;
  static Value Nil() { return Value{}; }
  static Value Bool(bool v) { return Value{v}; }
  static Value Number(double v) { return Value{v}; }
  static Value String(const std::string& v) { return Value{std::make_shared<std::string>(v)}; }
  static Value Agent(const std::string& name) { return Value{std::make_shared<AgentRef>(AgentRef{name})}; }

  ValueType type() const;

  bool is_nil() const { return std::holds_alternative<std::monostate>(storage_); }
  bool is_bool() const { return std::holds_alternative<bool>(storage_); }
  bool is_number() const { return std::holds_alternative<double>(storage_); }
  bool is_string() const { return std::holds_alternative<StringRef>(storage_); }
  bool is_agent() const { return std::holds_alternative<AgentHandle>(storage_); }

  bool as_bool() const;
  double as_number() const;
  const std::string& as_string() const;
  const AgentRef& as_agent() const;

  const Storage& raw() const { return storage_; }

private:
  explicit Value(Storage storage) : storage_(std::move(storage)) {}

  Storage storage_{};
};
}  // namespace neamc::vm
