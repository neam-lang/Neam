// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Autonomous Agent Executor (v0.5.0)
//

#include "neamc/vm/autonomous.hpp"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <regex>
#include <sstream>

namespace neamc::vm
{
AutonomousExecutor::AutonomousExecutor() = default;

AutonomousExecutor::~AutonomousExecutor()
{
  stop();
}

void AutonomousExecutor::register_agent(const AutonomousAgentConfig& config)
{
  std::lock_guard lock(mutex_);
  AgentEntry entry;
  entry.config = config;
  entry.interval = parse_schedule(config.schedule);
  entry.last_trigger = std::chrono::steady_clock::now();
  entry.budget.date = current_date();
  agents_[config.agent_name] = std::move(entry);
}

void AutonomousExecutor::unregister_agent(const std::string& agent_name)
{
  std::lock_guard lock(mutex_);
  agents_.erase(agent_name);
}

void AutonomousExecutor::pause_agent(const std::string& agent_name)
{
  std::lock_guard lock(mutex_);
  auto it = agents_.find(agent_name);
  if (it != agents_.end())
  {
    it->second.paused = true;
  }
}

void AutonomousExecutor::resume_agent(const std::string& agent_name)
{
  std::lock_guard lock(mutex_);
  auto it = agents_.find(agent_name);
  if (it != agents_.end())
  {
    it->second.paused = false;
  }
}

bool AutonomousExecutor::is_paused(const std::string& agent_name) const
{
  std::lock_guard lock(mutex_);
  auto it = agents_.find(agent_name);
  return it != agents_.end() && it->second.paused;
}

void AutonomousExecutor::set_goals(const std::string& agent_name,
                                    const std::vector<std::string>& goals)
{
  std::lock_guard lock(mutex_);
  auto it = agents_.find(agent_name);
  if (it != agents_.end())
  {
    it->second.config.goals = goals;
  }
}

std::vector<std::string> AutonomousExecutor::get_goals(const std::string& agent_name) const
{
  std::lock_guard lock(mutex_);
  auto it = agents_.find(agent_name);
  if (it != agents_.end())
  {
    return it->second.config.goals;
  }
  return {};
}

void AutonomousExecutor::start()
{
  if (running_.exchange(true))
  {
    return;  // Already running
  }
  thread_ = std::thread([this]() { run_loop(); });
}

void AutonomousExecutor::stop()
{
  if (!running_.exchange(false))
  {
    return;  // Not running
  }
  cv_.notify_all();
  if (thread_.joinable())
  {
    thread_.join();
  }
}

void AutonomousExecutor::run_loop()
{
  while (running_.load())
  {
    {
      std::unique_lock lock(mutex_);
      cv_.wait_for(lock, std::chrono::seconds(1), [this]() { return !running_.load(); });
      if (!running_.load()) break;

      auto now = std::chrono::steady_clock::now();
      std::string today = current_date();

      for (auto& [name, entry] : agents_)
      {
        if (entry.paused) continue;
        if (entry.interval.count() == 0) continue;

        // Reset daily budget if date changed
        if (entry.budget.date != today)
        {
          entry.budget = DailyBudget{today, 0, 0, 0.0};
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.last_trigger);
        if (elapsed >= entry.interval)
        {
          if (check_budget(entry))
          {
            entry.last_trigger = now;
            // Execute outside lock
            lock.unlock();
            execute_agent(entry);
            lock.lock();
          }
        }
      }
    }
  }
}

bool AutonomousExecutor::check_budget(AgentEntry& entry) const
{
  const auto& cfg = entry.config;
  const auto& budget = entry.budget;

  if (cfg.max_daily_calls > 0 && budget.calls_used >= cfg.max_daily_calls)
  {
    return false;
  }
  if (cfg.max_daily_cost > 0.0 && budget.cost_used >= cfg.max_daily_cost)
  {
    return false;
  }
  if (cfg.max_daily_tokens > 0 && budget.tokens_used >= cfg.max_daily_tokens)
  {
    return false;
  }
  return true;
}

void AutonomousExecutor::execute_agent(AgentEntry& entry)
{
  if (!call_fn_) return;

  // Build a goal-oriented query
  std::string query = "You are running autonomously. Your goals are:\n";
  for (size_t i = 0; i < entry.config.goals.size(); ++i)
  {
    query += std::to_string(i + 1) + ". " + entry.config.goals[i] + "\n";
  }
  query += "\nPlease make progress toward your goals. Report what actions you've taken.";

  try
  {
    std::string result = call_fn_(entry.config.agent_name, query);

    // Update budget
    {
      std::lock_guard lock(mutex_);
      entry.budget.calls_used++;
    }

    // Log the action
    if (log_fn_)
    {
      log_fn_(entry.config.agent_name, "schedule", result);
    }
  }
  catch (...)
  {
    // Autonomous execution failure is non-fatal
    if (log_fn_)
    {
      log_fn_(entry.config.agent_name, "schedule", "Error: execution failed");
    }
  }
}

std::chrono::seconds AutonomousExecutor::parse_schedule(const std::string& schedule) const
{
  if (schedule.empty()) return std::chrono::seconds(0);

  // Parse "every Nm" (minutes), "every Nh" (hours), "every Ns" (seconds)
  std::regex every_regex(R"(every\s+(\d+)\s*(s|m|h|d))");
  std::smatch match;
  if (std::regex_search(schedule, match, every_regex))
  {
    int value = std::stoi(match[1].str());
    std::string unit = match[2].str();
    if (unit == "s") return std::chrono::seconds(value);
    if (unit == "m") return std::chrono::seconds(value * 60);
    if (unit == "h") return std::chrono::seconds(value * 3600);
    if (unit == "d") return std::chrono::seconds(value * 86400);
  }

  // Parse "every day HH:MM" — approximate as daily (24h)
  std::regex daily_regex(R"(every\s+day)");
  if (std::regex_search(schedule, daily_regex))
  {
    return std::chrono::seconds(86400);
  }

  return std::chrono::seconds(0);
}

std::string AutonomousExecutor::current_date() const
{
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#ifdef _WIN32
  localtime_s(&tm, &time_t);
#else
  localtime_r(&time_t, &tm);
#endif
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%d");
  return oss.str();
}
}  // namespace neamc::vm
