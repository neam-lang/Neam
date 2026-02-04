// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Distributed Autonomous Executor (v0.6.0)
//

#pragma once

#include "neamc/vm/autonomous.hpp"
#include "neamc/vm/state_backend.hpp"

#include <memory>

namespace neamc::vm
{

class DistributedAutonomousExecutor : public AutonomousExecutor
{
public:
  DistributedAutonomousExecutor();
  ~DistributedAutonomousExecutor() override;

  /// Set the state backend for distributed locking and budget tracking
  void set_state_backend(std::shared_ptr<StateBackend> backend);

  /// Unique instance ID for this replica
  void set_instance_id(const std::string& id);
  std::string instance_id() const;

  /// Is this instance currently the leader?
  bool is_leader() const;

protected:
  void run_loop() override;

private:
  bool try_become_leader();
  void renew_leadership();
  void release_leadership();
  DailyBudget load_distributed_budget(const std::string& agent_name, const std::string& date);
  void save_distributed_budget(const std::string& agent_name, const DailyBudget& budget);

  std::shared_ptr<StateBackend> backend_;
  std::string instance_id_;
  std::chrono::seconds lease_ttl_{30};
  std::chrono::seconds renew_interval_{10};
  bool is_leader_{false};
  std::chrono::steady_clock::time_point last_renew_;
};

}  // namespace neamc::vm
