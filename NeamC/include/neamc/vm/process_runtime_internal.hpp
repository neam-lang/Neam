// v1.6 NeamMesh — internal-only header shared between process_runtime.cpp
// and process_persistence.cpp.  NOT for general consumption.

#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

namespace neamc::vm::process::detail {

struct ProcessInstance {
  std::string instance_id;
  std::string process_name;
  std::string current_step;
  std::string status;        // "running"|"complete"|"aborted"
  std::string trace_path;
  std::string last_output;
  int         step_count{0};
};

std::mutex& mu();
std::unordered_map<std::string, ProcessInstance>& instances();

}  // namespace neamc::vm::process::detail
