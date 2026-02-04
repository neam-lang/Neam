// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Redis State Backend (v0.6.0)
//

#pragma once
#ifdef NEAM_BACKEND_REDIS

#include "neamc/vm/state_backend.hpp"

struct redisContext;

namespace neamc::vm
{

class RedisBackend final : public StateBackend
{
public:
  explicit RedisBackend(const std::string& connection_string);
  ~RedisBackend() override;

  RedisBackend(const RedisBackend&) = delete;
  RedisBackend& operator=(const RedisBackend&) = delete;

  // MemoryBackend interface
  void store_event(const std::string& store_name, const MemoryEventRecord& event) override;
  std::vector<MemoryEventRecord> load_events(const std::string& store_name) override;
  void save_checkpoint(const std::string& store_name, const std::string& label,
                       std::size_t position) override;
  std::optional<std::size_t> load_checkpoint(const std::string& store_name,
                                              const std::string& label) override;
  std::vector<MemoryEventRecord> search_events(const std::string& store_name,
                                                const std::string& query,
                                                std::size_t limit = 10) override;
  void clear(const std::string& store_name) override;

  // StateBackend extensions
  void store_learning_interaction(const LearningInteraction& interaction) override;
  std::vector<LearningInteraction> load_recent_interactions(const std::string& agent,
                                                             size_t limit) override;
  void store_learning_review(const LearningReview& review) override;
  void store_prompt_version(const PromptVersion& version) override;
  std::vector<PromptVersion> load_prompt_history(const std::string& agent) override;
  bool try_update_prompt(const std::string& agent, int expected_version,
                         const PromptVersion& new_version) override;
  void store_autonomous_action(const AutonomousAction& action) override;
  DailyBudget load_budget(const std::string& agent, const std::string& date) override;
  void update_budget(const std::string& agent, const DailyBudget& budget) override;

  // Distributed locking via Redis SET NX EX
  bool try_acquire_lock(const std::string& lock_name, const std::string& holder_id,
                        std::chrono::seconds ttl) override;
  void release_lock(const std::string& lock_name, const std::string& holder_id) override;
  bool renew_lock(const std::string& lock_name, const std::string& holder_id,
                  std::chrono::seconds ttl) override;

private:
  redisContext* conn_{nullptr};
};

}  // namespace neamc::vm

#endif  // NEAM_BACKEND_REDIS
