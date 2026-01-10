//
// Neam Virtual Machine - Runtime value (type-erased)
//

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "neamc/vm/async/future.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm
{
struct RuntimeValue
{
  using FutureValue = std::shared_ptr<async::Future<Value>>;
  using OptionValue = std::shared_ptr<std::optional<Value>>;
  using ListValue = std::shared_ptr<std::vector<Value>>;

  std::variant<std::monostate, bool, double, std::string, FutureValue, OptionValue, ListValue>
      value;
};
}  // namespace neamc::vm
