//
// Neam v0.8 Phase 8 — DAG Executor for multi-agent orchestration
//

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "neamc/vm/value.hpp"

namespace neamc::vm
{

class VirtualMachine;

struct DagNode
{
  std::string id;
  std::string agent_name;
  std::string task;
  std::vector<std::string> depends_on;
};

class DagExecutor
{
public:
  explicit DagExecutor(VirtualMachine& vm);

  std::unordered_map<std::string, Value> execute(const std::vector<DagNode>& nodes);
  Value spawn_agent(const std::string& agent_name, const std::string& task);

private:
  std::vector<std::string> topological_order(const std::vector<DagNode>& nodes);

  VirtualMachine& vm_;
};

}  // namespace neamc::vm
