// v1.5 NeamEvolve — Belief runtime implementation.
// See header for surface contract; spec §8.

#include "neamc/vm/belief_runtime.hpp"

#include "neamc/vm/harness_runtime.hpp"
#include "neamc/vm/harness_types.hpp"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>

namespace neamc::vm::belief {

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

// Levenshtein-distance fallback for cumulative drift (NFR-EVO-1).  We don't
// wire up the embedding model here — that requires the knowledge subsystem.
// For P0, Levenshtein is adequate to bound runaway belief mutation.
size_t levenshtein(const std::string& a, const std::string& b)
{
  const size_t m = a.size(), n = b.size();
  if (m == 0) return n;
  if (n == 0) return m;
  std::vector<std::vector<size_t>> d(m + 1, std::vector<size_t>(n + 1));
  for (size_t i = 0; i <= m; i++) d[i][0] = i;
  for (size_t j = 0; j <= n; j++) d[0][j] = j;
  for (size_t i = 1; i <= m; i++) {
    for (size_t j = 1; j <= n; j++) {
      size_t cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      d[i][j] = std::min({d[i - 1][j] + 1, d[i][j - 1] + 1, d[i - 1][j - 1] + cost});
    }
  }
  return d[m][n];
}

float compute_cumulative_drift(const std::string& initial, const std::string& current)
{
  size_t lev = levenshtein(initial, current);
  size_t denom = std::max(initial.size(), current.size());
  return denom == 0 ? 0.0f : static_cast<float>(lev) / static_cast<float>(denom);
}

std::string trigger_label(const std::string& trigger_str)
{
  // Tolerated: returns the raw trigger string. Extension point for future
  // human-friendly mapping.
  return trigger_str;
}

void emit_revision_event(const std::string& evolve_agent,
                         const std::string& before_hash,
                         const std::string& after_hash,
                         const std::string& trigger,
                         bool committed,
                         const std::string& reason)
{
  nlohmann::json e = {
    {"kind",         "belief.revision"},
    {"evolve_agent", evolve_agent},
    {"before_hash",  before_hash},
    {"after_hash",   after_hash},
    {"trigger",      trigger},
    {"committed",    committed}
  };
  if (!reason.empty()) e["reason"] = reason;
  ::neamc::vm::harness::trace_emit_for_run_with_chain(evolve_agent, e.dump());
}

void emit_rollback_event(const std::string& evolve_agent,
                         const std::string& from_hash,
                         const std::string& to_hash,
                         const std::string& reason)
{
  nlohmann::json e = {
    {"kind",         "belief.rollback"},
    {"evolve_agent", evolve_agent},
    {"from_hash",    from_hash},
    {"to_hash",      to_hash},
    {"reason",       reason}
  };
  ::neamc::vm::harness::trace_emit_for_run_with_chain(evolve_agent, e.dump());
}

// Locate the evolve agent that "owns" this belief — i.e., the unique active
// HarnessRecord with evolve_mode=true and belief_ref==belief_name.  Returns
// "" if no active evolve agent references it (callers MUST treat that as
// out-of-evolve-scope per FR-EA-12 / NFR-SEC-3).
std::string find_active_evolve_agent_for_belief(const std::string& belief_name)
{
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  for (const auto& name : reg.list_harnesses()) {
    const auto* h = reg.lookup_harness(name);
    if (!h || !h->evolve_mode) continue;
    if (h->belief_ref != belief_name) continue;
    if (::neamc::vm::harness::is_run_active(name)) return name;
  }
  return {};
}

}  // anonymous namespace

// ─── Public API ────────────────────────────────────────────────────────

BeliefResult belief_text(const std::string& name)
{
  const auto* b = ::neamc::vm::harness::HarnessRegistry::instance().lookup_belief(name);
  if (!b) return {false, "", "BL-UNKNOWN"};
  return {true, b->current_text, ""};
}

