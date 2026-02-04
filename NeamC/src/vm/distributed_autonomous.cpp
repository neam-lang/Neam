// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Distributed Autonomous Executor (v0.6.0)
//

#include "neamc/vm/distributed_autonomous.hpp"

#include <iostream>
#include <random>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#endif

namespace neamc::vm
{
namespace
{
std::string generate_default_instance_id()
{
  char hostname[256] = {};
#ifdef _WIN32
  gethostname(hostname, sizeof(hostname));
#else
  gethostname(hostname, sizeof(hostname));
#endif
  std::string id = hostname;

  // Append random suffix for uniqueness
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(1000, 9999);
  id += "-" + std::to_string(dist(gen));

  return id;
}
}  // namespace

DistributedAutonomousExecutor::DistributedAutonomousExecutor()
    : instance_id_(generate_default_instance_id())
{
}

DistributedAutonomousExecutor::~DistributedAutonomousExecutor()
{
  if (is_leader_ && backend_)
  {
    release_leadership();
  }
}

void DistributedAutonomousExecutor::set_state_backend(std::shared_ptr<StateBackend> backend)
{
  backend_ = std::move(backend);
}

void DistributedAutonomousExecutor::set_instance_id(const std::string& id)
{
  instance_id_ = id;
}

std::string DistributedAutonomousExecutor::instance_id() const
{
  return instance_id_;
}

bool DistributedAutonomousExecutor::is_leader() const
{
  return is_leader_;
}

void DistributedAutonomousExecutor::run_loop()
{
  while (running_.load())
  {
    // 1. Try to become/renew leader
    if (!is_leader_)
    {
      is_leader_ = try_become_leader();
      if (is_leader_ && log_fn_)
      {
        log_fn_("system", "leader_election", "Acquired leadership: " + instance_id_);
      }
    }
    else
    {
      auto now = std::chrono::steady_clock::now();
      if (now - last_renew_ >= renew_interval_)
      {
        renew_leadership();
      }
    }

    if (!is_leader_)
    {
      // Not leader: sleep and retry
      std::unique_lock lock(mutex_);
      cv_.wait_for(lock, std::chrono::seconds(5), [this]() { return !running_.load(); });
      continue;
    }

    // 2. Leader behavior: check agents and execute with distributed budget
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

        // Load distributed budget
        entry.budget = load_distributed_budget(name, today);

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - entry.last_trigger);
        if (elapsed >= entry.interval)
        {
          if (check_budget(entry))
          {
            entry.last_trigger = now;
            lock.unlock();
            execute_agent(entry);
            // Save updated budget to distributed backend
            save_distributed_budget(name, entry.budget);
            lock.lock();
          }
        }
      }
    }
  }

  // Release leadership on stop
  if (is_leader_)
  {
    release_leadership();
    is_leader_ = false;
  }
}

bool DistributedAutonomousExecutor::try_become_leader()
{
  if (!backend_) return true;  // No backend = single node, always leader

  try
  {
    bool acquired = backend_->try_acquire_lock("neam:autonomous:leader", instance_id_, lease_ttl_);
    if (acquired)
    {
      last_renew_ = std::chrono::steady_clock::now();
    }
    return acquired;
  }
  catch (const std::exception& e)
  {
    if (log_fn_)
    {
      log_fn_("system", "leader_election",
              std::string("Failed to acquire lock: ") + e.what());
    }
    return false;
  }
}

void DistributedAutonomousExecutor::renew_leadership()
{
  if (!backend_) return;

  try
  {
    if (!backend_->renew_lock("neam:autonomous:leader", instance_id_, lease_ttl_))
    {
      is_leader_ = false;
      if (log_fn_)
      {
        log_fn_("system", "leader_election", "Lost leadership: " + instance_id_);
      }
    }
    else
    {
      last_renew_ = std::chrono::steady_clock::now();
    }
  }
  catch (const std::exception& e)
  {
    is_leader_ = false;
    if (log_fn_)
    {
      log_fn_("system", "leader_election",
              std::string("Failed to renew lock: ") + e.what());
    }
  }
}

void DistributedAutonomousExecutor::release_leadership()
{
  if (!backend_) return;

  try
  {
    backend_->release_lock("neam:autonomous:leader", instance_id_);
    if (log_fn_)
    {
      log_fn_("system", "leader_election", "Released leadership: " + instance_id_);
    }
  }
  catch (...)
  {
    // Best effort
  }
}

DailyBudget DistributedAutonomousExecutor::load_distributed_budget(const std::string& agent_name,
                                                                     const std::string& date)
{
  if (!backend_)
  {
    return DailyBudget{date, 0, 0, 0.0};
  }
  return backend_->load_budget(agent_name, date);
}

void DistributedAutonomousExecutor::save_distributed_budget(const std::string& agent_name,
                                                             const DailyBudget& budget)
{
  if (!backend_) return;
  backend_->update_budget(agent_name, budget);
}

}  // namespace neamc::vm
