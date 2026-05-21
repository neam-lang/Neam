// v1.6 NeamMesh — Agentic Process Automation runtime (≤350 LOC by ADR-003).
// Bridges to harness::execute_one_step; reuses HarnessRegistry side tables.
// MUST NOT contain LLM/HTTP/provider code.

#include "neamc/vm/process_runtime.hpp"
#include "neamc/vm/process_runtime_internal.hpp"
#include "neamc/vm/harness_runtime.hpp"
#include "neamc/vm/harness_types.hpp"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <unordered_map>

namespace neamc::vm::process {

// Internal state map — defined here, declared in process_runtime_internal.hpp
// so process_persistence.cpp can rehydrate / snapshot.
namespace detail {
std::mutex& mu() { static std::mutex m; return m; }
std::unordered_map<std::string, ProcessInstance>& instances() {
  static std::unordered_map<std::string, ProcessInstance> m; return m;
}
}  // namespace detail

namespace {
// Bring detail names into anon namespace for terseness.
using detail::ProcessInstance;
using detail::mu;
using detail::instances;

std::string iso8601_utc_now() {
  std::time_t t = std::time(nullptr); std::tm g{};
#if defined(_WIN32)
  gmtime_s(&g, &t);
#else
  gmtime_r(&t, &g);
#endif
  char buf[32]; std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &g);
  return std::string(buf);
}

std::string generate_instance_id() {
  auto now = std::chrono::system_clock::now().time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  std::mt19937_64 rng{static_cast<uint64_t>(ms) ^ static_cast<uint64_t>(rand())};
  std::uniform_int_distribution<uint64_t> dist;
  std::ostringstream oss;
  oss << std::hex << std::setw(10) << std::setfill('0') << ms
      << std::setw(16) << std::setfill('0') << dist(rng);
  return oss.str();
}

std::string getenv_or(const char* k, const std::string& def) {
  const char* v = std::getenv(k); return (v && *v) ? std::string(v) : def;
}

std::string derive_trace_path(const std::string& process_name,
                              const std::string& instance_id) {
  const char* override_path = std::getenv("NEAM_PROCESS_TRACE_PATH");
  if (override_path && *override_path) return override_path;
  std::string run_id = getenv_or("NEAM_RUN_ID", std::to_string(std::time(nullptr)));
  std::filesystem::path dir = std::filesystem::path("runs") / run_id;
  std::error_code ec; std::filesystem::create_directories(dir, ec);
  return (dir / (process_name + "." + instance_id.substr(0, 16) + ".trace.jsonl")).string();
}

void trace_emit(const std::string& trace_path, const nlohmann::json& event) {
  if (trace_path.empty()) return;
  std::ofstream f(trace_path, std::ios::app | std::ios::binary);
  if (!f) return;
  nlohmann::json e = event; e["ts"] = iso8601_utc_now();
  f << e.dump() << "\n";
}

// Bridge to harness::execute_one_step — resolves provider/model from the
// referenced HarnessRecord, then delegates to the v1.4.5 LLM/dry-run substrate.
std::pair<std::string, std::string> execute_task(
    const ::neamc::vm::harness::TaskRecord& task,
    const std::string& prior, ProcessInstance& inst) {
  ::neamc::vm::harness::ExecuteStepArgs args;
  args.slot = task.name; args.agent_name = task.agent_ref; args.prior = prior;
  std::string goal = task.name;
  try {
    auto j = nlohmann::json::parse(task.fields_json);
    if (j.contains("goal") && j["goal"].is_string()) goal = j.value("goal", goal);
    else if (j.contains("input") && j["input"].is_string()) goal = j.value("input", goal);
  } catch (...) {}
  args.goal = goal;
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  if (!task.agent_ref.empty()) {
    if (const auto* h = reg.lookup_harness(task.agent_ref)) {
      args.provider = h->provider; args.model = h->model;
    }
    // P-FR-AAN-3: HITL human-role tasks short-circuit (Phase F integration).
    if (const auto* meta = reg.lookup_forge_metadata(task.agent_ref)) {
      if (meta->role == "human") {
        trace_emit(inst.trace_path, {{"kind", "task.human_pending"},
            {"task", task.name}, {"agent", task.agent_ref}, {"goal", goal}});
        return {std::string("[hitl_pending agent=") + task.agent_ref + "]", {}};
      }
    }
  }
  auto r = ::neamc::vm::harness::execute_one_step(args);
  return {r.output, r.error};
}

// Decision body: { expr, branches: { yes:<step>, no:<step>, ... } }
// v1 heuristic: last_output truthiness on yes/true/approved/ok keywords.
std::string evaluate_decision(const ::neamc::vm::harness::DecisionRecord& d,
                              const std::string& last_output) {
  try {
    auto j = nlohmann::json::parse(d.fields_json);
    if (!j.contains("branches") || !j["branches"].is_object()) return {};
    bool truthy = !last_output.empty() &&
                  (last_output.find("yes") != std::string::npos ||
                   last_output.find("true") != std::string::npos ||
                   last_output.find("approved") != std::string::npos ||
                   last_output.find("ok") != std::string::npos);
    auto branches = j["branches"];
    if (truthy && branches.contains("yes") && branches["yes"].is_string())
      return branches.value("yes", std::string{});
    if (!truthy && branches.contains("no") && branches["no"].is_string())
      return branches.value("no", std::string{});
    for (auto it = branches.begin(); it != branches.end(); ++it)
      if (it->is_string()) return it->get<std::string>();
  } catch (...) {}
  return {};
}

// Resolve next step from (current step, last_output).  Tasks: fields.next or
// fields.on_success.  Decisions: branch.  Events of type "end": terminate.
// Fallback: process.transitions[cur_step].
std::string find_next_step(const std::string& process_name,
                           const std::string& cur_step,
                           const std::string& last_output) {
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  if (const auto* t = reg.lookup_task(cur_step)) {
    try {
      auto j = nlohmann::json::parse(t->fields_json);
      if (j.contains("next") && j["next"].is_string()) return j.value("next", std::string{});
      if (j.contains("on_success") && j["on_success"].is_string()) return j.value("on_success", std::string{});
    } catch (...) {}
  }
  if (const auto* d = reg.lookup_decision(cur_step)) return evaluate_decision(*d, last_output);
  if (const auto* e = reg.lookup_event(cur_step)) {
    if (e->event_type == "end") return {};
    try {
      auto j = nlohmann::json::parse(e->fields_json);
      if (j.contains("next") && j["next"].is_string()) return j.value("next", std::string{});
    } catch (...) {}
  }
  if (const auto* p = reg.lookup_process(process_name)) {
    try {
      auto j = nlohmann::json::parse(p->fields_json);
      if (j.contains("transitions") && j["transitions"].is_object()) {
        auto t = j["transitions"];
        if (t.contains(cur_step) && t[cur_step].is_string())
          return t.value(cur_step, std::string{});
      }
    } catch (...) {}
  }
  return {};
}

}  // anonymous namespace

