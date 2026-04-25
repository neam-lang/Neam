// v1.5 NeamEvolve — EvolveAgent lifecycle, layered on harness_runtime.
//
// The cardinal rule (Impl Spec §3): this file is a THIN SHIM.  Every
// lifecycle native delegates to harness::* for orchestration and adds only
// the v1.5-specific extension hooks (belief revision triggering, evolve_mode
// validation).  If you find yourself re-implementing per-slot iteration here,
// stop and rebase on harness_run.

#pragma once

#include <string>

namespace neamc::vm::evolve {

struct EvolveResult
{
  bool        ok{false};
  std::string output;       // delegated harness output on success; error string on failure
  std::string error_code;   // "" | "EA-UNKNOWN" | "EA-NOT-EVOLVE" | "EA-STATE" | "HR-..." (delegated)
};

EvolveResult evolve_agent_start   (const std::string& name);
EvolveResult evolve_agent_run     (const std::string& name, const std::string& goal);
EvolveResult evolve_agent_complete(const std::string& name);
EvolveResult evolve_agent_abort   (const std::string& name, const std::string& reason);
std::string  evolve_agent_status  (const std::string& name);
std::string  evolve_agent_trace_path(const std::string& name);

}  // namespace neamc::vm::evolve
