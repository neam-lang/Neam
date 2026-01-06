//
// Neam Virtual Machine - Bytecode definition
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <string>
#include <vector>

#include "neamc/vm/value.hpp"

namespace neamc::vm
{
enum class OpCode : uint8_t
{
  OP_CONST = 0,
  OP_POP,
  OP_DUP,

  OP_NEGATE,

  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,

  OP_JUMP,
  OP_JUMP_IF_FALSE,

  OP_EMIT
};

class Bytecode
{
public:
  std::size_t add_constant(Value value);
  void write_op(OpCode op);
  void write_short(uint16_t value);
  void emit_constant(Value value);

  void set_manifest(std::string manifest) { manifest_ = std::move(manifest); }
  const std::string& manifest() const { return manifest_; }

  void serialize(std::ostream& out) const;
  static Bytecode deserialize(std::istream& in);

  const std::vector<uint8_t>& code() const { return code_; }
  const std::vector<Value>& constants() const { return constants_; }

private:
  std::vector<uint8_t> code_{};
  std::vector<Value> constants_{};
  std::string manifest_{};
};

using Chunk = Bytecode;
}  // namespace neamc::vm
