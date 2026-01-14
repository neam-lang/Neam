//
// NeamC - Module dependency graph
//

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace neamc::module
{
class ModuleGraph
{
public:
  void add_module(const std::string& name);
  void add_import(const std::string& from, const std::string& to);

  bool has_cycle(std::vector<std::string>* cycle) const;

private:
  bool visit(const std::string& node, std::unordered_set<std::string>& visiting,
             std::unordered_set<std::string>& visited, std::vector<std::string>& stack,
             std::vector<std::string>* cycle) const;

  std::unordered_map<std::string, std::vector<std::string>> edges_{};
};
}  // namespace neamc::module
