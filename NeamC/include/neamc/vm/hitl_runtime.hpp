// v1.6 NeamMesh — Human-In-The-Loop runtime (Phase F).
//
// Forge agents declared with role:"human" cause the process_runtime to
// short-circuit task execution into a "task.human_pending" trace event
// and return a placeholder output.  This module lets external orchestration
// (UI, ticketing system, queue worker) supply the actual human response and
// resume the process.
//
// Surface:
//   hitl_resume(instance_id, payload)  — overwrites the instance's
//     last_output with `payload` so the next process_advance picks it up.
//   hitl_is_pending(instance_id)        — true if last_output starts with
//     the [hitl_pending ...] sentinel.

#pragma once
#include <string>

namespace neamc::vm::hitl {

struct HitlResult {
  bool        ok{false};
  std::string output;        // "ok" or error
  std::string error_code;    // "" | "HL-UNKNOWN" | "HL-NOTPENDING"
};

HitlResult hitl_resume(const std::string& instance_id,
                       const std::string& payload);
bool       hitl_is_pending(const std::string& instance_id);

}  // namespace neamc::vm::hitl
