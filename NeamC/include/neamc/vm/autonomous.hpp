// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Autonomous Agent Executor (v0.5.0)
//

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace neamc::vm
{
struct AutonomousAgentConfig
{
  std::string agent_name;
  std::vector<std::string> goals;
  std::string schedule;  // e.g., "every 5m", "every day 9:00"
  bool initiative{false};
  size_t max_daily_calls{100};
  double max_daily_cost{5.0};
  size_t max_daily_tokens{0};
};

struct DailyBudget
{
  std::string date;
  size_t calls_used{0};
  size_t tokens_used{0};
  double cost_used{0.0};
};

class AutonomousExecutor
{
public:
  using AgentCallFn = std::function<std::string(const std::string& agent_name, const std::string& query)>;
  using LogFn = std::function<void(const std::string& agent_name, const std::string& trigger_type,
                                    const std::string& action)>;

  AutonomousExecutor();

  AutonomousExecutor(const AutonomousExecutor&) = delete;
  AutonomousExecutor& operator=(const AutonomousExecutor&) = delete;

  void set_agent_call_fn(AgentCallFn fn) { call_fn_ = std::move(fn); }
  void set_log_fn(LogFn fn) { log_fn_ = std::move(fn); }

  void register_agent(const AutonomousAgentConfig& config);
  void unregister_agent(const std::string& agent_name);

  void pause_agent(const std::string& agent_name);
  void resume_agent(const std::string& agent_name);
  bool is_paused(const std::string& agent_name) const;

  void set_goals(const std::string& agent_name, const std::vector<std::string>& goals);
  std::vector<std::string> get_goals(const std::string& agent_name) const;

  void start();
  void stop();
  bool is_running() const { return running_.load(); }
  virtual ~AutonomousExecutor();

protected:
  struct AgentEntry
  {
    AutonomousAgentConfig config;
    bool paused{false};
    DailyBudget budget;
    std::chrono::steady_clock::time_point last_trigger;
    std::chrono::seconds interval{0};
  };

  virtual void run_loop();
  bool check_budget(AgentEntry& entry) const;
  void execute_agent(AgentEntry& entry);
  std::chrono::seconds parse_schedule(const std::string& schedule) const;
  std::string current_date() const;

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::unordered_map<std::string, AgentEntry> agents_;
  AgentCallFn call_fn_;
  LogFn log_fn_;
};
}  // namespace neamc::vm
