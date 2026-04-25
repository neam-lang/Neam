// v1.5 NeamEvolve P2 — Design operation (architecture search via meta-agent).
//
// design_propose(evolve_agent, goal)         -> candidate Neam source string
// design_compile_in_sandbox(candidate)        -> "ok" | "[design error ...]"
// design_score(candidate, eval_set)           -> score Number
// design_promote(evolve_agent, candidate)     -> "ok" | "[needs human_gate]"
//
// Locked behind FR-DS-4 mandatory `safety.human_gate:` reference (E-010).
// Sandboxed compilation spawns a child neamc process with a per-call temp
// directory.  P2 design archive at runs/$RUN_ID/design_archive.jsonl.

#pragma once

#include <string>

namespace neamc::vm::design {

struct DesignResult
{
  bool        ok{false};
  std::string output;       // candidate source on propose; "ok" otherwise
  std::string error_code;   // "" | "DS-UNKNOWN-AGENT" | "DS-NOGATE" | "DS-CAP"
                            //    | "DS-COMPILE" | "DS-IO" | "DS-DENY"
};

DesignResult design_propose(const std::string& evolve_agent_name,
                            const std::string& goal);

DesignResult design_compile_in_sandbox(const std::string& candidate_source);

double       design_score(const std::string& candidate_source,
                          const std::string& eval_set_name);

DesignResult design_promote(const std::string& evolve_agent_name,
                            const std::string& candidate_source);

}  // namespace neamc::vm::design
