// Neam v1.4.5 Phase 4 — Handoff runtime.
//
// Implements file-backed I/O for handoff declarations. Each handoff lives
// on disk (path declared in the Neam source), with schema validation on
// read/write. The atomic-write-then-rename pattern is used so a killed
// harness mid-write doesn't corrupt the file.
//
// Scope this file:
//   - handoff_write(name, section, content)
//   - handoff_read(name, section?)
//   - handoff_exists(name)
//   - handoff_size(name)
//   - handoff_validate(name)
//
// Out of scope (documented gaps):
//   - "summarize" overflow policy (needs LLM call — later)
//   - "append_log" versioning (delta JSONL) — later
//   - git versioning currently shells out via std::system; production
//     variant should use libgit2. Works for the common case.

#include "neamc/vm/handoff_runtime.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <unordered_map>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace neamc::vm::handoff {

namespace {

// Per-path write lock so two concurrent harness writes don't corrupt one
// another.  Held only for the short window of rename(tmp -> final).
std::unordered_map<std::string, std::mutex> g_path_locks;
std::mutex                                  g_path_locks_mu;

std::mutex& lock_for_path(const std::string& p) {
  std::lock_guard<std::mutex> lg(g_path_locks_mu);
  return g_path_locks[p];
}

std::string gen_tmp_suffix() {
  std::random_device rd;
  std::mt19937_64 rng(rd());
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream oss;
  oss << std::hex << dist(rng);
  return oss.str();
}

// Expand ~ and $NEAM_RUN_* style tokens.  For Phase 4 minimal we only
// expand the leading ~ (home dir).  Full run-env expansion lives in
// harness_types (Phase 3-minimal) and is additive.
std::string expand_path(const std::string& raw) {
  if (raw.empty() || raw[0] != '~') return raw;
  const char* home = std::getenv("HOME");
  if (!home) return raw;
  return std::string(home) + raw.substr(1);
}

}  // anonymous namespace

// ─── Public API ────────────────────────────────────────────────────────

HandoffIoResult write(const HandoffRecord& rec,
                      const std::string& section,
                      const std::string& content) {
  HandoffIoResult r;
  const std::string path = expand_path(rec.path_template);

  // Enforce max_size_kb.
  const auto size_kb = (content.size() + 1023) / 1024;
  if (rec.max_size_kb > 0 && static_cast<int>(size_kb) > rec.max_size_kb) {
    if (rec.on_overflow == "error") {
      r.ok = false;
      r.error = "HF-OVERFLOW: content " + std::to_string(size_kb) +
                "KB exceeds max_size_kb " + std::to_string(rec.max_size_kb);
      return r;
    }
    if (rec.on_overflow == "truncate") {
      // Keep the head; append a marker.
      const size_t cap = rec.max_size_kb * 1024;
      std::string truncated = content.substr(0, cap - 32);
      truncated += "\n<!-- truncated by handoff -->\n";
      return write_raw(rec, section, truncated);
    }
    // "summarize" falls through to write-as-is for Phase 4 minimal (needs LLM).
  }
  return write_raw(rec, section, content);
}

HandoffIoResult write_raw(const HandoffRecord& rec,
                          const std::string& section,
                          const std::string& content) {
  HandoffIoResult r;
  const std::string path = expand_path(rec.path_template);

  std::error_code ec;
  fs::create_directories(fs::path(path).parent_path(), ec);
  // ec on create_directories for "" parent is fine; ignore.

  std::lock_guard<std::mutex> lg(lock_for_path(path));

  // For markdown: upsert the section (create-or-replace "## section" block).
  // For json:     overwrite the whole file with {section: content} or merge.
  // For neam:     Phase 4 minimal treats as opaque text, same as markdown.
  std::string final_content;

  if (rec.schema == "markdown" || rec.schema == "neam" || rec.schema.empty()) {
    // Read existing
    std::string existing;
    if (fs::exists(path)) {
      std::ifstream in(path);
      std::ostringstream ss; ss << in.rdbuf();
      existing = ss.str();
    }
    final_content = upsert_markdown_section(existing, section, content);
  } else if (rec.schema == "json") {
    nlohmann::json doc;
    if (fs::exists(path)) {
      try {
        std::ifstream in(path);
        in >> doc;
      } catch (...) { doc = nlohmann::json::object(); }
    }
    if (!doc.is_object()) doc = nlohmann::json::object();
    // Heuristic: if content is valid JSON, store structured.  Else store string.
    try {
      doc[section] = nlohmann::json::parse(content);
    } catch (...) {
      doc[section] = content;
    }
    final_content = doc.dump(2);
  } else {
    r.ok = false;
    r.error = "HF-SCHEMA: unknown schema '" + rec.schema + "'";
    return r;
  }

  // Atomic write: tempfile + rename.
  const std::string tmp = path + ".tmp." + gen_tmp_suffix();
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) {
      r.ok = false;
      r.error = "HF-IO: cannot create tempfile " + tmp;
      return r;
    }
    out.write(final_content.data(), final_content.size());
  }
  fs::rename(tmp, path, ec);
  if (ec) {
    r.ok = false;
    r.error = "HF-IO: rename failed: " + ec.message();
    std::remove(tmp.c_str());
    return r;
  }

  // Versioning: git / append_log / none
  if (rec.versioning == "git") {
    const std::string cmd =
        "git -C \"" + fs::path(path).parent_path().string() +
        "\" add \"" + fs::path(path).filename().string() + "\" "
        "&& git -C \"" + fs::path(path).parent_path().string() +
        "\" commit -q -m \"handoff update: " + section + "\" > /dev/null 2>&1";
    // Best-effort: if the parent dir isn't a git repo, this silently no-ops.
    std::system(cmd.c_str());
  } else if (rec.versioning == "append_log") {
    const std::string log = path + ".deltas.jsonl";
    std::ofstream delta(log, std::ios::app);
    nlohmann::json entry = {
        {"section", section},
        {"size",    content.size()},
        {"ts",      0 /* filled in by Phase 4 trace writer when landed */}
    };
    delta << entry.dump() << "\n";
  }

  r.ok = true;
  r.bytes_written = final_content.size();
  return r;
}

