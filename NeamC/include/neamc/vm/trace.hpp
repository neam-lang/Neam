//
// Neam Virtual Machine - Trace logger
//

#pragma once

#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

namespace neamc::vm
{
class TraceLogger
{
public:
  TraceLogger();

  void start_run();
  void log_start();
  void log_end();
  void log_step(const std::string& op_name, std::size_t ip, std::size_t stack_size);
  void log_tool_call(const std::string& agent, const std::string& tool,
                     const nlohmann::json& args);
  void log_tool_result(const std::string& tool, const nlohmann::json& result);
  void log_llm_output(const std::string& agent, const std::string& content);
  void log_custom_event(const std::string& type, const nlohmann::json& payload);

  const std::string& run_id() const { return run_id_; }
  bool step_trace_enabled() const { return step_trace_; }

private:
  void log_event(const std::string& type, const nlohmann::json& payload);
  std::string timestamp() const;
  std::string generate_run_id() const;
  bool ensure_directories() const;

  std::string run_id_{};
  std::ofstream out_{};
  bool step_trace_{false};
};
}  // namespace neamc::vm
