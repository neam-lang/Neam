// Neam v1.4.5 NeamHarness — side-table runtime state implementation.

#include "neamc/vm/harness_types.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <random>
#include <sstream>
#include <ctime>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

namespace neamc::vm::harness {

// ─── HarnessRegistry ───────────────────────────────────────────────────

HarnessRegistry& HarnessRegistry::instance() {
  static HarnessRegistry inst;
  return inst;
}

void HarnessRegistry::register_harness(HarnessRecord rec) {
  std::lock_guard<std::mutex> lk(mu_);
  harnesses_[rec.name] = std::move(rec);
}

void HarnessRegistry::register_handoff(HandoffRecord rec) {
  std::lock_guard<std::mutex> lk(mu_);
  handoffs_[rec.name] = std::move(rec);
}

void HarnessRegistry::register_tool_registry(ToolRegistryRecord rec) {
  std::lock_guard<std::mutex> lk(mu_);
  tool_registries_[rec.name] = std::move(rec);
}

void HarnessRegistry::register_assertion_registry(AssertionRegistryRecord rec) {
  std::lock_guard<std::mutex> lk(mu_);
  assertion_registries_[rec.name] = std::move(rec);
}

void HarnessRegistry::register_harness_benchmark(HarnessBenchmarkRecord rec) {
  std::lock_guard<std::mutex> lk(mu_);
  benchmarks_[rec.name] = std::move(rec);
}

void HarnessRegistry::register_forge_metadata(ForgeMetadataRecord rec) {
  std::lock_guard<std::mutex> lk(mu_);
  forge_metadata_[rec.name] = std::move(rec);
}

const HarnessRecord* HarnessRegistry::lookup_harness(const std::string& name) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = harnesses_.find(name);
  return it == harnesses_.end() ? nullptr : &it->second;
}

const HandoffRecord* HarnessRegistry::lookup_handoff(const std::string& name) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = handoffs_.find(name);
  return it == handoffs_.end() ? nullptr : &it->second;
}

const ToolRegistryRecord* HarnessRegistry::lookup_tool_registry(const std::string& name) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = tool_registries_.find(name);
  return it == tool_registries_.end() ? nullptr : &it->second;
}

const AssertionRegistryRecord* HarnessRegistry::lookup_assertion_registry(const std::string& name) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = assertion_registries_.find(name);
  return it == assertion_registries_.end() ? nullptr : &it->second;
}

const HarnessBenchmarkRecord* HarnessRegistry::lookup_harness_benchmark(const std::string& name) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = benchmarks_.find(name);
  return it == benchmarks_.end() ? nullptr : &it->second;
}

const ForgeMetadataRecord* HarnessRegistry::lookup_forge_metadata(const std::string& name) const {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = forge_metadata_.find(name);
  return it == forge_metadata_.end() ? nullptr : &it->second;
}

void HarnessRegistry::set_harness_status(const std::string& name, const std::string& status) {
  std::lock_guard<std::mutex> lk(mu_);
  auto it = harnesses_.find(name);
  if (it != harnesses_.end()) {
    it->second.status = status;
  }
}

std::vector<std::string> HarnessRegistry::list_harnesses() const {
  std::lock_guard<std::mutex> lk(mu_);
  std::vector<std::string> out;
  out.reserve(harnesses_.size());
  for (const auto& [n, _] : harnesses_) out.push_back(n);
  return out;
}

void HarnessRegistry::reset_for_tests() {
  std::lock_guard<std::mutex> lk(mu_);
  harnesses_.clear();
  handoffs_.clear();
  tool_registries_.clear();
  assertion_registries_.clear();
  benchmarks_.clear();
  forge_metadata_.clear();
}

// ─── compute_harness_hash ──────────────────────────────────────────────
//
// FR-H-5: pure function of (name + fields_json).  SHA-256 via OpenSSL EVP
// (same pattern as mcp_client.cpp:324).  Uses 0x1F (Unit Separator) as a
// delimiter between name and fields so that a harness named "A" with
// fields "B" doesn't collide with a harness named "AB" with empty fields.

std::string compute_harness_hash(const std::string& name,
                                 const std::string& fields_json) {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return {};
  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
  EVP_DigestUpdate(ctx, name.data(), name.size());
  const char sep = 0x1F;
  EVP_DigestUpdate(ctx, &sep, 1);
  EVP_DigestUpdate(ctx, fields_json.data(), fields_json.size());
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;
  EVP_DigestFinal_ex(ctx, hash, &hash_len);
  EVP_MD_CTX_free(ctx);

  std::ostringstream oss;
  for (unsigned int i = 0; i < hash_len; i++) {
    oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
  }
  return oss.str();
}

// ─── extract_json_string ───────────────────────────────────────────────
//
// Uses nlohmann::json.  Tolerant: on parse failure or missing key, returns "".

std::string extract_json_string(const std::string& fields_json,
                                const std::string& key) {
  if (fields_json.empty()) return {};
  try {
    auto j = nlohmann::json::parse(fields_json);
    auto it = j.find(key);
    if (it == j.end()) return {};
    if (it->is_string()) return it->get<std::string>();
    // Numeric / bool — stringify
    return it->dump();
  } catch (...) {
    return {};
  }
}

// ─── harness_runtime_env_map ───────────────────────────────────────────
//
// Lazy-init ULID-like run_id + ISO-8601 timestamp.  Phase 3+ extends this
// with NEAM_RUN_HARNESS_HASH / _SANDBOX_DIR / _HOST_DIR / _MODEL.

namespace {

std::string generate_run_id() {
  // ULID-lite: 10 chars of time + 16 chars of random.
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();

  std::mt19937_64 rng{static_cast<uint64_t>(ms)};
  std::uniform_int_distribution<uint64_t> dist;

  std::ostringstream oss;
  oss << std::hex << std::setw(10) << std::setfill('0') << ms
      << std::setw(16) << std::setfill('0') << dist(rng);
  return oss.str();
}

std::string iso8601_now_utc() {
  auto now = std::chrono::system_clock::now();
  auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
              now.time_since_epoch()).count() % 1000;
  std::ostringstream oss;
  oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
      << '.' << std::setw(3) << std::setfill('0') << ms << 'Z';
  return oss.str();
}

}  // anonymous namespace

const std::map<std::string, std::string>& harness_runtime_env_map() {
  static std::map<std::string, std::string> env = {
      {"NEAM_RUN_ID", generate_run_id()},
      {"NEAM_RUN_TIMESTAMP", iso8601_now_utc()},
  };
  return env;
}

}  // namespace neamc::vm::harness
