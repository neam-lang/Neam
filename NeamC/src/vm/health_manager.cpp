// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Health Check Manager (v0.6.0)
//

#include "neamc/vm/health_manager.hpp"

#include "neamc/vm/llm_gateway.hpp"
#include "neamc/vm/state_backend.hpp"

#include <chrono>

namespace neamc::vm
{
nlohmann::json HealthManager::check_liveness() const
{
  nlohmann::json result;
  result["status"] = "alive";
  result["version"] = "0.6.0";

  auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time_)
                    .count();
  result["uptime_seconds"] = uptime;

  return result;
}

nlohmann::json HealthManager::check_readiness() const
{
  nlohmann::json result;
  nlohmann::json checks;
  bool all_ready = true;

  // State backend check
  if (state_backend_)
  {
    auto start = std::chrono::steady_clock::now();
    try
    {
      state_backend_->load_checkpoint("__health__", "__ping__");
      auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count();
      checks["state_backend"] = {{"status", "up"}, {"latency_ms", latency}};
    }
    catch (...)
    {
      checks["state_backend"] = {{"status", "down"}};
      all_ready = false;
    }
  }

  // LLM provider health
  if (llm_gateway_)
  {
    auto provider_health = llm_gateway_->get_provider_health();
    nlohmann::json providers;
    for (const auto& [name, health] : provider_health)
    {
      std::string circuit;
      switch (health.circuit)
      {
        case ProviderHealth::CircuitState::kClosed:
          circuit = "closed";
          break;
        case ProviderHealth::CircuitState::kOpen:
          circuit = "open";
          break;
        case ProviderHealth::CircuitState::kHalfOpen:
          circuit = "half-open";
          break;
      }
      std::string status =
          (health.circuit == ProviderHealth::CircuitState::kClosed)    ? "up"
          : (health.circuit == ProviderHealth::CircuitState::kHalfOpen) ? "degraded"
                                                                        : "down";
      providers[name] = {{"status", status}, {"circuit", circuit}};
      if (health.circuit == ProviderHealth::CircuitState::kOpen)
      {
        all_ready = false;
      }
    }
    checks["llm_providers"] = providers;
  }

  result["status"] = all_ready ? "ready" : "not_ready";
  result["checks"] = checks;
  return result;
}

nlohmann::json HealthManager::check_startup() const
{
  nlohmann::json result;
  result["status"] = startup_complete_ ? "started" : "starting";
  result["agents_registered"] = agents_registered_;
  result["startup_complete"] = startup_complete_;
  return result;
}

nlohmann::json HealthManager::check_health() const
{
  auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now() - start_time_)
                    .count();

  nlohmann::json result;
  result["version"] = "0.6.0";
  result["uptime_seconds"] = uptime;

  nlohmann::json checks;

  // State backend check
  if (state_backend_)
  {
    auto start = std::chrono::steady_clock::now();
    try
    {
      state_backend_->load_checkpoint("__health__", "__ping__");
      auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count();
      checks["state_backend"] = {{"status", "up"}, {"latency_ms", latency}};
    }
    catch (...)
    {
      checks["state_backend"] = {{"status", "down"}};
    }
  }

  // LLM provider health
  if (llm_gateway_)
  {
    auto provider_health = llm_gateway_->get_provider_health();
    nlohmann::json providers;
    for (const auto& [name, health] : provider_health)
    {
      std::string circuit;
      switch (health.circuit)
      {
        case ProviderHealth::CircuitState::kClosed:
          circuit = "closed";
          break;
        case ProviderHealth::CircuitState::kOpen:
          circuit = "open";
          break;
        case ProviderHealth::CircuitState::kHalfOpen:
          circuit = "half-open";
          break;
      }
      std::string status =
          (health.circuit == ProviderHealth::CircuitState::kClosed)    ? "up"
          : (health.circuit == ProviderHealth::CircuitState::kHalfOpen) ? "degraded"
                                                                        : "down";
      providers[name] = {{"status", status},
                         {"circuit", circuit},
                         {"total_requests", health.total_requests},
                         {"total_failures", health.total_failures}};
    }
    checks["llm_providers"] = providers;
  }

  checks["agents_registered"] = agents_registered_;
  checks["autonomous_leader"] = is_autonomous_leader_;
  result["checks"] = checks;

  // Overall status
  bool all_up = true;
  if (checks.contains("state_backend") && checks["state_backend"]["status"] != "up")
  {
    all_up = false;
  }
  result["status"] = all_up ? "healthy" : "degraded";

  // Cost report if gateway available
  if (llm_gateway_)
  {
    auto cost = llm_gateway_->get_cost_report();
    result["cost"] = {{"daily_total", cost.daily_total}, {"monthly_total", cost.monthly_total}};
  }

  return result;
}

}  // namespace neamc::vm
