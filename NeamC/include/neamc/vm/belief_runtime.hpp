// v1.5 NeamEvolve — Belief runtime (the only genuinely new mutable cell in v1.5).
//
// belief_text(name)              -> current text on success | error string
// belief_revise(name, new_text,
//               evolve_agent)    -> "ok" | "[E-...]" — gates: AR kernel +
//                                   max_revisions_per_session + drift bound
// belief_rollback(name [, hash]) -> "ok" | error
// belief_history_json(name)      -> JSON array of {version, hash, ts, ...}
// belief_diff(name, va, vb)      -> human-readable line diff
// belief_hash(name [, version])  -> SHA-256 hex of named version's text
//
// Layered on top of v1.4.5 substrate:
//   - assertion kernel  ⟵  harness::evaluate_hard_regex_assertions_pub
//   - run-scope check   ⟵  harness::is_run_active
//   - trace emission    ⟵  harness::trace_emit_for_run
//   - record storage    ⟵  HarnessRegistry::lookup_belief_mut

#pragma once

#include <string>

namespace neamc::vm::belief {

struct BeliefResult
{
  bool        ok{false};
  std::string output;       // current text on read; "ok" on revise/rollback success
  std::string error_code;   // "" | "BL-UNKNOWN" | "BL-CONSTRAINTS" | "BL-MAX-REV" | "BL-DRIFT" | "BL-NOPRIOR" | "BL-SCOPE"
};

BeliefResult belief_text    (const std::string& name);
BeliefResult belief_revise  (const std::string& name,
                             const std::string& new_text,
                             const std::string& evolve_agent_name);
BeliefResult belief_rollback(const std::string& name,
                             const std::string& version_hash = "");
std::string  belief_history_json(const std::string& name);
std::string  belief_diff    (const std::string& name,
                             const std::string& va_hash,
                             const std::string& vb_hash);
std::string  belief_hash    (const std::string& name, int version = -1);

}  // namespace neamc::vm::belief
