//
// NeamC - Compilation pipeline wiring parser and compiler
//

#pragma once

#include <string>

#include "neamc/ast.hpp"
#include "neamc/compiler.hpp"
#include "neamc/parser.hpp"
#include "neamc/vm/bytecode.hpp"

namespace neamc
{
struct CompilationUnit
{
  Program program;
  vm::Chunk chunk;
};

class Pipeline
{
public:
  CompilationUnit compile(const std::string& source, std::string manifest = {});
};
}  // namespace neamc
