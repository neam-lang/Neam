// v1.6 NeamMesh — file-backed persistence for process instances (Phase E).
// Sibling of process_runtime.cpp; both share state through
// process_runtime_internal.hpp.  Atomic snapshot via tmp+rename.
//
// State file shape (JSON):
//   { instance_id, process, current_step, status, trace_path,
//     last_output, step_count }
//
// Path resolution:
//   1. NEAM_PROCESS_STATE_PATH (single file override)
//   2. runs/<NEAM_RUN_ID|epoch>/<process>.<inst[0:16]>.state.json (default)

#include "neamc/vm/process_runtime.hpp"
#include "neamc/vm/process_runtime_internal.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace neamc::vm::process {
namespace {

std::string getenv_or(const char* k, const std::string& def) {
  const char* v = std::getenv(k); return (v && *v) ? std::string(v) : def;
}

std::string default_state_path(const detail::ProcessInstance& s) {
  std::string run_id = getenv_or("NEAM_RUN_ID", std::to_string(std::time(nullptr)));
  std::filesystem::path dir = std::filesystem::path("runs") / run_id;
  std::error_code ec; std::filesystem::create_directories(dir, ec);
  return (dir / (s.process_name + "." + s.instance_id.substr(0,16) + ".state.json")).string();
}

}  // anonymous namespace

ProcessResult process_persist(const std::string& instance_id) {
  detail::ProcessInstance snap;
  {
    std::lock_guard<std::mutex> g(detail::mu());
    auto it = detail::instances().find(instance_id);
    if (it == detail::instances().end()) return {false, "", "PR-UNKNOWN"};
    snap = it->second;
  }
  nlohmann::json st = {
      {"instance_id", snap.instance_id}, {"process", snap.process_name},
      {"current_step", snap.current_step}, {"status", snap.status},
      {"trace_path", snap.trace_path}, {"last_output", snap.last_output},
      {"step_count", snap.step_count}};
  std::string path;
  const char* override_path = std::getenv("NEAM_PROCESS_STATE_PATH");
  if (override_path && *override_path) path = override_path;
  else path = default_state_path(snap);

  std::string tmp = path + ".tmp";
  { std::ofstream f(tmp, std::ios::binary);
    if (!f) return {false, "", "PR-IO"};
    f << st.dump(); }
  std::error_code ec; std::filesystem::rename(tmp, path, ec);
  if (ec) return {false, ec.message(), "PR-IO"};
  return {true, path, ""};
}

ProcessResult process_recover(const std::string& state_path) {
  std::ifstream f(state_path, std::ios::binary);
  if (!f) return {false, "", "PR-IO"};
  std::stringstream ss; ss << f.rdbuf();
  nlohmann::json st;
  try { st = nlohmann::json::parse(ss.str()); }
  catch (...) { return {false, "parse", "PR-IO"}; }

  detail::ProcessInstance inst;
  inst.instance_id = st.value("instance_id", std::string{});
  inst.process_name = st.value("process", std::string{});
  inst.current_step = st.value("current_step", std::string{});
  inst.status = st.value("status", std::string{"running"});
  inst.trace_path = st.value("trace_path", std::string{});
  inst.last_output = st.value("last_output", std::string{});
  inst.step_count = st.value("step_count", 0);
  if (inst.instance_id.empty()) return {false, "no instance_id", "PR-STATE"};

  std::lock_guard<std::mutex> g(detail::mu());
  detail::instances()[inst.instance_id] = inst;
  return {true, inst.instance_id, ""};
}

}  // namespace neamc::vm::process
