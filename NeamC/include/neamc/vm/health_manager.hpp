// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Health Check Manager (v0.6.0)
//

#pragma once

#include <chrono>
#include <string>

#include <nlohmann/json.hpp>

namespace neamc::vm
{

class StateBackend;
class LLMGateway;

class HealthManager
{
public:
  /// Liveness: process alive, no deadlocks
  nlohmann::json check_liveness() const;

  /// Readiness: state backend connected, LLM providers reachable
  nlohmann::json check_readiness() const;

  /// Startup: configuration loaded, agents registered
  nlohmann::json check_startup() const;

  /// Full status (for /health endpoint)
  nlohmann::json check_health() const;

  // Set dependencies (called during VM init)
  void set_state_backend(StateBackend* backend) { state_backend_ = backend; }
  void set_llm_gateway(LLMGateway* gateway) { llm_gateway_ = gateway; }
  void set_agents_registered(size_t count) { agents_registered_ = count; }
  void set_autonomous_leader(bool leader) { is_autonomous_leader_ = leader; }
  void set_startup_complete(bool complete) { startup_complete_ = complete; }
  void set_start_time(std::chrono::steady_clock::time_point t) { start_time_ = t; }

private:
  StateBackend* state_backend_{nullptr};
  LLMGateway* llm_gateway_{nullptr};
  size_t agents_registered_{0};
  bool is_autonomous_leader_{false};
  bool startup_complete_{false};
  std::chrono::steady_clock::time_point start_time_;
};

}  // namespace neamc::vm
