// SPDX-License-Identifier: Apache-2.0
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
  void log_llm_call(const std::string& agent, const std::string& model,
                    const std::string& provider, std::size_t prompt_tokens,
                    std::size_t completion_tokens, std::size_t total_tokens,
                    double cost_usd, int64_t latency_ms);

  const std::string& run_id() const { return run_id_; }
  bool step_trace_enabled() const { return step_trace_; }

  // Accumulator accessors for evaluation framework
  std::size_t total_prompt_tokens() const { return total_prompt_tokens_; }
  std::size_t total_completion_tokens() const { return total_completion_tokens_; }
  double total_cost_usd() const { return total_cost_usd_; }
  void reset_accumulators();

private:
  void log_event(const std::string& type, const nlohmann::json& payload);
  std::string timestamp() const;
  std::string generate_run_id() const;
  bool ensure_directories() const;

  std::string run_id_{};
  std::ofstream out_{};
  bool step_trace_{false};

  // Accumulators for token/cost tracking
  std::size_t total_prompt_tokens_{0};
  std::size_t total_completion_tokens_{0};
  double total_cost_usd_{0.0};
};
}  // namespace neamc::vm
