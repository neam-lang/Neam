//
// Basic unit tests for NeamVM
//

#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/vm.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace neamc::vm;

namespace
{
Bytecode arithmetic_program()
{
  Bytecode chunk;
  chunk.emit_constant(Value::Number(1.0));
  chunk.emit_constant(Value::Number(2.0));
  chunk.write_op(OpCode::OP_ADD);
  return chunk;
}

Bytecode branching_program()
{
  Bytecode chunk;
  chunk.emit_constant(Value::Number(0.0));        // [0] push 0
  chunk.write_op(OpCode::OP_JUMP_IF_FALSE);      // [1] conditional jump
  chunk.write_short(4);                          // skip next const + emit
  chunk.emit_constant(Value::String("skipped")); // [4]
  chunk.write_op(OpCode::OP_EMIT);               // [7]

  chunk.emit_constant(Value::String("executed"));
  chunk.write_op(OpCode::OP_EMIT);
  return chunk;
}

Bytecode type_error_program()
{
  Bytecode chunk;
  chunk.emit_constant(Value::Number(1.0));
  chunk.emit_constant(Value::String("bad"));
  chunk.write_op(OpCode::OP_ADD);
  return chunk;
}
}  // namespace

int main()
{
  {
    VirtualMachine vm;
    const auto result = vm.run(arithmetic_program());
    assert(result.is_number());
    assert(result.as_number() == 3.0);
    assert(vm.stack().size() == 1);
  }

  {
    VirtualMachine vm;
    vm.run(branching_program());
    assert(vm.events().size() == 1);
    assert(vm.events().front().is_string());
    assert(vm.events().front().as_string() == "executed");
  }

  {
    bool threw = false;
    VirtualMachine vm;
    try
    {
      vm.run(type_error_program());
    }
    catch (const std::runtime_error&)
    {
      threw = true;
    }
    assert(threw && "Expected numeric type error");
  }

  std::cout << "All VM tests passed\n";
  return 0;
}
