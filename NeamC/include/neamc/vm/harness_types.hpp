// Neam v1.4.5 NeamHarness — side-table runtime state for harness-family declarations.
//
// Phase 3-minimal scope:
//   - HarnessRecord:   rich runtime state per harness (hash, status, fields)
//   - HarnessRegistry: process-wide registry keyed by declaration name
//   - compute_harness_hash: deterministic SHA-256 of (name + fields_json)
//   - harness_runtime_env_map: snapshot of NEAM_RUN_* values
//
// Full runtime (harness_start, sub-agent spawn, trace writer, assertion
// kernel) lands in a future PR. This file establishes the side-table
// pattern documented in v1.4.5 Impl Spec §7.1.

#pragma once

#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace neamc::vm::harness {

// ─── Per-declaration runtime records ──────────────────────────────────
//
// All five v1.4.5 declarations share a minimal record shape for Phase 3.
// Phase 3+ expands HarnessRecord with capability envelope, trace writer,
// assertion kernel, etc.  Other records stay simple until their consuming
// runtime lands.

struct HarnessRecord {
  std::string name;
  std::string bytecode_hash;  // SHA-256 hex, 64 chars
  std::string provider;
  std::string model;
  std::string fields_json;    // raw declaration body (for later field-level access)
  std::string status;         // "registered" | "running" | "complete" | "aborted"
  // ── v1.5 NeamEvolve additions ──
  bool        evolve_mode = false;          // sub-type 31 sets this to true
  std::string belief_ref;                   // resolved from fields_json["belief"]
  std::string skills_ref;                   // resolved from fields_json["skills"]
  std::string curriculum_ref;               // P1
  std::string safety_program_ref;           // alignment-anchor purpose
  std::string safety_human_gate_ref;        // P2 design op gate
};

struct HandoffRecord {
  std::string name;
  std::string fields_json;
  std::string schema_version;  // extracted from fields_json for quick access
};

struct ToolRegistryRecord {
  std::string name;
  std::string fields_json;
};

struct AssertionRegistryRecord {
  std::string name;
  std::string fields_json;
};

struct HarnessBenchmarkRecord {
  std::string name;
  std::string fields_json;
};

// v1.4.5 Phase 7: per-forge-agent metadata populated by sub-type 30 emission.
// Introspection natives (forge_role_of etc.) read from here.
struct ForgeMetadataRecord {
  std::string name;
  std::string role;           // "planner" | "generator" | "evaluator" | ""
  std::string function_json;  // typed function block (submodule-as-tool)
  std::string ops_json;       // per-op task/step modifiers
};

// v1.5 NeamEvolve — versioned snapshot of a belief.
struct BeliefVersion {
  int         version;
  std::string text;
  std::string hash;            // SHA-256 hex of text
  std::string ts;              // ISO-8601 UTC
  std::string committed_by_trigger;
};

// v1.5 NeamEvolve — mutable belief runtime cell (sub-type 32).
struct BeliefRecord {
  std::string                name;
  std::string                initial_text;
  std::string                current_text;     // mutable runtime cell
  std::string                current_hash;     // SHA-256 of current_text
  std::string                constraints_ref;  // assertion_registry name
  std::string                revision_trigger; // "every_N_runs" | "performance_plateau" | "manual"
  int                        trigger_n = 5;
  int                        max_revisions_per_session = 10;
  bool                       rollback_enabled = true;
  float                      max_drift = 0.7f;
  float                      rollback_on_regression = 0.10f;
  std::string                distillation_method;
  std::vector<BeliefVersion> history;
  int                        revisions_this_run = 0;
};

// v1.5 NeamEvolve — one entry in a SkillLibraryRecord::skills map.
struct AcquiredSkill {
  std::string              name;
  std::string              hash;          // SHA-256 of canonical body
  std::string              body;          // single-file source (P0)
  std::vector<std::string> requires_capabilities;
  std::string              signed_by;     // agent_identity hex / agent name
  std::string              acquired_ts;   // ISO-8601 UTC
  bool                     active = true;
  int                      invocations = 0;
  int                      failures = 0;
};

// v1.5 NeamEvolve — runtime-acquired skill registry (sub-type 33).
struct SkillLibraryRecord {
  std::string                                       name;
  std::string                                       verify_method;     // "self_test"|"surrogate"|"evaluator_role"
  bool                                              verify_sandbox = true;
  int                                               deprecate_after_failures = 5;
  bool                                              allow_runtime_acquisition = true;
  std::vector<std::string>                          trusted_signers;
  std::unordered_map<std::string, AcquiredSkill>    skills;            // mutable cell
};

