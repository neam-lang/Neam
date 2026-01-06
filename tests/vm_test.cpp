//
// Basic unit tests for NeamVM
//

#include "neamc/pipeline.hpp"
#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/vm.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

using namespace neamc;
using namespace neamc::vm;

int main()
{
  // Raw bytecode arithmetic
  {
    Bytecode chunk;
    chunk.emit_constant(Value::Number(1.0));
    chunk.emit_constant(Value::Number(2.0));
    chunk.write_op(OpCode::OP_ADD);
    chunk.write_op(OpCode::OP_RETURN);

    VirtualMachine vm;
    const auto result = vm.run(chunk);
    assert(result.is_number());
    assert(result.as_number() == 3.0);
  }

  // While loop + locals + return
  {
    Pipeline pipeline;
    auto unit = pipeline.compile("let x = 0; while (x < 3) { x = x + 1; } return x;", {});
    VirtualMachine vm;
    const auto result = vm.run(unit.chunk);
    assert(result.is_number());
    assert(result.as_number() == 3.0);
  }

  // Function calls
  {
    Pipeline pipeline;
    auto unit = pipeline.compile("fun add(a, b) { return a + b; } return add(2, 3);", {});
    VirtualMachine vm;
    const auto result = vm.run(unit.chunk);
    assert(result.is_number());
    assert(result.as_number() == 5.0);
  }

  // Native print should not throw and should return nil
  {
    Pipeline pipeline;
    auto unit = pipeline.compile("print(\"hello\"); return nil;", {});
    VirtualMachine vm;
    const auto result = vm.run(unit.chunk);
    assert(result.is_nil());
  }

  std::cout << "All VM tests passed\n";
  return 0;
}
