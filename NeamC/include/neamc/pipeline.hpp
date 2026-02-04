// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Compilation pipeline wiring parser and compiler
//

#pragma once

#include <memory>
#include <string>

#include "neamc/ast.hpp"
#include "neamc/compiler.hpp"
#include "neamc/module/compilation_unit.hpp"
#include "neamc/module/graph.hpp"
#include "neamc/module/resolver.hpp"
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
  CompilationUnit compile(const std::string& source, const std::string& source_path,
                          std::string manifest);

private:
  std::unique_ptr<module::ModuleResolver> resolver_;
  std::unique_ptr<module::ModuleCache> module_cache_;
  std::unique_ptr<module::ModuleGraph> dep_graph_;
};
}  // namespace neamc
