// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Trace logger
//

#include "neamc/vm/trace.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <random>
#include <sstream>

namespace neamc::vm
{
namespace
{
std::string iso_timestamp()
{
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const auto time = clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}
}  // namespace

TraceLogger::TraceLogger()
{
  step_trace_ = std::getenv("NEAM_TRACE") != nullptr;
}

bool TraceLogger::ensure_directories() const
{
  const char* home = std::getenv("HOME");
  if (!home)
  {
    return false;
  }
  std::filesystem::path base = std::filesystem::path(home) / ".neam" / "traces";
  std::error_code ec;
  std::filesystem::create_directories(base, ec);
  return !ec;
}

std::string TraceLogger::generate_run_id() const
{
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int> dist(0, 255);
  std::ostringstream out;
  for (int i = 0; i < 16; ++i)
  {
    const int value = dist(gen);
    out << std::hex << std::setw(2) << std::setfill('0') << value;
  }
  return out.str();
}

std::string TraceLogger::timestamp() const
{
  return iso_timestamp();
}

void TraceLogger::start_run()
{
  if (out_.is_open())
  {
    out_.close();
  }
  if (!ensure_directories())
  {
    return;
  }
  const char* home = std::getenv("HOME");
  if (!home)
  {
    return;
  }
  run_id_ = generate_run_id();
  std::filesystem::path file =
      std::filesystem::path(home) / ".neam" / "traces" / (run_id_ + ".jsonl");
  out_.open(file, std::ios::out | std::ios::trunc);
}

void TraceLogger::log_event(const std::string& type, const nlohmann::json& payload)
{
  if (!out_.is_open())
  {
    return;
  }
  nlohmann::json event;
  event["timestamp"] = timestamp();
  event["run_id"] = run_id_;
  event["type"] = type;
  event["payload"] = payload;
  out_ << event.dump() << "\n";
  out_.flush();
}

void TraceLogger::log_start()
{
  log_event("START", nlohmann::json::object());
}

void TraceLogger::log_end()
{
  log_event("END", nlohmann::json::object());
}

void TraceLogger::log_step(const std::string& op_name, std::size_t ip, std::size_t stack_size)
{
  if (!step_trace_)
  {
    return;
  }
  nlohmann::json payload;
  payload["op"] = op_name;
  payload["ip"] = ip;
  payload["stack_size"] = stack_size;
  log_event("STEP", payload);
}

void TraceLogger::log_tool_call(const std::string& agent, const std::string& tool,
                                const nlohmann::json& args)
{
  nlohmann::json payload;
  payload["agent"] = agent;
  payload["tool"] = tool;
  payload["args"] = args;
  log_event("TOOL_CALL", payload);
}

void TraceLogger::log_tool_result(const std::string& tool, const nlohmann::json& result)
{
  nlohmann::json payload;
  payload["tool"] = tool;
  payload["result"] = result;
  log_event("TOOL_RESULT", payload);
}

void TraceLogger::log_llm_output(const std::string& agent, const std::string& content)
{
  nlohmann::json payload;
  payload["agent"] = agent;
  payload["content"] = content;
  log_event("LLM_OUTPUT", payload);
}

void TraceLogger::log_custom_event(const std::string& type, const nlohmann::json& payload)
{
  log_event(type, payload);
}

void TraceLogger::log_llm_call(const std::string& agent, const std::string& model,
                                const std::string& provider, std::size_t prompt_tokens,
                                std::size_t completion_tokens, std::size_t total_tokens,
                                double cost_usd, int64_t latency_ms)
{
  // Update accumulators
  total_prompt_tokens_ += prompt_tokens;
  total_completion_tokens_ += completion_tokens;
  total_cost_usd_ += cost_usd;

  nlohmann::json payload;
  payload["agent"] = agent;
  payload["model"] = model;
  payload["provider"] = provider;
  payload["prompt_tokens"] = prompt_tokens;
  payload["completion_tokens"] = completion_tokens;
  payload["total_tokens"] = total_tokens;
  payload["cost_usd"] = cost_usd;
  payload["latency_ms"] = latency_ms;
  log_event("LLM_CALL", payload);
}

void TraceLogger::reset_accumulators()
{
  total_prompt_tokens_ = 0;
  total_completion_tokens_ = 0;
  total_cost_usd_ = 0.0;
}
}  // namespace neamc::vm
