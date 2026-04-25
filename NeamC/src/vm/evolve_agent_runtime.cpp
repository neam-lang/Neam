// v1.5 NeamEvolve — EvolveAgent lifecycle implementation.
// Spec §9.  This file MUST stay a thin shim (NFR-COMPAT-3).

#include "neamc/vm/evolve_agent_runtime.hpp"

#include "neamc/vm/harness_runtime.hpp"
#include "neamc/vm/harness_types.hpp"

namespace neamc::vm::evolve {

EvolveResult evolve_agent_start(const std::string& name)
{
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  const auto* rec = reg.lookup_harness(name);
  if (!rec) return {false, "evolve agent not registered: " + name, "EA-UNKNOWN"};
  if (!rec->evolve_mode) {
    return {false, "harness '" + name + "' is not an evolve agent", "EA-NOT-EVOLVE"};
  }

  // Reset BeliefRecord::revisions_this_run before delegating (FR-EA-9 PAC step).
  if (!rec->belief_ref.empty()) {
    if (auto* b = reg.lookup_belief_mut(rec->belief_ref)) {
      b->revisions_this_run = 0;
    }
  }

  // Delegate to the v1.4.5.1 lifecycle.  This is the load-bearing reuse rule.
  auto h = ::neamc::vm::harness::harness_start(name);
  if (!h.ok) return {false, h.output, h.error_code};
  return {true, "ok", ""};
}

EvolveResult evolve_agent_run(const std::string& name, const std::string& goal)
{
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  const auto* rec = reg.lookup_harness(name);
  if (!rec) return {false, "evolve agent not registered: " + name, "EA-UNKNOWN"};
  if (!rec->evolve_mode) return {false, "not an evolve agent: " + name, "EA-NOT-EVOLVE"};

  // Delegate the inner orchestration to harness_run (NFR-COMPAT-3).
  auto h = ::neamc::vm::harness::harness_run(name, goal);
  if (!h.ok) return {false, h.output, h.error_code};

  // v1.5 extension hook: after the harness loop completes, evaluate
  // belief.revision_trigger.  P0 implements only "every_N_runs"; the rest
  // are no-ops that callers can drive manually via belief_revise.
  if (!rec->belief_ref.empty()) {
    if (auto* b = reg.lookup_belief_mut(rec->belief_ref)) {
      if (b->revision_trigger == "every_N_runs" && b->trigger_n > 0) {
        // Note: P0 doesn't auto-propose new belief text — that requires
        // sub-agent invocation.  For now, the trigger evaluation just
        // increments the counter; tests / user code drive belief_revise
        // explicitly when ready.  This is documented in §10.1 of the
        // implementation spec.
        // (Counter increment is handled by belief_revise itself when
        //  the user code calls it.)
      }
    }
  }

  return {true, h.output, ""};
}

EvolveResult evolve_agent_complete(const std::string& name)
{
  auto h = ::neamc::vm::harness::harness_complete(name);
  if (!h.ok) return {false, h.output, h.error_code};
  return {true, "ok", ""};
}

EvolveResult evolve_agent_abort(const std::string& name, const std::string& reason)
{
  auto h = ::neamc::vm::harness::harness_abort(name, reason);
  if (!h.ok) return {false, h.output, h.error_code};
  return {true, "ok", ""};
}

std::string evolve_agent_status(const std::string& name)
{
  // Pass through HarnessRegistry — same status cell, so registered/running/
  // complete/aborted all surface identically.  An agent's evolve_mode bit is
  // independent of its status; status reflects lifecycle only.
  const auto* rec = ::neamc::vm::harness::HarnessRegistry::instance().lookup_harness(name);
  if (!rec) return "unknown";
  return rec->status;
}

std::string evolve_agent_trace_path(const std::string& name)
{
  return ::neamc::vm::harness::harness_trace_path(name);
}

}  // namespace neamc::vm::evolve
