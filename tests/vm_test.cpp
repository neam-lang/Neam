//
// Basic unit tests for NeamVM
//

#include "neamc/pipeline.hpp"
#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/vm.hpp"

#include <cassert>
#include <iostream>
#include <string>
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

  // Variadic print should accept multiple arguments
  {
    Pipeline pipeline;
    auto unit = pipeline.compile("print(\"hello\", 42); return nil;", {});
    VirtualMachine vm;
    const auto result = vm.run(unit.chunk);
    assert(result.is_nil());
  }

  // Emit should capture values in order
  {
    Pipeline pipeline;
    auto unit = pipeline.compile("emit \"hello\"; emit 42; return nil;", {});
    VirtualMachine vm;
    const auto result = vm.run(unit.chunk);
    assert(result.is_nil());
    const auto& emitted = vm.emitted();
    assert(emitted.size() == 2);
    assert(is_obj_type(emitted[0], ObjType::OBJ_STRING));
    const auto* str = as_string(emitted[0]);
    assert(str->length == 5);
    assert(std::string(str->chars, str->length) == "hello");
    assert(emitted[1].is_number());
    assert(emitted[1].as_number() == 42.0);
  }

  // Skills and agents should compile and expose agent methods
  {
    Pipeline pipeline;
    const std::string program = R"neam(
      skill Echo {
        description: "Echo input back to the caller."
        params: { text: String }
        impl(text) { return text; }
      }

      agent Bot {
        provider: "openai"
        model: "gpt-4-turbo"
        system: "You are helpful."
        skills: [Echo]
      }

      Bot.ask("hello");
      return Bot.context.history();
    )neam";
    auto unit = pipeline.compile(program, {});
    VirtualMachine vm;
    const auto result = vm.run(unit.chunk);
    assert(result.is_list());
    auto* list = as_list(result);
    assert(list->items.size() == 2);
    assert(list->items[0].is_map());
    auto* first = as_map(list->items[0]);
    auto role_it = first->entries.find("role");
    assert(role_it != first->entries.end());
    auto* role_str = as_string(role_it->second);
    assert(std::string(role_str->chars, role_str->length) == "user");
  }

  std::cout << "All VM tests passed\n";
  return 0;
}