// ─── Public API ─────────────────────────────────────────────────────

ProcessResult process_start(const std::string& process_name) {
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  const auto* p = reg.lookup_process(process_name);
  if (!p) return {false, "", "PR-UNKNOWN"};
  std::string start_ref;
  try {
    auto j = nlohmann::json::parse(p->fields_json);
    start_ref = j.value("start", std::string{});
  } catch (...) {}
  if (start_ref.empty()) return {false, "process has no start step", "PR-STATE"};

  ProcessInstance inst;
  inst.instance_id  = generate_instance_id();
  inst.process_name = process_name;
  inst.current_step = start_ref;
  inst.status       = "running";
  inst.trace_path   = derive_trace_path(process_name, inst.instance_id);

  trace_emit(inst.trace_path, {
      {"kind", "process.start"}, {"process", process_name},
      {"instance_id", inst.instance_id}, {"start", start_ref}});

  std::lock_guard<std::mutex> g(mu());
  instances()[inst.instance_id] = inst;
  return {true, inst.instance_id, ""};
}

ProcessResult process_advance(const std::string& instance_id) {
  ProcessInstance inst;
  {
    std::lock_guard<std::mutex> g(mu());
    auto it = instances().find(instance_id);
    if (it == instances().end()) return {false, "", "PR-UNKNOWN"};
    if (it->second.status != "running")
      return {false, "instance not running: " + it->second.status, "PR-STATE"};
    inst = it->second;
  }
  auto& reg = ::neamc::vm::harness::HarnessRegistry::instance();
  // Bounded loop guard (P-NFR-PERF-1: process advance must be O(steps)).
  const int MAX_STEPS = 1000;
  while (!inst.current_step.empty() && inst.step_count < MAX_STEPS) {
    inst.step_count++;
    // 1. Task?
    if (const auto* t = reg.lookup_task(inst.current_step)) {
      trace_emit(inst.trace_path, {
          {"kind", "task.begin"}, {"task", t->name}, {"agent", t->agent_ref},
          {"step", inst.step_count}});
      auto [out, err] = execute_task(*t, inst.last_output, inst);
      if (!err.empty()) {
        inst.status = "aborted";
        trace_emit(inst.trace_path, {
            {"kind", "task.error"}, {"task", t->name}, {"error", err}});
        std::lock_guard<std::mutex> g(mu());
        instances()[instance_id] = inst;
        return {false, err, "PR-TASK"};
      }
      inst.last_output = out;
      trace_emit(inst.trace_path, {
          {"kind", "task.complete"}, {"task", t->name},
          {"output_chars", static_cast<int>(out.size())}});
    }
    // 2. Decision?
    else if (const auto* d = reg.lookup_decision(inst.current_step)) {
      std::string branch = evaluate_decision(*d, inst.last_output);
      trace_emit(inst.trace_path, {
          {"kind", "decision"}, {"decision", d->name}, {"branch", branch}});
      inst.current_step = branch;
      continue;  // skip find_next_step; the branch IS the next step
    }
    // 3. Event?
    else if (const auto* e = reg.lookup_event(inst.current_step)) {
      trace_emit(inst.trace_path, {
          {"kind", "event"}, {"event", e->name}, {"type", e->event_type}});
      if (e->event_type == "end") {
        inst.status = "complete";
        inst.current_step = "";
        break;
      }
    }
    else {
      // Unknown step — abort cleanly.
      inst.status = "aborted";
      trace_emit(inst.trace_path, {
          {"kind", "step.unknown"}, {"step", inst.current_step}});
      std::lock_guard<std::mutex> g(mu());
      instances()[instance_id] = inst;
      return {false, "unknown step: " + inst.current_step, "PR-STEP"};
    }
    // Resolve next step.
    inst.current_step = find_next_step(inst.process_name, inst.current_step,
                                       inst.last_output);
  }
  if (inst.step_count >= MAX_STEPS) {
    inst.status = "aborted";
    trace_emit(inst.trace_path, {{"kind", "process.step_limit"}});
    std::lock_guard<std::mutex> g(mu());
    instances()[instance_id] = inst;
    return {false, "step limit exceeded", "PR-LIMIT"};
  }
  if (inst.current_step.empty() && inst.status == "running") {
    inst.status = "complete";
  }
  trace_emit(inst.trace_path, {
      {"kind", "process.complete"},
      {"instance_id", inst.instance_id}, {"steps", inst.step_count},
      {"status", inst.status}});
  std::lock_guard<std::mutex> g(mu());
  instances()[instance_id] = inst;
  return {true, inst.last_output, ""};
}

