//
// NeamC - Tree-sitter backed parser wrapper
//

#pragma once

#include <string>

#include <tree_sitter/api.h>

#include "neamc/ast.hpp"

namespace neamc
{
// Thin wrapper that ensures tree-sitter nodes are navigated via typed helpers
// instead of brittle manual unwrapping. Falls back to the hand-written parser
// for now while the grammar is being iterated on.
class TreeSitterParser
{
public:
  explicit TreeSitterParser(const TSLanguage* language);
  Program parse(const std::string& source);

private:
  const TSLanguage* language_;
};
}  // namespace neamc
