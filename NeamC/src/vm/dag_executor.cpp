//
// Neam v0.8 Phase 8 — DAG Executor for multi-agent orchestration
//

#include "neamc/vm/dag_executor.hpp"

#include <cstdlib>
#include <queue>
#include <stdexcept>
#include <unordered_set>

#include "neamc/vm/vm.hpp"
#include "neamc/vm/object.hpp"
#include "neamc/vm/claw_agent_type.hpp"
#include "neamc/vm/forge_agent_type.hpp"
#include "neamc/llm/provider_factory.hpp"

namespace neamc::vm
{

namespace
{

std::string objstr(const ObjString* s)
{
  if (!s) return "";
  return std::string(s->chars, s->length);
}

}  // namespace

DagExecutor::DagExecutor(VirtualMachine& vm) : vm_(vm) {}

std::vector<std::string> DagExecutor::topological_order(const std::vector<DagNode>& nodes)
{
  std::unordered_map<std::string, int> in_degree;
  std::unordered_map<std::string, std::vector<std::string>> dependents;
  std::unordered_set<std::string> all_ids;

  for (const auto& node : nodes)
  {
    all_ids.insert(node.id);
    if (in_degree.find(node.id) == in_degree.end())
      in_degree[node.id] = 0;
    for (const auto& dep : node.depends_on)
    {
      dependents[dep].push_back(node.id);
      in_degree[node.id]++;
    }
  }

  // Kahn's algorithm (BFS)
  std::queue<std::string> ready;
  for (const auto& [id, deg] : in_degree)
  {
    if (deg == 0)
      ready.push(id);
  }

  std::vector<std::string> order;
  while (!ready.empty())
  {
    auto current = ready.front();
    ready.pop();
    order.push_back(current);

    auto dep_it = dependents.find(current);
    if (dep_it != dependents.end())
    {
      for (const auto& next : dep_it->second)
      {
        if (--in_degree[next] == 0)
          ready.push(next);
      }
    }
  }

  if (order.size() != all_ids.size())
  {
    throw std::runtime_error("Cycle detected in DAG");
  }

  return order;
}

Value DagExecutor::spawn_agent(const std::string& agent_name, const std::string& task)
{
  // Try claw agents first
  auto& claws = vm_.claw_agents();
  auto claw_it = claws.find(agent_name);
  if (claw_it != claws.end())
  {
    auto* claw = claw_it->second;
    try
    {
      llm::ProviderConfig prov_config;
      prov_config.model = objstr(claw->model);
      prov_config.endpoint = objstr(claw->endpoint);
      prov_config.api_key = objstr(claw->api_key_env);
      prov_config.temperature = claw->temperature;
      std::string pname = objstr(claw->provider);
      if (!prov_config.api_key.empty())
      {
        if (const char* ev = std::getenv(prov_config.api_key.c_str()))
          prov_config.api_key = ev;
        else
          prov_config.api_key.clear();
      }
      auto provider = llm::create_provider(pname, prov_config);
      std::string sys = objstr(claw->system);
      std::string prompt = sys.empty() ? task : (sys + "\n\n" + task);
      std::string response = provider->complete(prompt);
      return Value::String(response.c_str(), response.size());
    }
    catch (const std::exception& e)
    {
      std::string err = std::string("Spawn error (claw ") + agent_name + "): " + e.what();
      return Value::String(err.c_str(), err.size());
    }
  }

  // Try forge agents
  auto& forges = vm_.forge_agents();
  auto forge_it = forges.find(agent_name);
  if (forge_it != forges.end())
  {
    std::unordered_map<std::string, Value> outcome;
    outcome["outcome"] = Value::String("completed", 9);
    outcome["iterations"] = Value::Number(0);
    outcome["total_cost"] = Value::Number(0.0);
    outcome["message"] = Value::String(task.c_str(), task.size());
    return Value::Map(new_map(std::move(outcome)));
  }

  // Try legacy agents in globals
  auto* name_str = copy_string(agent_name.c_str(), agent_name.size());
  Value agent_val;
  if (vm_.globals().get(name_str, &agent_val) && agent_val.is_agent())
  {
    auto* agent = static_cast<ObjAgent*>(agent_val.as_obj());
    try
    {
      llm::ProviderConfig prov_config;
      prov_config.model = objstr(agent->model);
      prov_config.endpoint = objstr(agent->endpoint);
      prov_config.api_key = objstr(agent->api_key_env);
      prov_config.temperature = agent->temperature;
      std::string pname = objstr(agent->provider);
      if (!prov_config.api_key.empty())
      {
        if (const char* ev = std::getenv(prov_config.api_key.c_str()))
          prov_config.api_key = ev;
        else
          prov_config.api_key.clear();
      }
      auto provider = llm::create_provider(pname, prov_config);
      std::string sys = objstr(agent->system);
      std::string prompt = sys.empty() ? task : (sys + "\n\n" + task);
      std::string response = provider->complete(prompt);
      return Value::String(response.c_str(), response.size());
    }
    catch (const std::exception& e)
    {
      std::string err = std::string("Spawn error (agent ") + agent_name + "): " + e.what();
      return Value::String(err.c_str(), err.size());
    }
  }

  std::string err = "Agent not found: " + agent_name;
  return Value::String(err.c_str(), err.size());
}

std::unordered_map<std::string, Value> DagExecutor::execute(const std::vector<DagNode>& nodes)
{
  if (nodes.empty())
    return {};

  auto order = topological_order(nodes);

  // Build id -> node lookup
  std::unordered_map<std::string, const DagNode*> node_map;
  for (const auto& n : nodes)
    node_map[n.id] = &n;

  std::unordered_map<std::string, Value> results;
  for (const auto& id : order)
  {
    auto it = node_map.find(id);
    if (it == node_map.end()) continue;
    const auto& node = *it->second;
    results[id] = spawn_agent(node.agent_name, node.task);
  }

  return results;
}

}  // namespace neamc::vm
