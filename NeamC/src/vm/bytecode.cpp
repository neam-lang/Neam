//
// Neam Virtual Machine - Bytecode implementation
//

#include "neamc/vm/bytecode.hpp"

#include <limits>
#include <stdexcept>

namespace neamc::vm
{
std::size_t Bytecode::add_constant(Value value)
{
  constants_.push_back(std::move(value));
  return constants_.size() - 1;
}

void Bytecode::write_op(OpCode op)
{
  code_.push_back(static_cast<uint8_t>(op));
}

void Bytecode::write_short(uint16_t value)
{
  const uint8_t low = static_cast<uint8_t>(value & 0xFF);
  const uint8_t high = static_cast<uint8_t>((value >> 8) & 0xFF);
  code_.push_back(low);
  code_.push_back(high);
}

void Bytecode::emit_constant(Value value)
{
  write_op(OpCode::OP_CONST);
  const auto index = add_constant(std::move(value));
  if (index > std::numeric_limits<uint16_t>::max())
  {
    throw std::overflow_error("Too many constants in bytecode chunk");
  }
  write_short(static_cast<uint16_t>(index));
}
}  // namespace neamc::vm
