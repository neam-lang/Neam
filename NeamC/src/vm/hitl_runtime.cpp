// v1.6 NeamMesh — Human-In-The-Loop runtime (Phase F).
// See hitl_runtime.hpp for surface contract.  Operates on the same
// process instance map as process_runtime (via process_runtime_internal.hpp).

#include "neamc/vm/hitl_runtime.hpp"
#include "neamc/vm/process_runtime_internal.hpp"

namespace neamc::vm::hitl {

namespace {
constexpr const char* kPendingPrefix = "[hitl_pending";
}  // anon

bool hitl_is_pending(const std::string& instance_id) {
  std::lock_guard<std::mutex> g(::neamc::vm::process::detail::mu());
  auto it = ::neamc::vm::process::detail::instances().find(instance_id);
  if (it == ::neamc::vm::process::detail::instances().end()) return false;
  return it->second.last_output.rfind(kPendingPrefix, 0) == 0;
}

HitlResult hitl_resume(const std::string& instance_id,
                       const std::string& payload) {
  std::lock_guard<std::mutex> g(::neamc::vm::process::detail::mu());
  auto it = ::neamc::vm::process::detail::instances().find(instance_id);
  if (it == ::neamc::vm::process::detail::instances().end())
    return {false, "unknown instance", "HL-UNKNOWN"};
  if (it->second.last_output.rfind(kPendingPrefix, 0) != 0)
    return {false, "instance not pending human input", "HL-NOTPENDING"};
  it->second.last_output = payload;
  return {true, "ok", ""};
}

}  // namespace neamc::vm::hitl
