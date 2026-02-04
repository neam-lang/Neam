// SPDX-License-Identifier: Apache-2.0
//
// NeamC - Tree-sitter parser wrapper using typed node access
//

#include "neamc/tree_sitter_parser.hpp"

#include <memory>
#include <stdexcept>

#include <tree_sitter/api.h>

#include "neamc/parser.hpp"

namespace neamc
{
namespace
{
struct TreeDeleter
{
  void operator()(TSTree* tree) const { ts_tree_delete(tree); }
};
}  // namespace

TreeSitterParser::TreeSitterParser(const TSLanguage* language) : language_(language)
{
  if (language_ == nullptr)
  {
    throw std::runtime_error("TreeSitterParser requires a language");
  }
}

Program TreeSitterParser::parse(const std::string& source)
{
  TSParser* parser = ts_parser_new();
  ts_parser_set_language(parser, language_);

  TSTree* raw_tree = ts_parser_parse_string(parser, nullptr, source.c_str(), static_cast<uint32_t>(source.size()));
  std::unique_ptr<TSTree, TreeDeleter> tree(raw_tree);
  ts_parser_delete(parser);

  if (tree == nullptr)
  {
    throw std::runtime_error("Failed to produce syntax tree");
  }

  // TODO: when the full Neam grammar is available, walk the tree with a visitor.
  // For now, delegate to the hand-written parser but keep the tree-sitter parse
  // around for validation and debugging.
  Parser fallback(source);
  return fallback.parse();
}
}  // namespace neamc
