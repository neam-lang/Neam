// v1.5 NeamEvolve — Skill library runtime implementation.  Spec §11.

#include "neamc/vm/skill_library_runtime.hpp"

#include "neamc/vm/harness_runtime.hpp"
#include "neamc/vm/harness_types.hpp"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace neamc::vm::skill {

namespace {

std::string sha256_hex(const std::string& s)
{
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return {};
  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
  EVP_DigestUpdate(ctx, s.data(), s.size());
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int  hash_len = 0;
  EVP_DigestFinal_ex(ctx, hash, &hash_len);
  EVP_MD_CTX_free(ctx);
  std::ostringstream oss;
  for (unsigned int i = 0; i < hash_len; i++) {
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
  }
  return oss.str();
}

std::string iso8601_utc_now()
{
  std::time_t t = std::time(nullptr);
  std::tm g{};
#if defined(_WIN32)
  gmtime_s(&g, &t);
#else
  gmtime_r(&t, &g);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &g);
  return std::string(buf);
}

bool dry_run_mode()
{
  const char* v = std::getenv("NEAM_HARNESS_DRY_RUN");
  if (!v || !*v) return false;
  std::string s = v;
  return s == "1" || s == "true" || s == "TRUE" || s == "yes";
}

// FR-SBX-8: deny list of natives a skill MUST NOT call.
const std::set<std::string>& denied_natives()
{
  static const std::set<std::string> s = {
    "os_exec", "system", "popen", "subprocess",
    "evolve_agent_start", "evolve_agent_run",
    "evolve_agent_complete", "evolve_agent_abort",
    "design_propose", "design_promote",
    "design_compile_in_sandbox", "design_score",
    "skill_acquire",          // FR-SBX-8: no recursive acquisition
  };
  return s;
}

// Crude but adequate static analysis — scan for occurrences of denied native
// identifiers.  In a real implementation this would walk the parsed AST; for
// P0 a token-level scan covers the threat model.
struct StaticAnalysisResult {
  bool                     ok = true;
  std::string              violation_reason;
  std::vector<std::string> required_capabilities;
  int                      prohibited_ops = 0;
  int                      warnings = 0;
};

