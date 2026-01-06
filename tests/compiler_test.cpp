//
// Compiler + pipeline integration tests
//

#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"

#include <cassert>
#include <sstream>

using namespace neamc;
using namespace neamc::vm;

int main()
{
  Pipeline pipeline;
  const std::string manifest = R"({"name":"math"})";
  auto unit = pipeline.compile("let x = 1 + 2; return x;", manifest);

  // Expect constants for 1 and 2, with add, set_local, return opcodes
  assert(unit.chunk.constants().size() == 2);
  const auto& code = unit.chunk.code();
  assert(!code.empty());
  assert(static_cast<OpCode>(code[0]) == OpCode::OP_CONST);
  assert(static_cast<OpCode>(code[3]) == OpCode::OP_CONST);
  assert(static_cast<OpCode>(code[6]) == OpCode::OP_ADD);

  std::stringstream buffer;
  unit.chunk.serialize(buffer);
  Chunk roundtrip = Chunk::deserialize(buffer);
  assert(roundtrip.manifest() == manifest);
  assert(roundtrip.constants().size() == unit.chunk.constants().size());
  assert(roundtrip.code().size() == unit.chunk.code().size());

  VirtualMachine vm;
  const auto result = vm.run(roundtrip);
  assert(result.is_number());
  assert(result.as_number() == 3.0);

  // Unary negation path + comparison + if/else
  auto neg_unit = pipeline.compile("let x = -1 + 2; if (x == 1) { return 42; } else { return 0; }", {});
  const auto& neg_code = neg_unit.chunk.code();
  assert(static_cast<OpCode>(neg_code[0]) == OpCode::OP_CONST);
  assert(static_cast<OpCode>(neg_code[3]) == OpCode::OP_NEGATE);

  return 0;
}
