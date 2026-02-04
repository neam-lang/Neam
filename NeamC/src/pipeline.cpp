// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Pipeline wiring parser to compiler
//

#include "neamc/pipeline.hpp"

#include <cstdlib>
#include <filesystem>

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

CompilationUnit Pipeline::compile(const std::string& source, const std::string& source_path,
                                  std::string manifest)
{
  namespace fs = std::filesystem;

  Parser parser(source);
  Program program = parser.parse();
  program.manifest = std::move(manifest);

  // Set up module resolution context
  fs::path src_path(source_path);
  fs::path project_root = src_path.parent_path();

  resolver_ = std::make_unique<module::ModuleResolver>(project_root);

  // Try to find stdlib root: check NEAM_STDLIB env var, or relative to executable
  if (const char* stdlib_env = std::getenv("NEAM_STDLIB"))
  {
    if (*stdlib_env != '\0')
    {
      resolver_->set_stdlib_root(fs::path(stdlib_env));
    }
  }
  else
  {
    // Try common relative paths
    fs::path potential_stdlib = project_root / "stdlib";
    if (fs::exists(potential_stdlib))
    {
      resolver_->set_stdlib_root(potential_stdlib);
    }
  }

  module_cache_ = std::make_unique<module::ModuleCache>();
  dep_graph_ = std::make_unique<module::ModuleGraph>();

  // Derive current module name from file path
  std::string current_module = src_path.stem().string();

  Compiler compiler;
  compiler.set_module_context(resolver_.get(), module_cache_.get(),
                              dep_graph_.get(), current_module);
  vm::Chunk chunk = compiler.compile(program);

  return CompilationUnit{std::move(program), std::move(chunk)};
}
}  // namespace neamc