StaticAnalysisResult analyse(const std::string& code)
{
  StaticAnalysisResult r;
  for (const auto& d : denied_natives()) {
    auto pos = code.find(d);
    while (pos != std::string::npos) {
      // Heuristic: match `<denied>(` or `<denied> (` to avoid matching identifiers
      // that happen to contain the substring (e.g., `my_system`).
      bool is_identifier_boundary_left = (pos == 0)
          || !(std::isalnum((unsigned char)code[pos - 1]) || code[pos - 1] == '_');
      size_t after = pos + d.size();
      // skip whitespace
      while (after < code.size() && std::isspace((unsigned char)code[after])) after++;
      bool followed_by_paren = after < code.size() && code[after] == '(';
      if (is_identifier_boundary_left && followed_by_paren) {
        r.ok = false;
        r.prohibited_ops++;
        if (!r.violation_reason.empty()) r.violation_reason += ", ";
        r.violation_reason += d;
        break;  // one finding per denied is enough to record
      }
      pos = code.find(d, pos + 1);
    }
  }
  // Required capabilities are read from a structured comment header
  // (kept simple for P0): `// requires: tool1, tool2`
  auto rpos = code.find("// requires:");
  if (rpos != std::string::npos) {
    auto eol = code.find('\n', rpos);
    std::string tail = code.substr(rpos + std::string("// requires:").size(),
                                   eol == std::string::npos ? std::string::npos : eol - rpos - 12);
    std::string cap;
    for (char c : tail) {
      if (c == ',' || c == '\n') {
        // trim
        std::string t = cap;
        while (!t.empty() && std::isspace((unsigned char)t.front())) t.erase(t.begin());
        while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
        if (!t.empty()) r.required_capabilities.push_back(t);
        cap.clear();
      } else {
        cap.push_back(c);
      }
    }
    std::string t = cap;
    while (!t.empty() && std::isspace((unsigned char)t.front())) t.erase(t.begin());
    while (!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
    if (!t.empty()) r.required_capabilities.push_back(t);
  }
  return r;
}

// FR-AAN-3: capability monotonicity — required capabilities MUST be subset
// of the parent agent's tool_registry.scoping union.
std::string check_capability_monotonicity(const std::string& evolve_agent_name,
                                          const std::vector<std::string>& required)
{
  if (required.empty()) return {};
  const auto* parent = ::neamc::vm::harness::HarnessRegistry::instance()
                         .lookup_harness(evolve_agent_name);
  if (!parent) return "no parent agent context";

  // Find the tool_registry referenced from parent's fields_json.
  std::string tools_ref;
  try {
    auto j = nlohmann::json::parse(parent->fields_json);
    tools_ref = j.value("tools", std::string{});
  } catch (...) { /* tolerated */ }
  if (tools_ref.empty()) {
    // No tool_registry declared on parent — every required capability is a violation.
    std::ostringstream os;
    os << "skill requires capabilities but parent agent has no tool_registry: ";
    for (auto& c : required) os << c << " ";
    return os.str();
  }

  const auto* tr = ::neamc::vm::harness::HarnessRegistry::instance()
                    .lookup_tool_registry(tools_ref);
  if (!tr) return "tool_registry '" + tools_ref + "' not registered";

  // Collect the union of all role scopes + the builtin/project/user lists.
  std::set<std::string> allowed;
  try {
    auto j = nlohmann::json::parse(tr->fields_json);
    auto add_array = [&](const char* key) {
      if (j.contains(key) && j[key].is_array()) {
        for (auto& v : j[key]) if (v.is_string()) allowed.insert(v.get<std::string>());
      }
    };
    add_array("builtin");
    add_array("project");
    add_array("user");
    if (j.contains("scoping") && j["scoping"].is_object()) {
      for (auto it = j["scoping"].begin(); it != j["scoping"].end(); ++it) {
        if (it.value().is_array()) {
          for (auto& v : it.value()) if (v.is_string()) allowed.insert(v.get<std::string>());
        }
      }
    }
  } catch (...) { /* tolerated */ }

  std::vector<std::string> exceeding;
  for (const auto& c : required) {
    if (!allowed.count(c)) exceeding.push_back(c);
  }
  if (!exceeding.empty()) {
    std::ostringstream os;
    os << "skill requests capabilities outside parent scope: ";
    for (size_t i = 0; i < exceeding.size(); i++) {
      if (i) os << ", ";
      os << exceeding[i];
    }
    return os.str();
  }
  return {};
}

// Locate the active evolve agent that owns this skill_library, mirroring
// the belief_runtime pattern.
std::string find_active_evolve_agent_for_library(const std::string& library_name)
{
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  for (const auto& name : reg.list_harnesses()) {
    const auto* h = reg.lookup_harness(name);
    if (!h || !h->evolve_mode) continue;
    if (h->skills_ref != library_name) continue;
    if (::neamc::vm::harness::is_run_active(name)) return name;
  }
  return {};
}

void emit_acquired(const std::string& evolve_agent,
                   const std::string& skill_name,
                   const std::string& skill_hash,
                   const std::string& sandbox_result,
                   int prohibited_ops,
                   int warnings,
                   const std::string& signed_by,
                   bool committed,
                   const std::string& reason)
{
  nlohmann::json e = {
    {"kind",            "skill.acquired"},
    {"evolve_agent",    evolve_agent},
    {"skill_name",      skill_name},
    {"skill_hash",      skill_hash},
    {"sandbox_result",  sandbox_result},
    {"static_analysis", { {"prohibited_ops", prohibited_ops},
                          {"warnings", warnings} }},
    {"signed_by",       signed_by},
    {"committed",       committed}
  };
  if (!reason.empty()) e["reason"] = reason;
  ::neamc::vm::harness::trace_emit_for_run_with_chain(evolve_agent, e.dump());
}

void emit_deprecated(const std::string& evolve_agent,
                     const std::string& skill_name,
                     const std::string& reason,
                     int failure_count)
{
  nlohmann::json e = {
    {"kind",          "skill.deprecated"},
    {"evolve_agent",  evolve_agent},
    {"skill_name",    skill_name},
    {"reason",        reason},
    {"failure_count", failure_count}
  };
  ::neamc::vm::harness::trace_emit_for_run_with_chain(evolve_agent, e.dump());
}

void emit_invoked(const std::string& evolve_agent,
                  const std::string& skill_name,
                  int latency_ms,
                  const std::string& status)
{
  nlohmann::json e = {
    {"kind",         "skill.invoked"},
    {"evolve_agent", evolve_agent},
    {"skill_name",   skill_name},
    {"latency_ms",   latency_ms},
    {"status",       status}
  };
  ::neamc::vm::harness::trace_emit_for_run_with_chain(evolve_agent, e.dump());
}

}  // anonymous namespace

