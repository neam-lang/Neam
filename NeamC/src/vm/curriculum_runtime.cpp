// v1.5 NeamEvolve P1 — Curriculum runtime implementation.  Spec §12.

#include "neamc/vm/curriculum_runtime.hpp"

#include "neamc/vm/harness_runtime.hpp"
#include "neamc/vm/harness_types.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <sstream>
#include <string>

namespace neamc::vm::curriculum {

namespace {

// Locate the active evolve agent that owns this curriculum (mirrors the
// belief / skill_library pattern).
std::string find_active_evolve_agent_for_curriculum(const std::string& curriculum_name)
{
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  for (const auto& name : reg.list_harnesses()) {
    const auto* h = reg.lookup_harness(name);
    if (!h || !h->evolve_mode) continue;
    if (h->curriculum_ref != curriculum_name) continue;
    if (::neamc::vm::harness::is_run_active(name)) return name;
  }
  return {};
}

void emit_advance_event(const std::string& evolve_agent,
                        double from_difficulty,
                        double to_difficulty,
                        double success_rate)
{
  nlohmann::json e = {
    {"kind",            "curriculum.advance"},
    {"evolve_agent",    evolve_agent},
    {"from_difficulty", from_difficulty},
    {"to_difficulty",   to_difficulty},
    {"success_rate",    success_rate}
  };
  ::neamc::vm::harness::trace_emit_for_run_with_chain(evolve_agent, e.dump());
}

}  // anonymous namespace

CurriculumResult curriculum_next(const std::string& name)
{
  auto* curr = ::neamc::vm::harness::HarnessRegistry::instance().lookup_curriculum_mut(name);
  if (!curr) return {false, "", "CR-UNKNOWN"};

  if (curr->mode == "auto" || curr->mode == "co_evolve") {
    // Templated task description per current difficulty band.
    std::ostringstream os;
    os << "[curriculum=" << name << " mode=" << curr->mode
       << " difficulty=" << curr->current_difficulty
       << "] generate task at level " << curr->current_difficulty;
    return {true, os.str(), ""};
  }

  // manual / eval_set_iterator: round-robin from task_pool.
  if (curr->task_pool.empty()) return {false, "", "CR-EMPTY-POOL"};
  // Use recent_outcomes.size() % pool_size for a deterministic walk.
  size_t idx = curr->recent_outcomes.size() % curr->task_pool.size();
  return {true, curr->task_pool[idx], ""};
}

CurriculumResult curriculum_advance(const std::string& name, bool success)
{
  auto* curr = ::neamc::vm::harness::HarnessRegistry::instance().lookup_curriculum_mut(name);
  if (!curr) return {false, "", "CR-UNKNOWN"};

  curr->recent_outcomes.push_back(success);
  if (curr->recent_outcomes.size() > 20) {
    curr->recent_outcomes.erase(curr->recent_outcomes.begin());
  }
  size_t total = curr->recent_outcomes.size();
  size_t hit   = std::count(curr->recent_outcomes.begin(),
                            curr->recent_outcomes.end(), true);
  double rate = total == 0 ? 0.0 : static_cast<double>(hit) / static_cast<double>(total);

  std::string result = "ok";
  double prior = curr->current_difficulty;
  if (rate >= curr->advance_threshold && curr->current_difficulty < 1.0f) {
    curr->current_difficulty = std::min(1.0f, curr->current_difficulty + 0.1f);
    result = "advanced";
  } else if (rate <= curr->fallback_threshold && curr->current_difficulty > 0.0f) {
    curr->current_difficulty = std::max(0.0f, curr->current_difficulty - 0.1f);
    result = "regressed";
  }

  if (result != "ok") {
    std::string ea = find_active_evolve_agent_for_curriculum(name);
    if (!ea.empty()) emit_advance_event(ea, prior, curr->current_difficulty, rate);
  }
  return {true, result, ""};
}

double curriculum_difficulty(const std::string& name)
{
  const auto* curr = ::neamc::vm::harness::HarnessRegistry::instance().lookup_curriculum(name);
  if (!curr) return -1.0;
  return curr->current_difficulty;
}

}  // namespace neamc::vm::curriculum
