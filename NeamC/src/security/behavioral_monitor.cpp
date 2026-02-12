//
// Neam v0.6.9 — Behavioral Monitoring Implementation (D9)
//

#include "neamc/security/behavioral_monitor.hpp"

#include <cmath>

namespace neamc::security {

BehavioralMonitor& BehavioralMonitor::instance()
{
  static BehavioralMonitor inst;
  return inst;
}

void BehavioralMonitor::record_request(const std::string& agent, int tool_calls,
                                       double response_time_ms,
                                       const std::vector<std::string>& tools_used)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto& baseline = baselines_[agent];

  // Incremental mean update
  baseline.sample_count++;
  double n = static_cast<double>(baseline.sample_count);
  baseline.avg_tool_calls += (tool_calls - baseline.avg_tool_calls) / n;
  baseline.avg_response_time_ms += (response_time_ms - baseline.avg_response_time_ms) / n;

  // Track typical tools
  for (const auto& tool : tools_used)
  {
    baseline.typical_tools.insert(tool);
  }
}

AnomalyResult BehavioralMonitor::check_anomaly(const std::string& agent,
                                                int tool_calls,
                                                const std::vector<std::string>& tools_used)
{
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = baselines_.find(agent);
  if (it == baselines_.end() || it->second.sample_count < baseline_min_samples_)
  {
    // Not enough data to detect anomalies
    return {false, 0.0, ""};
  }

  const auto& baseline = it->second;
  AnomalyResult result;

  // Check tool call count deviation
  if (baseline.avg_tool_calls > 0)
  {
    double deviation = std::abs(tool_calls - baseline.avg_tool_calls) / baseline.avg_tool_calls;
    if (deviation > anomaly_threshold_)
    {
      result.anomalous = true;
      result.deviation_score = deviation;
      result.reason = "Tool call count (" + std::to_string(tool_calls) +
                      ") deviates " + std::to_string(deviation) +
                      "x from baseline avg " +
                      std::to_string(static_cast<int>(baseline.avg_tool_calls));
      return result;
    }
    result.deviation_score = std::max(result.deviation_score, deviation);
  }

  // Check for novel tools not seen in baseline
  int novel_count = 0;
  std::string novel_tools;
  for (const auto& tool : tools_used)
  {
    if (baseline.typical_tools.find(tool) == baseline.typical_tools.end())
    {
      ++novel_count;
      if (!novel_tools.empty()) novel_tools += ",";
      novel_tools += tool;
    }
  }

  // Flag if more than half the tools used are novel
  if (!tools_used.empty() && novel_count > 0)
  {
    double novel_ratio = static_cast<double>(novel_count) / static_cast<double>(tools_used.size());
    if (novel_ratio > 0.5)
    {
      result.anomalous = true;
      result.deviation_score = novel_ratio * anomaly_threshold_;
      result.reason = "Novel tools detected (" + novel_tools +
                      "), " + std::to_string(novel_count) + " of " +
                      std::to_string(tools_used.size()) + " tools not in baseline";
      return result;
    }
  }

  return result;
}

void BehavioralMonitor::disable_agent(const std::string& agent)
{
  std::lock_guard<std::mutex> lock(mutex_);
  disabled_agents_.insert(agent);
}

void BehavioralMonitor::enable_agent(const std::string& agent)
{
  std::lock_guard<std::mutex> lock(mutex_);
  disabled_agents_.erase(agent);
}

bool BehavioralMonitor::is_disabled(const std::string& agent) const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return disabled_agents_.count(agent) > 0;
}

std::vector<std::string> BehavioralMonitor::disabled_agents() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return {disabled_agents_.begin(), disabled_agents_.end()};
}

const std::unordered_map<std::string, AgentBaseline>& BehavioralMonitor::baselines() const
{
  return baselines_;
}

}  // namespace neamc::security
