// v1.6 NeamMesh — Agentic Process Automation runtime.
//
// Shim layer over harness_runtime: traverses a process graph (start -> task ->
// decision -> ... -> end), invoking each task via harness::execute_one_step.
// Multi-instance: each process_start() returns a fresh instance_id (ULID) and
// per-instance state is kept in an in-memory map keyed by that id (ADR-004).
//
// Process instances emit hash-chained JSONL events to
// `./runs/{NEAM_RUN_ID}/{process_name}.{instance_id}.trace.jsonl`.
//
// Public surface (5 functions + 1 trace path accessor):
//
//   process_start(process_name)              -> {ok, instance_id, ""}
//   process_advance(instance_id)             -> {ok, current_step, ""}
//                                               (loops until end/error)
//   process_status(instance_id)              -> "running"|"complete"|"aborted"|"unknown"
//   process_abort(instance_id, reason)       -> {ok, "ok", ""}
//   process_trace_path(instance_id)          -> file path string
//
// Lifecycle is intentionally narrow.  Decisions and persistence tier in via
// future helpers.  See impl spec §8.
//
// Deferred to Phase E: SQLite/Postgres backends; recover_instance().

#pragma once

#include <string>
#include <vector>

namespace neamc::vm::process {

struct ProcessResult {
  bool        ok{false};
  std::string output;       // instance_id on start; current step on advance; "ok" elsewhere
  std::string error_code;   // "" on success; "PR-UNKNOWN"|"PR-STATE"|"PR-TASK"|...
};

ProcessResult process_start(const std::string& process_name);
ProcessResult process_advance(const std::string& instance_id);
std::string   process_status(const std::string& instance_id);
ProcessResult process_abort(const std::string& instance_id,
                            const std::string& reason);
std::string   process_trace_path(const std::string& instance_id);

// Enumeration helper for tests / introspection.
std::vector<std::string> list_instances();

// Phase E: file-backed persistence.  Snapshots the instance's live state to
// runs/<run_id>/<process>.<instance_id>.state.json.  Override path with
// NEAM_PROCESS_STATE_PATH (single instance) or NEAM_PROCESS_STATE_DIR (dir).
// process_recover rehydrates the in-memory instance from a state file.
ProcessResult process_persist(const std::string& instance_id);
ProcessResult process_recover(const std::string& state_path);

// Reset all in-memory instance state — tests only.
void reset_for_tests();

}  // namespace neamc::vm::process