std::string process_status(const std::string& instance_id) {
  std::lock_guard<std::mutex> g(mu());
  auto it = instances().find(instance_id);
  if (it == instances().end()) return "unknown";
  return it->second.status;
}

ProcessResult process_abort(const std::string& instance_id,
                            const std::string& reason) {
  std::lock_guard<std::mutex> g(mu());
  auto it = instances().find(instance_id);
  if (it == instances().end()) return {false, "", "PR-UNKNOWN"};
  if (it->second.status != "running")
    return {false, "not running", "PR-STATE"};
  it->second.status = "aborted";
  trace_emit(it->second.trace_path, {
      {"kind", "process.abort"}, {"reason", reason}});
  return {true, "ok", ""};
}

std::string process_trace_path(const std::string& instance_id) {
  std::lock_guard<std::mutex> g(mu());
  auto it = instances().find(instance_id);
  return it == instances().end() ? std::string{} : it->second.trace_path;
}

std::vector<std::string> list_instances() {
  std::lock_guard<std::mutex> g(mu());
  std::vector<std::string> out;
  out.reserve(instances().size());
  for (const auto& [k, _] : instances()) out.push_back(k);
  return out;
}

void reset_for_tests() {
  std::lock_guard<std::mutex> g(mu());
  instances().clear();
}

}  // namespace neamc::vm::process