HandoffIoResult read(const HandoffRecord& rec,
                     const std::string& section) {
  HandoffIoResult r;
  const std::string path = expand_path(rec.path_template);

  if (!fs::exists(path)) {
    r.ok = false;
    r.error = "HF-NOEXIST: " + path;
    return r;
  }

  std::ifstream in(path);
  if (!in) {
    r.ok = false;
    r.error = "HF-IO: cannot open " + path;
    return r;
  }
  std::ostringstream ss; ss << in.rdbuf();
  std::string whole = ss.str();

  if (section.empty()) {
    // Return the whole file.
    r.ok = true;
    r.content = std::move(whole);
    return r;
  }

  if (rec.schema == "json") {
    try {
      auto doc = nlohmann::json::parse(whole);
      auto it = doc.find(section);
      if (it == doc.end()) {
        r.ok = false;
        r.error = "HF-SECTION: section '" + section + "' not found";
        return r;
      }
      r.ok = true;
      r.content = it->is_string() ? it->get<std::string>() : it->dump(2);
      return r;
    } catch (const std::exception& e) {
      r.ok = false;
      r.error = std::string("HF-PARSE: ") + e.what();
      return r;
    }
  }

  // Markdown / neam / empty: extract a section body between "## <section>"
  // and the next "## " or EOF.
  auto body = extract_markdown_section(whole, section);
  if (!body.has_value()) {
    r.ok = false;
    r.error = "HF-SECTION: section '" + section + "' not found";
    return r;
  }
  r.ok = true;
  r.content = std::move(*body);
  return r;
}

bool exists(const HandoffRecord& rec) {
  return fs::exists(expand_path(rec.path_template));
}

std::size_t size_bytes(const HandoffRecord& rec) {
  std::error_code ec;
  auto sz = fs::file_size(expand_path(rec.path_template), ec);
  return ec ? 0 : sz;
}

HandoffIoResult validate(const HandoffRecord& rec) {
  HandoffIoResult r;
  const std::string path = expand_path(rec.path_template);
  if (!fs::exists(path)) {
    r.ok = false; r.error = "HF-NOEXIST: " + path; return r;
  }
  std::ifstream in(path);
  std::ostringstream ss; ss << in.rdbuf();
  std::string whole = ss.str();

  if (rec.schema == "json") {
    try { (void)nlohmann::json::parse(whole); }
    catch (const std::exception& e) {
      r.ok = false; r.error = std::string("HF-PARSE: ") + e.what(); return r;
    }
  } else if (rec.schema == "markdown" || rec.schema == "neam" || rec.schema.empty()) {
    // Check required_sections (caller must supply via rec.required_sections).
    for (const auto& sec : rec.required_sections) {
      if (!extract_markdown_section(whole, sec).has_value()) {
        r.ok = false;
        r.error = "HF-SECTION: required section '" + sec + "' missing";
        return r;
      }
    }
  }
  r.ok = true;
  return r;
}

// ─── Helpers (exported for testing) ────────────────────────────────────

std::string upsert_markdown_section(const std::string& doc,
                                    const std::string& section,
                                    const std::string& body) {
  // Find "## <section>" header at line start.
  const std::string header = "## " + section;
  auto at = doc.find(header);

  if (at == std::string::npos) {
    // Append new section.
    std::string out = doc;
    if (!out.empty() && out.back() != '\n') out += "\n";
    out += "\n" + header + "\n\n" + body;
    if (out.empty() || out.back() != '\n') out += "\n";
    return out;
  }

  // Replace existing section body up to the next "## " or EOF.
  auto body_start = doc.find('\n', at);
  if (body_start == std::string::npos) body_start = doc.size();
  else ++body_start;

  // Skip a single optional blank line after the header.
  auto next = doc.find("\n## ", body_start);
  auto body_end = (next == std::string::npos) ? doc.size() : next + 1;

  std::string out = doc.substr(0, body_start);
  out += "\n" + body;
  if (out.empty() || out.back() != '\n') out += "\n";
  if (body_end < doc.size()) out += doc.substr(body_end);
  return out;
}

std::optional<std::string> extract_markdown_section(const std::string& doc,
                                                    const std::string& section) {
  const std::string header = "## " + section;
  auto at = doc.find(header);
  if (at == std::string::npos) return std::nullopt;

  // Header must start at beginning of file or after a newline.
  if (at != 0 && doc[at - 1] != '\n') return std::nullopt;

  auto body_start = doc.find('\n', at);
  if (body_start == std::string::npos) return std::string{};
  ++body_start;

  auto next = doc.find("\n## ", body_start);
  auto body_end = (next == std::string::npos) ? doc.size() : next + 1;

  // Trim leading/trailing whitespace.
  std::string body = doc.substr(body_start, body_end - body_start);
  auto ltrim = body.find_first_not_of(" \t\r\n");
  auto rtrim = body.find_last_not_of(" \t\r\n");
  if (ltrim == std::string::npos) return std::string{};
  return body.substr(ltrim, rtrim - ltrim + 1);
}

}  // namespace neamc::vm::handoff
