// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Distributed State Backend Interface (v0.6.0)
//

#pragma once

#include "neamc/vm/memory_backend.hpp"

#include <chrono>

namespace neamc::vm
{

// Forward declare DailyBudget from autonomous.hpp
struct DailyBudget;

struct LearningInteraction
{
  std::string agent_name;
  std::string query;
  std::string response;
  double reflection_score{0.0};
  double feedback_score{0.0};
  size_t tokens_used{0};
  int64_t timestamp{0};
};

struct LearningReview
{
  std::string agent_name;
  std::string strategy;
  size_t interactions_reviewed{0};
  double avg_reflection_score{0.0};
  std::string lessons_json;
  std::string prompt_addendum;
  int64_t timestamp{0};
};

struct PromptVersion
{
  std::string agent_name;
  int version{0};
  std::string original_prompt;
  std::string evolved_prompt;
  std::string reasoning;
  std::string status{"active"};
  int64_t timestamp{0};
};

struct AutonomousAction
{
  std::string agent_name;
  std::string trigger_type;
  std::string action_taken;
  size_t tokens_used{0};
  int64_t timestamp{0};
};

class StateBackend : public MemoryBackend
{
public:
  ~StateBackend() override = default;

  // --- Learning (v0.5.0 persistence) ---
  virtual void store_learning_interaction(const LearningInteraction& interaction) = 0;
  virtual std::vector<LearningInteraction> load_recent_interactions(
      const std::string& agent_name, size_t limit) = 0;
  virtual void store_learning_review(const LearningReview& review) = 0;

  // --- Evolution (v0.5.0 persistence) ---
  virtual void store_prompt_version(const PromptVersion& version) = 0;
  virtual std::vector<PromptVersion> load_prompt_history(const std::string& agent_name) = 0;
  virtual bool try_update_prompt(const std::string& agent_name, int expected_version,
                                 const PromptVersion& new_version) = 0;

  // --- Autonomous (v0.5.0 persistence) ---
  virtual void store_autonomous_action(const AutonomousAction& action) = 0;
  virtual DailyBudget load_budget(const std::string& agent_name, const std::string& date) = 0;
  virtual void update_budget(const std::string& agent_name, const DailyBudget& budget) = 0;

  // --- Distributed Locking (v0.6.0 new) ---
  virtual bool try_acquire_lock(const std::string& lock_name, const std::string& holder_id,
                                std::chrono::seconds ttl) = 0;
  virtual void release_lock(const std::string& lock_name, const std::string& holder_id) = 0;
  virtual bool renew_lock(const std::string& lock_name, const std::string& holder_id,
                          std::chrono::seconds ttl) = 0;
};

/// Factory: create backend by type string
std::unique_ptr<StateBackend> create_state_backend(const std::string& backend_type,
                                                    const std::string& connection_string = "");

}  // namespace neamc::vm
