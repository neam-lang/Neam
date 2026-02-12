//
// Neam Virtual Machine - Value hashing for Set/Tuple/Map keys (v0.7.0)
//

#pragma once

#include <cstddef>

#include "neamc/vm/value.hpp"

namespace neamc::vm
{
struct ValueHash
{
  std::size_t operator()(const Value& v) const;
};

struct ValueEqual
{
  bool operator()(const Value& a, const Value& b) const;
};

bool is_hashable(const Value& v);
}  // namespace neamc::vm
