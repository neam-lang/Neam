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
  auto unit = pipeline.compile("{ 1 + 2; }", manifest);

  // Expect constants for 1 and 2, with add + pop opcodes
  assert(unit.chunk.constants().size() == 2);
  const auto& code = unit.chunk.code();
  assert(!code.empty());
  assert(static_cast<OpCode>(code[0]) == OpCode::OP_CONST);
  assert(static_cast<OpCode>(code[3]) == OpCode::OP_CONST);
  assert(static_cast<OpCode>(code[6]) == OpCode::OP_ADD);
  assert(static_cast<OpCode>(code[7]) == OpCode::OP_POP);

  std::stringstream buffer;
  unit.chunk.serialize(buffer);
  Chunk roundtrip = Chunk::deserialize(buffer);
  assert(roundtrip.manifest() == manifest);
  assert(roundtrip.constants().size() == unit.chunk.constants().size());
  assert(roundtrip.code().size() == unit.chunk.code().size());

  VirtualMachine vm;
  const auto result = vm.run(roundtrip);
  (void)result;
  assert(vm.stack().empty());

  // Unary negation path
  auto neg_unit = pipeline.compile("-1 + 2;", {});
  const auto& neg_code = neg_unit.chunk.code();
  assert(static_cast<OpCode>(neg_code[0]) == OpCode::OP_CONST);
  assert(static_cast<OpCode>(neg_code[3]) == OpCode::OP_NEGATE);

  return 0;
}
