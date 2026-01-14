//
// NeamC - Module dependency graph implementation
//

#include "neamc/module/graph.hpp"

namespace neamc::module
{
void ModuleGraph::add_module(const std::string& name)
{
  edges_.try_emplace(name, std::vector<std::string>{});
}

void ModuleGraph::add_import(const std::string& from, const std::string& to)
{
  edges_[from].push_back(to);
}

bool ModuleGraph::has_cycle(std::vector<std::string>* cycle) const
{
  std::unordered_set<std::string> visiting;
  std::unordered_set<std::string> visited;
  std::vector<std::string> stack;

  for (const auto& entry : edges_)
  {
    if (visited.count(entry.first) == 0)
    {
      if (visit(entry.first, visiting, visited, stack, cycle))
      {
        return true;
      }
    }
  }
  return false;
}

bool ModuleGraph::visit(const std::string& node, std::unordered_set<std::string>& visiting,
                        std::unordered_set<std::string>& visited, std::vector<std::string>& stack,
                        std::vector<std::string>* cycle) const
{
  visiting.insert(node);
  stack.push_back(node);

  auto it = edges_.find(node);
  if (it != edges_.end())
  {
    for (const auto& neighbor : it->second)
    {
      if (visiting.count(neighbor) != 0)
      {
        if (cycle)
        {
          cycle->assign(stack.begin(), stack.end());
          cycle->push_back(neighbor);
        }
        return true;
      }
      if (visited.count(neighbor) == 0)
      {
        if (visit(neighbor, visiting, visited, stack, cycle))
        {
          return true;
        }
      }
    }
  }

  visiting.erase(node);
  visited.insert(node);
  stack.pop_back();
  return false;
}
}  // namespace neamc::module
