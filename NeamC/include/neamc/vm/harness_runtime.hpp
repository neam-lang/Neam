// v1.4.5 Phase 3 full — harness lifecycle + sub-agent orchestration.
//
// Exposes the five primitives that turn a harness declaration into a live run:
//   harness_start(name)           -> status transition registered -> running
//   harness_run(name, goal)       -> iterate sub_agents; per slot: prompt + LLM
//                                    + hard-assertion check + single retry
//   harness_complete(name)        -> running -> complete
//   harness_abort(name, reason)   -> running -> aborted
//   harness_trace_path(name)      -> path of the JSONL trace for this run
//
// Each run writes one JSONL event per lifecycle step to
// `./runs/{NEAM_RUN_ID}/{harness_name}.trace.jsonl` (override with
// NEAM_HARNESS_TRACE_PATH).  Events: harness.start, sub_agent.begin,
// sub_agent.complete, assertion.violation, harness.complete, harness.abort.
//
// Dry-run mode (NEAM_HARNESS_DRY_RUN=1) short-circuits each sub-agent LLM call
// with a deterministic echo string — used by tests and by callers that want to
// validate the orchestration graph without paying for real inference.

#pragma once

#include <string>

namespace neamc::vm::harness {

struct HarnessRunResult
{
  bool ok{false};
  std::string output;  // final sub-agent output on success; error message on failure
  std::string error_code; // "" | "HR-UNKNOWN" | "HR-STATE" | "HR-AR" | "HR-LLM" | ...
};

HarnessRunResult harness_start(const std::string& name);
HarnessRunResult harness_run(const std::string& name, const std::string& goal);
HarnessRunResult harness_complete(const std::string& name);
HarnessRunResult harness_abort(const std::string& name, const std::string& reason);
std::string      harness_trace_path(const std::string& name);

// v1.5 NeamEvolve — additive accessors so layered components (belief_runtime,
// evolve_agent_runtime) can reuse the substrate without duplicating state.
// These are read-only and purely additive; v1.4.5 callers are unaffected.
bool             is_run_active(const std::string& name);

// Reuse the hard-severity regex assertion evaluator from belief revisions.
// Returns "" on pass; first failing assertion name on violation.
std::string      evaluate_hard_regex_assertions_pub(const std::string& ar_name,
                                                    const std::string& content);

// Append an arbitrary JSON event to the trace file currently open for `name`.
// Used by belief_runtime / skill_library_runtime to emit v1.5 event kinds
// into the same JSONL plane (FR-EVT-1).
//
// The chain-aware variant `trace_emit_for_run_with_chain` adds prev_hash /
// this_hash fields per NFR-SEC-6 — making the v1.5 audit plane tamper-evident.
// v1.5 event emitters MUST use the chain variant; v1.4.5 emitters may use
// either (backward-compat: consumers ignore unknown fields per FR-EVT-4).
void             trace_emit_for_run(const std::string& name,
                                    const std::string& kind_payload_json);
void             trace_emit_for_run_with_chain(const std::string& name,
                                               const std::string& kind_payload_json);

}  // namespace neamc::vm::harness
