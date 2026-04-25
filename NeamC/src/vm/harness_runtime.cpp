// v1.4.5 Phase 3 full — harness lifecycle + sub-agent orchestration.
// See harness_runtime.hpp for surface-level contract.

#include "neamc/vm/harness_runtime.hpp"

#include "neamc/llm/provider.hpp"
#include "neamc/llm/provider_factory.hpp"
#include "neamc/vm/harness_types.hpp"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <iomanip>

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <regex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace neamc::vm::harness {

namespace {

struct RunContext
{
  std::string harness_name;
  std::string run_id;
  std::string trace_path;
  std::string provider;
  std::string model;
  std::string assertions_ref;  // "" if none
  std::string sub_agents_json; // raw object body
  bool        active{false};
};

std::mutex& registry_mu()
{
  static std::mutex m;
  return m;
}

std::unordered_map<std::string, RunContext>& run_registry()
{
  static std::unordered_map<std::string, RunContext> r;
  return r;
}

std::string getenv_or(const char* k, const std::string& def)
{
  const char* v = std::getenv(k);
  return (v && *v) ? std::string(v) : def;
}

bool env_is_truthy(const char* k)
{
  const char* v = std::getenv(k);
  if (!v || !*v) return false;
  const std::string s = v;
  return s == "1" || s == "true" || s == "TRUE" || s == "yes";
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

std::string derive_trace_path(const std::string& harness_name, const std::string& run_id)
{
  const char* override_path = std::getenv("NEAM_HARNESS_TRACE_PATH");
  if (override_path && *override_path) return override_path;
  std::filesystem::path dir = std::filesystem::path("runs") / run_id;
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  return (dir / (harness_name + ".trace.jsonl")).string();
}

void trace_append(const std::string& path, const nlohmann::json& event)
{
  if (path.empty()) return;
  std::ofstream f(path, std::ios::app | std::ios::binary);
  if (!f) return;
  nlohmann::json e = event;
  e["ts"] = iso8601_utc_now();
  f << e.dump() << "\n";
}

// Extract a key from fields_json.  Returns {} if absent or not parseable.
// Use ordered_json so sub_agents iterate in declaration order, not alphabetical.
nlohmann::ordered_json fields_parse(const std::string& fields_json)
{
  if (fields_json.empty()) return nlohmann::ordered_json::object();
  try
  {
    auto v = nlohmann::ordered_json::parse(fields_json);
    if (v.is_object()) return v;
    return nlohmann::ordered_json::object();
  }
  catch (...) { return nlohmann::ordered_json::object(); }
}

std::string json_string_or(const nlohmann::json& j, const std::string& key, const std::string& def)
{
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return def;
  return it->get<std::string>();
}

std::string json_string_or(const nlohmann::ordered_json& j, const std::string& key,
                           const std::string& def)
{
  auto it = j.find(key);
  if (it == j.end() || !it->is_string()) return def;
  return it->get<std::string>();
}

// Run every hard-severity regex assertion in `ar_name` against `content`.
// Returns empty string on success, or the first failing assertion name.
std::string evaluate_hard_regex_assertions(const std::string& ar_name,
                                           const std::string& content)
{
  if (ar_name.empty()) return {};
  const auto* meta = HarnessRegistry::instance().lookup_assertion_registry(ar_name);
  if (!meta) return {};
  nlohmann::json ar = fields_parse(meta->fields_json);
  if (!ar.is_object()) return {};
  for (auto it = ar.begin(); it != ar.end(); ++it)
  {
    if (!it->is_object()) continue;
    const auto kind = json_string_or(*it, "kind", "");
    const auto sev  = json_string_or(*it, "severity", "soft");
    if (kind != "regex" || sev != "hard") continue;
    const auto pattern = json_string_or(*it, "pattern", "");
    if (pattern.empty()) continue;
    try
    {
      std::regex re(pattern);
      if (std::regex_search(content, re)) return it.key();  // violation
    }
    catch (...) { /* malformed pattern: ignore */ }
  }
  return {};
}

// Echo-output used when NEAM_HARNESS_DRY_RUN is set.  Deterministic + cheap.
std::string dry_run_echo(const std::string& slot, const std::string& agent_name,
                         const std::string& goal, const std::string& prior)
{
  std::ostringstream os;
  os << "[dry-run slot=" << slot << " agent=" << agent_name << "] goal=" << goal;
  if (!prior.empty()) os << " prior=" << prior.substr(0, 64);
  return os.str();
}

// Single LLM call or dry-run echo.  Returns ("", error) on provider failure.
std::pair<std::string, std::string> call_llm_or_dry(
    const RunContext& ctx, const std::string& slot, const std::string& agent_name,
    const std::string& goal, const std::string& prior)
{
  if (env_is_truthy("NEAM_HARNESS_DRY_RUN"))
  {
    return {dry_run_echo(slot, agent_name, goal, prior), {}};
  }
  if (ctx.provider.empty() || ctx.model.empty())
    return {"", "provider/model missing on harness"};

  std::string api_key;
  if (ctx.provider == "openai")
  {
    const char* env = std::getenv("OPENAI_API_KEY");
    if (env) api_key = env;
  }
  else if (ctx.provider == "anthropic")
  {
    const char* env = std::getenv("ANTHROPIC_API_KEY");
    if (env) api_key = env;
  }

  ::neamc::llm::ProviderConfig cfg;
  cfg.model = ctx.model;
  cfg.api_key = api_key;
  cfg.temperature = 0.0;

  try
  {
    auto provider = ::neamc::llm::create_provider(ctx.provider, cfg);
    if (!provider) return {"", "provider factory returned null"};
    std::vector<::neamc::llm::Message> msgs;
    std::ostringstream prompt;
    prompt << "Sub-agent slot: " << slot << "\n";
    prompt << "Goal: " << goal << "\n";
    if (!prior.empty()) prompt << "Prior sub-agent output:\n" << prior << "\n";
    msgs.push_back({"user", prompt.str()});
    return {provider->chat(msgs), {}};
  }
  catch (const std::exception& e)
  {
    return {"", std::string("llm error: ") + e.what()};
  }
  catch (...)
  {
    return {"", "llm error: unknown"};
  }
}

}  // anonymous namespace

// ─── Public API ──────────────────────────────────────────────────────────

HarnessRunResult harness_start(const std::string& name)
{
  const auto* rec = HarnessRegistry::instance().lookup_harness(name);
  if (!rec) return {false, "", "HR-UNKNOWN"};

  std::lock_guard<std::mutex> g(registry_mu());
  auto& ctxs = run_registry();
  auto it = ctxs.find(name);
  if (it != ctxs.end() && it->second.active)
    return {false, "harness already running: " + name, "HR-STATE"};

  const auto fields = fields_parse(rec->fields_json);
  RunContext ctx;
  ctx.harness_name     = name;
  ctx.run_id           = getenv_or("NEAM_RUN_ID", std::to_string(std::time(nullptr)));
  ctx.trace_path       = derive_trace_path(name, ctx.run_id);
  // Prefer HarnessRecord fields, fall back to fields_json (generic parser path
  // stores everything in fields_json and leaves dedicated slots empty).
  ctx.provider         = !rec->provider.empty() ? rec->provider
                                                : json_string_or(fields, "provider", "");
  ctx.model            = !rec->model.empty() ? rec->model
                                             : json_string_or(fields, "model", "");
  ctx.assertions_ref   = json_string_or(fields, "assertions", "");
  auto sa_it = fields.find("sub_agents");
  if (sa_it != fields.end())
    ctx.sub_agents_json = sa_it->is_string() ? sa_it->get<std::string>() : sa_it->dump();
  ctx.active = true;
  ctxs[name] = ctx;

  HarnessRegistry::instance().set_harness_status(name, "running");
#if !defined(_WIN32)
  setenv("NEAM_RUN_HARNESS_HASH", rec->bytecode_hash.c_str(), 1);
  setenv("NEAM_RUN_MODEL", rec->model.c_str(), 1);
#endif

  trace_append(ctx.trace_path, {
    {"kind", "harness.start"},
    {"harness", name},
    {"hash", rec->bytecode_hash},
    {"run_id", ctx.run_id},
    {"provider", ctx.provider},
    {"model", ctx.model}
  });
  return {true, "ok", ""};
}

HarnessRunResult harness_run(const std::string& name, const std::string& goal)
{
  RunContext ctx;
  {
    std::lock_guard<std::mutex> g(registry_mu());
    auto& ctxs = run_registry();
    auto it = ctxs.find(name);
    if (it == ctxs.end() || !it->second.active)
      return {false, "harness not started: " + name, "HR-STATE"};
    ctx = it->second;
  }

  // Parse sub_agents body.  Use ordered_json to preserve declaration order.
  nlohmann::ordered_json sa;
  try
  {
    sa = nlohmann::ordered_json::parse(ctx.sub_agents_json.empty() ? "{}" : ctx.sub_agents_json);
  }
  catch (...) { sa = nlohmann::ordered_json::object(); }
  if (!sa.is_object() || sa.empty())
  {
    // Fall back: try to parse fields_json again and extract sub_agents as a raw nested object.
    const auto* rec = HarnessRegistry::instance().lookup_harness(name);
    if (rec)
    {
      auto full = fields_parse(rec->fields_json);
      if (auto it = full.find("sub_agents"); it != full.end() && it->is_object()) sa = *it;
    }
  }
  if (!sa.is_object() || sa.empty())
    return {false, "harness has no sub_agents", "HR-STATE"};

  std::string prior;
  std::string final_output;
  for (auto it = sa.begin(); it != sa.end(); ++it)
  {
    const std::string slot = it.key();
    const std::string agent_name = it->is_string() ? it->get<std::string>() : it->dump();

    trace_append(ctx.trace_path, {
      {"kind", "sub_agent.begin"},
      {"slot", slot}, {"agent", agent_name}
    });

    // Call LLM (or dry-run), with a single retry on hard-assertion violation.
    std::string output;
    std::string violated;
    for (int attempt = 1; attempt <= 2; ++attempt)
    {
      auto [text, err] = call_llm_or_dry(ctx, slot, agent_name, goal, prior);
      if (!err.empty())
      {
        trace_append(ctx.trace_path, {
          {"kind", "sub_agent.llm_error"},
          {"slot", slot}, {"attempt", attempt}, {"error", err}
        });
        return {false, err, "HR-LLM"};
      }
      output = text;
      violated = evaluate_hard_regex_assertions(ctx.assertions_ref, output);
      if (violated.empty()) break;  // passed
      trace_append(ctx.trace_path, {
        {"kind", "assertion.violation"},
        {"slot", slot}, {"attempt", attempt}, {"assertion", violated}
      });
      if (attempt == 2)
      {
        // Second attempt also violated — abort cleanly.
        return {false, "hard assertion violated: " + violated, "HR-AR"};
      }
      // On first violation, augment the prompt with a corrective hint.
      prior = prior + "\n[violated_assertion=" + violated + "; please fix]";
    }

    trace_append(ctx.trace_path, {
      {"kind", "sub_agent.complete"},
      {"slot", slot}, {"agent", agent_name},
      {"output_chars", static_cast<int>(output.size())}
    });

    prior = output;
    final_output = output;
  }

  return {true, final_output, ""};
}

HarnessRunResult harness_complete(const std::string& name)
{
  std::string trace_path;
  {
    std::lock_guard<std::mutex> g(registry_mu());
    auto& ctxs = run_registry();
    auto it = ctxs.find(name);
    if (it == ctxs.end() || !it->second.active)
      return {false, "harness not running: " + name, "HR-STATE"};
    trace_path = it->second.trace_path;
    it->second.active = false;
  }
  HarnessRegistry::instance().set_harness_status(name, "complete");
  trace_append(trace_path, {{"kind", "harness.complete"}, {"harness", name}});
  return {true, "ok", ""};
}

HarnessRunResult harness_abort(const std::string& name, const std::string& reason)
{
  std::string trace_path;
  {
    std::lock_guard<std::mutex> g(registry_mu());
    auto& ctxs = run_registry();
    auto it = ctxs.find(name);
    if (it == ctxs.end() || !it->second.active)
      return {false, "harness not running: " + name, "HR-STATE"};
    trace_path = it->second.trace_path;
    it->second.active = false;
  }
  HarnessRegistry::instance().set_harness_status(name, "aborted");
  trace_append(trace_path, {{"kind", "harness.abort"}, {"harness", name}, {"reason", reason}});
  return {true, "ok", ""};
}

std::string harness_trace_path(const std::string& name)
{
  std::lock_guard<std::mutex> g(registry_mu());
  auto it = run_registry().find(name);
  if (it == run_registry().end()) return {};
  return it->second.trace_path;
}

// ─── v1.5 NeamEvolve — additive accessors ───────────────────────────

bool is_run_active(const std::string& name)
{
  std::lock_guard<std::mutex> g(registry_mu());
  auto it = run_registry().find(name);
  return it != run_registry().end() && it->second.active;
}

std::string evaluate_hard_regex_assertions_pub(const std::string& ar_name,
                                               const std::string& content)
{
  // Delegate to the anonymous-namespace implementation above.
  return evaluate_hard_regex_assertions(ar_name, content);
}

void trace_emit_for_run(const std::string& name,
                        const std::string& kind_payload_json)
{
  std::string trace_path;
  {
    std::lock_guard<std::mutex> g(registry_mu());
    auto it = run_registry().find(name);
    if (it == run_registry().end()) return;
    trace_path = it->second.trace_path;
  }
  if (trace_path.empty()) return;
  try {
    auto event = nlohmann::json::parse(kind_payload_json);
    trace_append(trace_path, event);
  } catch (...) {
    // Tolerated — caller produced malformed JSON; don't crash.
  }
}

// ─── v1.5 NFR-SEC-6 hash-chained trace emission ─────────────────────
namespace {
std::mutex& chain_mu() { static std::mutex m; return m; }
std::unordered_map<std::string, std::string>& last_hash_per_path()
{
  static std::unordered_map<std::string, std::string> m;
  return m;
}

std::string sha256_hex_of(const std::string& s)
{
  // Inline SHA-256 via OpenSSL EVP — same pattern as harness_types::compute_harness_hash.
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return std::string(64, '0');
  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
  EVP_DigestUpdate(ctx, s.data(), s.size());
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
}  // anon

void trace_emit_for_run_with_chain(const std::string& name,
                                   const std::string& kind_payload_json)
{
  std::string trace_path;
  {
    std::lock_guard<std::mutex> g(registry_mu());
    auto it = run_registry().find(name);
    if (it == run_registry().end()) return;
    trace_path = it->second.trace_path;
  }
  if (trace_path.empty()) return;

  nlohmann::json event;
  try {
    event = nlohmann::json::parse(kind_payload_json);
  } catch (...) { return; }

  std::lock_guard<std::mutex> g(chain_mu());
  auto& cache = last_hash_per_path();
  auto it = cache.find(trace_path);
  std::string prev = (it == cache.end()) ? std::string(64, '0') : it->second;
  event["prev_hash"] = prev;
  event["ts"]        = iso8601_utc_now();
  // this_hash is computed over the canonical-JSON (sorted, no this_hash field
  // included) so verifiers can reconstruct it deterministically.
  std::string canonical = event.dump();
  std::string this_hash = sha256_hex_of(canonical);
  event["this_hash"] = this_hash;
  std::ofstream f(trace_path, std::ios::app | std::ios::binary);
  if (f) f << event.dump() << "\n";
  cache[trace_path] = this_hash;
}

}  // namespace neamc::vm::harness