// v1.5 NeamEvolve — curriculum record (sub-type 34, P1).
struct CurriculumRecord {
  std::string              name;
  std::string              mode;              // "auto"|"co_evolve"|"manual"|"eval_set_iterator"
  std::string              difficulty_metric;
  float                    advance_threshold = 0.8f;
  float                    fallback_threshold = 0.4f;
  std::vector<std::string> task_pool;
  float                    current_difficulty = 0.5f;
  std::vector<bool>        recent_outcomes;   // rolling window
};

// ─── Process-wide registry ────────────────────────────────────────────

class HarnessRegistry {
 public:
  static HarnessRegistry& instance();

  // Registration (called from VM sub-type dispatch).
  void register_harness(HarnessRecord rec);
  void register_handoff(HandoffRecord rec);
  void register_tool_registry(ToolRegistryRecord rec);
  void register_assertion_registry(AssertionRegistryRecord rec);
  void register_harness_benchmark(HarnessBenchmarkRecord rec);
  void register_forge_metadata(ForgeMetadataRecord rec);
  // v1.5 NeamEvolve
  void register_belief(BeliefRecord rec);
  void register_skill_library(SkillLibraryRecord rec);
  void register_curriculum(CurriculumRecord rec);

  // Lookup (called from natives).
  const HarnessRecord* lookup_harness(const std::string& name) const;
  const HandoffRecord* lookup_handoff(const std::string& name) const;
  const ToolRegistryRecord* lookup_tool_registry(const std::string& name) const;
  const AssertionRegistryRecord* lookup_assertion_registry(const std::string& name) const;
  const HarnessBenchmarkRecord* lookup_harness_benchmark(const std::string& name) const;
  const ForgeMetadataRecord*    lookup_forge_metadata(const std::string& name) const;
  // v1.5 NeamEvolve
  const BeliefRecord*           lookup_belief(const std::string& name) const;
        BeliefRecord*           lookup_belief_mut(const std::string& name);
  const SkillLibraryRecord*     lookup_skill_library(const std::string& name) const;
        SkillLibraryRecord*     lookup_skill_library_mut(const std::string& name);
  const CurriculumRecord*       lookup_curriculum(const std::string& name) const;
        CurriculumRecord*       lookup_curriculum_mut(const std::string& name);

  // Set status on an existing harness (for harness_start / _complete / _abort
  // natives — currently only "registered" is set; other states land in Phase 3+).
  void set_harness_status(const std::string& name, const std::string& status);

  // Enumeration (for tests / introspection).
  std::vector<std::string> list_harnesses() const;

  // For tests.
  void reset_for_tests();

 private:
  HarnessRegistry() = default;
  mutable std::mutex mu_;
  std::unordered_map<std::string, HarnessRecord> harnesses_;
  std::unordered_map<std::string, HandoffRecord> handoffs_;
  std::unordered_map<std::string, ToolRegistryRecord> tool_registries_;
  std::unordered_map<std::string, AssertionRegistryRecord> assertion_registries_;
  std::unordered_map<std::string, HarnessBenchmarkRecord> benchmarks_;
  std::unordered_map<std::string, ForgeMetadataRecord>    forge_metadata_;
  // v1.5 NeamEvolve
  std::unordered_map<std::string, BeliefRecord>           beliefs_;
  std::unordered_map<std::string, SkillLibraryRecord>     skill_libraries_;
  std::unordered_map<std::string, CurriculumRecord>       curricula_;
};

// ─── Free functions ────────────────────────────────────────────────────

// Deterministic SHA-256 of (name || 0x1F || fields_json).  Used to fulfill
// FR-H-5: harness_hash(h) must be a pure function of the declaration.
// Returns lowercase hex (64 chars).
std::string compute_harness_hash(const std::string& name,
                                 const std::string& fields_json);

// Extracts a string field from a fields_json blob produced by
// parse_v10_generic_decl.  Returns empty string if missing.
std::string extract_json_string(const std::string& fields_json,
                                const std::string& key);

// Snapshot of the NEAM_RUN_* environment variables that the harness runtime
// seeds at VM startup.  Lazily initialized on first call; deterministic
// within a single VM process.
//
// Keys populated:
//   NEAM_RUN_ID
//   NEAM_RUN_TIMESTAMP  (ISO-8601 UTC)
// Phase 3+ adds: NEAM_RUN_HARNESS_HASH, _SANDBOX_DIR, _HOST_DIR, _MODEL
const std::map<std::string, std::string>& harness_runtime_env_map();

}  // namespace neamc::vm::harness
