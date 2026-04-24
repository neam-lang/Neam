// Neam v1.4.5 Phase 4 — Handoff runtime API.

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace neamc::vm::handoff {

// Minimal record for runtime ops. Phase 3-minimal's HarnessRegistry
// populates a HandoffRecord with the declaration body in fields_json;
// Phase 4 natives unpack into this view before calling these functions.
struct HandoffRecord {
  std::string name;
  std::string path_template;       // may include ~
  std::string schema;              // "markdown" | "json" | "neam" | ""
  std::vector<std::string> required_sections;
  int max_size_kb = 0;             // 0 = unlimited
  std::string on_overflow;         // "error" | "truncate" | "summarize"
  std::string versioning;          // "none" | "git" | "append_log"
  std::string on_read;             // "strict" | "lenient"
  std::string on_write;            // "strict" | "lenient"
  std::string schema_version;
};

struct HandoffIoResult {
  bool ok = true;
  std::string content;      // for read()
  std::size_t bytes_written = 0; // for write()
  std::string error;        // populated when ok == false; prefix identifies class:
                            //   HF-OVERFLOW, HF-IO, HF-SCHEMA, HF-NOEXIST,
                            //   HF-SECTION, HF-PARSE
};

// Write a section to a handoff, enforcing size/overflow policies.
HandoffIoResult write(const HandoffRecord& rec,
                      const std::string& section,
                      const std::string& content);

// Write ignoring overflow (used internally after truncation).
HandoffIoResult write_raw(const HandoffRecord& rec,
                          const std::string& section,
                          const std::string& content);

// Read a section, or the whole file if section is empty.
HandoffIoResult read(const HandoffRecord& rec,
                     const std::string& section);

bool        exists(const HandoffRecord& rec);
std::size_t size_bytes(const HandoffRecord& rec);

// Validate the on-disk file against rec.schema and rec.required_sections.
HandoffIoResult validate(const HandoffRecord& rec);

// ─── Exposed helpers (for testing) ─────────────────────────────────────

// Upsert "## <section>" block in a markdown doc. Creates if absent.
std::string upsert_markdown_section(const std::string& doc,
                                    const std::string& section,
                                    const std::string& body);

// Extract the body under "## <section>", or nullopt.
std::optional<std::string> extract_markdown_section(const std::string& doc,
                                                    const std::string& section);

}  // namespace neamc::vm::handoff