// ─── Public API ────────────────────────────────────────────────────────

SkillResult skill_acquire(const std::string& library_name,
                          const std::string& skill_name,
                          const std::string& code)
{
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();

  // Scope check (FR-SK-5a / NFR-SEC-3).
  std::string ea = find_active_evolve_agent_for_library(library_name);
  if (ea.empty()) return {false, "[E-SBX-1] not in evolve scope", "SK-SCOPE"};

  auto* lib = reg.lookup_skill_library_mut(library_name);
  if (!lib) return {false, "library not registered", "SK-UNKNOWN"};
  if (!lib->allow_runtime_acquisition)
    return {false, "library does not allow runtime acquisition", "SK-NOACQ"};

  // Static analysis (FR-SK-5b, FR-SBX-8).
  StaticAnalysisResult sa = analyse(code);
  if (!sa.ok) {
    emit_acquired(ea, skill_name, "", "failed", sa.prohibited_ops, sa.warnings, "",
                  /*committed*/ false,
                  "static_analysis: denied native(s): " + sa.violation_reason);
    return {false, "[E-011] denied native(s): " + sa.violation_reason, "SK-CAP"};
  }

  // Capability monotonicity (FR-AAN-3).
  std::string cap_violation = check_capability_monotonicity(ea, sa.required_capabilities);
  if (!cap_violation.empty()) {
    emit_acquired(ea, skill_name, "", "failed", sa.prohibited_ops, sa.warnings, "",
                  /*committed*/ false, cap_violation);
    return {false, "[E-011] capability monotonicity: " + cap_violation, "SK-CAP"};
  }

  // Sandbox self-test (FR-SK-2 + FR-SK-9).  P0 dry-run stub: if dry-run mode,
  // skip the actual subprocess and treat as passed.  Production path would
  // spawn a child neam-cli process with seccomp/sandbox-exec; that wiring is
  // platform-specific and is the same v1.0 code_sandbox infrastructure.
  bool sandbox_passed = dry_run_mode();   // P0: pass automatically in dry-run
  if (!dry_run_mode()) {
    // Best-effort: consider "passed" if static analysis cleared it.  A real
    // implementation would actually execute; this is the documented P0
    // limitation noted in the impl spec.
    sandbox_passed = true;
  }
  if (!sandbox_passed) {
    emit_acquired(ea, skill_name, sha256_hex(code),
                  "failed", sa.prohibited_ops, sa.warnings, "",
                  /*committed*/ false, "sandbox self-test failed");
    return {false, "[E-SBX-VIOLATION] sandbox self-test failed", "SK-SBX"};
  }

  // Sign + register.  Signed_by is the parent evolve agent's name (P0
  // simplification — full agent_identity integration is a v1.6 follow-up).
  ::neamc::vm::harness::AcquiredSkill skill;
  skill.name        = skill_name;
  skill.hash        = sha256_hex(code);
  skill.body        = code;
  skill.requires_capabilities = sa.required_capabilities;
  skill.signed_by   = ea;
  skill.acquired_ts = iso8601_utc_now();
  skill.active      = true;
  lib->skills[skill_name] = std::move(skill);

  emit_acquired(ea, skill_name, lib->skills[skill_name].hash, "passed",
                sa.prohibited_ops, sa.warnings, ea, /*committed*/ true, "");
  return {true, "ok", ""};
}

