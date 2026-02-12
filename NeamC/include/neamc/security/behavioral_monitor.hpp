//
// Neam v0.6.9 — Behavioral Monitoring (D9)
//
// Tracks agent execution baselines, detects anomalous behavior,
// and provides a kill switch to disable misbehaving agents.
//

#pragma once

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace neamc::security {

struct AgentBaseline {
  double avg_tool_calls = 0;
  double avg_response_time_ms = 0;
  std::unordered_set<std::string> typical_tools;
  int sample_count = 0;
};

struct AnomalyResult {
  bool anomalous = false;
  double deviation_score = 0.0;
  std::string reason;
};

class BehavioralMonitor {
 public:
  static BehavioralMonitor& instance();

  // Record metrics from a completed agent request
  void record_request(const std::string& agent, int tool_calls,
                      double response_time_ms,
                      const std::vector<std::string>& tools_used);

  // Check current execution against baseline
  AnomalyResult check_anomaly(const std::string& agent, int tool_calls,
                               const std::vector<std::string>& tools_used);

  // Kill switch — disable/enable agents at runtime
  void disable_agent(const std::string& agent);
  void enable_agent(const std::string& agent);
  bool is_disabled(const std::string& agent) const;

  // Admin introspection
  std::vector<std::string> disabled_agents() const;
  const std::unordered_map<std::string, AgentBaseline>& baselines() const;

 private:
  BehavioralMonitor() = default;

  std::unordered_map<std::string, AgentBaseline> baselines_;
  std::unordered_set<std::string> disabled_agents_;
  mutable std::mutex mutex_;

  // Minimum samples before anomaly detection activates
  int baseline_min_samples_ = 20;

  // Deviation threshold for flagging anomalies (standard deviations)
  double anomaly_threshold_ = 3.0;
};

}  // namespace neamc::security