BeliefResult belief_revise(const std::string& name,
                           const std::string& new_text,
                           const std::string& evolve_agent_name)
{
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  auto* b = reg.lookup_belief_mut(name);
  if (!b) return {false, "", "BL-UNKNOWN"};

  // Resolve the active evolve scope.  If the caller didn't pass one, infer
  // from the registry — but reject if no active evolve agent owns this belief
  // (FR-EA-12 / NFR-SEC-3: belief revision is only allowed inside an evolve run).
  std::string ea = evolve_agent_name;
  if (ea.empty()) ea = find_active_evolve_agent_for_belief(name);
  if (ea.empty() || !::neamc::vm::harness::is_run_active(ea)) {
    return {false, "[E-SBX-1] not in evolve scope", "BL-SCOPE"};
  }

  // Max-revisions guard (FR-BL-5 / NFR-EVO-3)
  if (b->revisions_this_run >= b->max_revisions_per_session) {
    emit_revision_event(ea, b->current_hash, "", "max_revisions",
                        false, "max_revisions_per_session exceeded");
    return {false, "[E-EVO-MAX-REV] exceeded "
            + std::to_string(b->max_revisions_per_session), "BL-MAX-REV"};
  }

  // Constraint kernel — REUSE harness_runtime's evaluator (FR-AAN-2)
  std::string violated =
      ::neamc::vm::harness::evaluate_hard_regex_assertions_pub(b->constraints_ref, new_text);
  if (!violated.empty()) {
    emit_revision_event(ea, b->current_hash, "", "constraint",
                        false, "hard assertion violated: " + violated);
    return {false, "[E-AAN-violation] " + violated, "BL-CONSTRAINTS"};
  }

  // Drift bound (NFR-EVO-1)
  float drift = compute_cumulative_drift(b->initial_text, new_text);
  if (drift > b->max_drift) {
    emit_revision_event(ea, b->current_hash, "", "drift_exceeded", false,
                        "drift " + std::to_string(drift)
                        + " > " + std::to_string(b->max_drift));
    // Per NFR-EVO-1: drift exceedance triggers abort of the parent evolve run.
    ::neamc::vm::harness::harness_abort(ea, "BELIEF_DRIFT_EXCEEDED");
    return {false, "[E-EVO-DRIFT] exceeded", "BL-DRIFT"};
  }

  // Commit
  std::string before_hash = b->current_hash;
  std::string new_hash    = sha256_hex(new_text);
  b->current_text = new_text;
  b->current_hash = new_hash;

  ::neamc::vm::harness::BeliefVersion v;
  v.version              = static_cast<int>(b->history.size());
  v.text                 = new_text;
  v.hash                 = new_hash;
  v.ts                   = iso8601_utc_now();
  v.committed_by_trigger = trigger_label(b->revision_trigger);
  b->history.push_back(std::move(v));
  b->revisions_this_run += 1;

  emit_revision_event(ea, before_hash, new_hash,
                      trigger_label(b->revision_trigger),
                      true, "");
  return {true, "ok", ""};
}

BeliefResult belief_rollback(const std::string& name,
                             const std::string& version_hash)
{
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  auto* b = reg.lookup_belief_mut(name);
  if (!b) return {false, "", "BL-UNKNOWN"};
  if (b->history.size() < 2) return {false, "no prior version", "BL-NOPRIOR"};

  const ::neamc::vm::harness::BeliefVersion* target = nullptr;
  if (version_hash.empty()) {
    target = &b->history[b->history.size() - 2];   // immediate prior
  } else {
    for (const auto& v : b->history) {
      if (v.hash == version_hash) { target = &v; break; }
    }
    if (!target) return {false, "version not found", "BL-NOVER"};
  }

  std::string from_hash = b->current_hash;
  b->current_text = target->text;
  b->current_hash = target->hash;
  // revisions_this_run does NOT change (FR-BL-10) — rollback is not a revision.

  std::string ea = find_active_evolve_agent_for_belief(name);
  if (!ea.empty()) {
    emit_rollback_event(ea, from_hash, target->hash, "user_request");
  }
  return {true, "ok", ""};
}

std::string belief_history_json(const std::string& name)
{
  const auto* b = ::neamc::vm::harness::HarnessRegistry::instance().lookup_belief(name);
  if (!b) return "[]";
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& v : b->history) {
    arr.push_back({
      {"version", v.version},
      {"hash",    v.hash},
      {"ts",      v.ts},
      {"committed_by_trigger", v.committed_by_trigger}
    });
  }
  return arr.dump();
}

std::string belief_diff(const std::string& name,
                        const std::string& va_hash,
                        const std::string& vb_hash)
{
  const auto* b = ::neamc::vm::harness::HarnessRegistry::instance().lookup_belief(name);
  if (!b) return "[belief_diff error BL-UNKNOWN]";
  const std::string* a = nullptr;
  const std::string* c = nullptr;
  for (const auto& v : b->history) {
    if (v.hash == va_hash) a = &v.text;
    if (v.hash == vb_hash) c = &v.text;
  }
  if (!a || !c) return "[belief_diff error BL-NOVER]";
  if (*a == *c) return "(identical)";
  // Minimal: report sizes + a single first-divergence offset.  A full unified
  // diff is overkill for a P0 sentinel implementation.
  std::ostringstream os;
  os << "size_a=" << a->size() << " size_b=" << c->size();
  size_t i = 0;
  while (i < a->size() && i < c->size() && (*a)[i] == (*c)[i]) ++i;
  os << " first_diff_at=" << i;
  return os.str();
}

std::string belief_hash(const std::string& name, int version)
{
  const auto* b = ::neamc::vm::harness::HarnessRegistry::instance().lookup_belief(name);
  if (!b) return "";
  if (version < 0) return b->current_hash;
  if (version >= 0 && static_cast<size_t>(version) < b->history.size())
    return b->history[version].hash;
  return "";
}

}  // namespace neamc::vm::belief
