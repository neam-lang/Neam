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

  // Lookup (called from natives).
  const HarnessRecord* lookup_harness(const std::string& name) const;
  const HandoffRecord* lookup_handoff(const std::string& name) const;
  const ToolRegistryRecord* lookup_tool_registry(const std::string& name) const;
  const AssertionRegistryRecord* lookup_assertion_registry(const std::string& name) const;
  const HarnessBenchmarkRecord* lookup_harness_benchmark(const std::string& name) const;

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
