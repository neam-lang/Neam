//
// NeamC - Pipeline wiring parser to compiler
//

#include "neamc/pipeline.hpp"

namespace neamc
{
CompilationUnit Pipeline::compile(const std::string& source, std::string manifest)
{
  Parser parser(source);
  Program program = parser.parse();
  program.manifest = std::move(manifest);

  Compiler compiler;
  vm::Chunk chunk = compiler.compile(program);

  return CompilationUnit{std::move(program), std::move(chunk)};
}
}  // namespace neamc