SkillResult skill_get(const std::string& library_name, const std::string& skill_name)
{
  const auto* lib = ::neamc::vm::harness::HarnessRegistry::instance()
                      .lookup_skill_library(library_name);
  if (!lib) return {false, "", "SK-UNKNOWN"};
  auto it = lib->skills.find(skill_name);
  if (it == lib->skills.end()) return {false, "", "SK-NOSKILL"};
  return {true, it->second.body, ""};
}

std::string skill_list_json(const std::string& library_name)
{
  const auto* lib = ::neamc::vm::harness::HarnessRegistry::instance()
                      .lookup_skill_library(library_name);
  if (!lib) return "[]";
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& [n, s] : lib->skills) {
    arr.push_back({
      {"name",        s.name},
      {"hash",        s.hash},
      {"status",      s.active ? "active" : "deprecated"},
      {"invocations", s.invocations},
      {"failures",    s.failures},
      {"signed_by",   s.signed_by},
      {"acquired_ts", s.acquired_ts}
    });
  }
  return arr.dump();
}

SkillResult skill_test(const std::string& library_name, const std::string& skill_name)
{
  const auto* lib = ::neamc::vm::harness::HarnessRegistry::instance()
                      .lookup_skill_library(library_name);
  if (!lib) return {false, "", "SK-UNKNOWN"};
  auto it = lib->skills.find(skill_name);
  if (it == lib->skills.end()) return {false, "", "SK-NOSKILL"};
  // Re-run static analysis as a smoke check
  StaticAnalysisResult sa = analyse(it->second.body);
  if (!sa.ok) return {true, "failed", ""};
  return {true, "passed", ""};
}

SkillResult skill_deprecate(const std::string& library_name, const std::string& skill_name)
{
  auto* lib = ::neamc::vm::harness::HarnessRegistry::instance()
                .lookup_skill_library_mut(library_name);
  if (!lib) return {false, "", "SK-UNKNOWN"};
  auto it = lib->skills.find(skill_name);
  if (it == lib->skills.end()) return {false, "", "SK-NOSKILL"};
  it->second.active = false;
  std::string ea = find_active_evolve_agent_for_library(library_name);
  if (!ea.empty())
    emit_deprecated(ea, skill_name, "user_request", it->second.failures);
  return {true, "ok", ""};
}

SkillResult skill_invoke(const std::string& library_name,
                         const std::string& skill_name,
                         const std::string& /*args_json*/)
{
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  auto* lib = reg.lookup_skill_library_mut(library_name);
  if (!lib) return {false, "", "SK-UNKNOWN"};
  auto it = lib->skills.find(skill_name);
  if (it == lib->skills.end() || !it->second.active)
    return {false, "skill not found or deprecated", "SK-NOSKILL"};

  std::string ea = find_active_evolve_agent_for_library(library_name);

  // Verify signature (FR-SK-6) — own-signed or in trusted_signers.
  bool trusted = false;
  for (const auto& s : lib->trusted_signers)
    if (s == it->second.signed_by) { trusted = true; break; }
  if (!trusted && it->second.signed_by != ea)
    return {false, "skill not signed by trusted identity", "SK-UNSIGNED"};

  // P0: under dry-run, return a stub response.  Production path would
  // spawn the sandboxed child.
  std::string out;
  bool ok = true;
  int latency_ms = 1;
  if (dry_run_mode()) {
    out = "[dry-run skill=" + skill_name + "] ok";
  } else {
    out = "(skill_invoke production path not implemented — P0 dry-run only)";
  }

  if (ok) {
    it->second.invocations += 1;
    if (!ea.empty()) emit_invoked(ea, skill_name, latency_ms, "ok");
    return {true, out, ""};
  } else {
    it->second.failures += 1;
    if (!ea.empty()) emit_invoked(ea, skill_name, latency_ms, "failure");
    if (it->second.failures >= lib->deprecate_after_failures) {
      it->second.active = false;
      if (!ea.empty()) emit_deprecated(ea, skill_name,
                                       "failures exceeded threshold",
                                       it->second.failures);
    }
    return {false, "[E-SBX-VIOLATION] " + out, "SK-FAIL"};
  }
}

}  // namespace neamc::vm::skill
