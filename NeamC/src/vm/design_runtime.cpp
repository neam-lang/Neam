// v1.5 NeamEvolve P2 — Design operation runtime implementation.  Spec §13.

#include "neamc/vm/design_runtime.hpp"

#include "neamc/vm/harness_runtime.hpp"
#include "neamc/vm/harness_types.hpp"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <string>

namespace neamc::vm::design {

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

std::string uuid_lite()
{
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  std::mt19937_64 rng{static_cast<uint64_t>(ms)};
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream oss;
  oss << std::hex << ms << dist(rng);
  return oss.str();
}

void emit_proposed_event(const std::string& evolve_agent,
                         const std::string& candidate_hash)
{
  nlohmann::json e = {
    {"kind",           "design.proposed"},
    {"evolve_agent",   evolve_agent},
    {"candidate_hash", candidate_hash}
  };
  ::neamc::vm::harness::trace_emit_for_run_with_chain(evolve_agent, e.dump());
}

void emit_promoted_event(const std::string& evolve_agent,
                         const std::string& candidate_hash,
                         double score,
                         bool human_approved)
{
  nlohmann::json e = {
    {"kind",            "design.promoted"},
    {"evolve_agent",    evolve_agent},
    {"candidate_hash",  candidate_hash},
    {"score",           score},
    {"human_approved",  human_approved}
  };
  ::neamc::vm::harness::trace_emit_for_run_with_chain(evolve_agent, e.dump());
}

void append_design_archive(const std::string& evolve_agent,
                           const std::string& candidate_hash,
                           const std::string& candidate_source,
                           double score,
                           bool human_approved)
{
  // Resolve the harness's trace path; archive lives next to it.
  std::string trace_path = ::neamc::vm::harness::harness_trace_path(evolve_agent);
  std::filesystem::path archive_path;
  if (trace_path.empty()) {
    archive_path = std::filesystem::path("runs") / "design_archive.jsonl";
  } else {
    archive_path = std::filesystem::path(trace_path).parent_path() / "design_archive.jsonl";
  }
  std::error_code ec;
  std::filesystem::create_directories(archive_path.parent_path(), ec);
  std::ofstream f(archive_path, std::ios::app | std::ios::binary);
  if (!f) return;
  nlohmann::json e = {
    {"ts",              iso8601_utc_now()},
    {"evolve_agent",    evolve_agent},
    {"candidate_hash",  candidate_hash},
    {"score",           score},
    {"human_approved",  human_approved},
    {"source",          candidate_source}
  };
  f << e.dump() << "\n";
}

}  // anonymous namespace

DesignResult design_propose(const std::string& evolve_agent_name,
                            const std::string& goal)
{
  // E-017: must be inside an active evolve scope.
  if (!::neamc::vm::harness::is_run_active(evolve_agent_name)) {
    return {false, "[design error E-017] not in active evolve run", "DS-NOTACTIVE"};
  }
  // P2 P0-stub: emit a deterministic candidate template.  A real design op
  // would invoke an LLM to generate Neam source.  This stub is enough to
  // round-trip the propose -> compile -> promote pipeline in tests.
  std::ostringstream cand;
  cand << "// v1.5 design_propose stub for evolve_agent=" << evolve_agent_name << "\n"
       << "// goal: " << goal << "\n"
       << "fun helper() { return 1; }\n";
  std::string source = cand.str();

  emit_proposed_event(evolve_agent_name, sha256_hex(source));
  return {true, source, ""};
}

DesignResult design_compile_in_sandbox(const std::string& candidate_source)
{
  // Per-call temporary directory.
  std::filesystem::path tmpdir = std::filesystem::temp_directory_path()
                               / ("neam_design_" + uuid_lite());
  std::error_code ec;
  std::filesystem::create_directories(tmpdir, ec);
  if (ec) return {false, "[design error DS-IO] " + ec.message(), "DS-IO"};

  std::filesystem::path candidate_path = tmpdir / "candidate.neam";
  {
    std::ofstream f(candidate_path);
    f << candidate_source;
  }

  // Locate the neamc binary — it's the parent process, but for sandbox we
  // prefer to spawn a fresh one.  In dry-run mode, skip the actual spawn.
  if (dry_run_mode()) {
    std::filesystem::remove_all(tmpdir, ec);
    return {true, "ok", ""};
  }

  // Best-effort: invoke `neamc` from PATH (works in normal install).  For P0
  // we accept that the parent's neamc may not be on PATH; in that case the
  // test path uses dry-run.  A production implementation would resolve the
  // binary path more carefully and apply seccomp/sandbox-exec.
  std::ostringstream cmd;
  cmd << "neamc " << candidate_path.string()
      << " -o " << (tmpdir / "candidate.neamb").string()
      << " 2>&1";
  // Apply a soft timeout via popen — caller is the tester and trusts the
  // child to finish.  Real sandbox is platform-specific.
#if defined(_WIN32)
  FILE* p = _popen(cmd.str().c_str(), "r");
#else
  FILE* p = popen(cmd.str().c_str(), "r");
#endif
  if (!p) {
    std::filesystem::remove_all(tmpdir, ec);
    return {false, "[design error DS-IO] popen failed", "DS-IO"};
  }
  std::string out;
  char buf[256];
  while (fgets(buf, sizeof(buf), p)) out += buf;
#if defined(_WIN32)
  int status = _pclose(p);
#else
  int status = pclose(p);
#endif
  std::filesystem::remove_all(tmpdir, ec);
  if (status != 0) {
    return {false, "[design error DS-COMPILE] " + out, "DS-COMPILE"};
  }
  return {true, "ok", ""};
}

double design_score(const std::string& /*candidate_source*/,
                    const std::string& /*eval_set_name*/)
{
  // P2 stub: real implementation would invoke v1.2 eval_set runner.
  // Returns 0.5 (mid-range) so callers can integrate scoring without
  // requiring a real eval_set wiring in the tests.
  return 0.5;
}

DesignResult design_promote(const std::string& evolve_agent_name,
                            const std::string& candidate_source)
{
  const auto* rec = ::neamc::vm::harness::HarnessRegistry::instance()
                     .lookup_harness(evolve_agent_name);
  if (!rec) return {false, "", "DS-UNKNOWN-AGENT"};
  if (rec->safety_human_gate_ref.empty()) {
    return {false, "[design error E-010] human_gate required for promote", "DS-NOGATE"};
  }

  // P0 stub: in dry-run mode, treat human_gate as approving.  In production
  // this would block on the gate's approval flow.
  bool approved = dry_run_mode();
  std::string candidate_hash = sha256_hex(candidate_source);
  double score = design_score(candidate_source, "");

  emit_promoted_event(evolve_agent_name, candidate_hash, score, approved);
  append_design_archive(evolve_agent_name, candidate_hash, candidate_source,
                        score, approved);

  if (!approved) {
    return {false, "[design error HUMAN-DENY] gate did not approve",
            "DS-DENY"};
  }
  return {true, "ok", ""};
}

}  // namespace neamc::vm::design
