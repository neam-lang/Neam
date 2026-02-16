//
// Neam v0.8 Phase 4 — Forge Loop helpers
//
// Lightweight types and free functions for plan parsing, checkpoint strategies,
// and learnings tracking. These don't need vm.cpp internals; the main forge loop
// lives inline in vm.cpp.
//

#pragma once

#include <string>
#include <vector>

namespace neamc::vm
{

enum class LoopOutcomeKind { Completed, MaxIterations, Aborted, BudgetExhausted };

struct LoopOutcome
{
  LoopOutcomeKind kind{LoopOutcomeKind::Completed};
  int iterations{0};
  std::string message;
  double total_cost{0.0};
};

// Plan file: one task per line (strips markdown bullets / numbered prefixes)
std::vector<std::string> load_plan_tasks(const std::string& path);

// Progress: JSONL file tracking completed tasks
std::vector<std::string> load_completed_tasks(const std::string& path);
void mark_task_done(const std::string& path, int iteration, const std::string& task);

// Learnings: JSONL append-only log
void append_learning(const std::string& path, int iteration,
                     const std::string& task, const std::string& status,
                     const std::string& detail);

// Checkpoint strategies: "git", "snapshot", "none"/""
void forge_checkpoint(const std::string& strategy, const std::string& workspace,
                      int iteration, const std::string& task);

// Convert LoopOutcomeKind to string
inline const char* outcome_kind_str(LoopOutcomeKind kind)
{
  switch (kind)
  {
    case LoopOutcomeKind::Completed:       return "completed";
    case LoopOutcomeKind::MaxIterations:   return "max_iterations";
    case LoopOutcomeKind::Aborted:         return "aborted";
    case LoopOutcomeKind::BudgetExhausted: return "budget_exhausted";
  }
  return "unknown";
}

}  // namespace neamc::vm
