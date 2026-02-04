// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - LLM Gateway (v0.6.0)
//

#pragma once

#include "neamc/llm/provider.hpp"

#include <chrono>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace neamc::vm
{

struct RateLimitConfig
{
  size_t requests_per_minute{0};
  size_t tokens_per_minute{0};
};

struct CircuitBreakerConfig
{
  size_t failure_threshold{5};
  std::chrono::seconds recovery_timeout{30};
};

struct CacheConfig
{
  bool enabled{false};
  std::chrono::seconds ttl{3600};
  size_t max_entries{10000};
};

struct CostConfig
{
  double alert_threshold_daily{0.0};
  double alert_threshold_monthly{0.0};
};

struct LLMGatewayConfig
{
  std::unordered_map<std::string, RateLimitConfig> rate_limits;
  std::vector<std::string> fallback_chain;
  CircuitBreakerConfig circuit_breaker;
  CacheConfig cache;
  CostConfig cost;
};

struct ProviderHealth
{
  enum class CircuitState
  {
    kClosed,
    kOpen,
    kHalfOpen
  };
  CircuitState circuit{CircuitState::kClosed};
  size_t consecutive_failures{0};
  std::chrono::steady_clock::time_point last_failure;
  size_t total_requests{0};
  size_t total_failures{0};
};

struct CostReport
{
  double daily_total{0.0};
  double monthly_total{0.0};
  std::unordered_map<std::string, double> by_provider;
  std::unordered_map<std::string, double> by_model;
};

class LLMGateway
{
public:
  explicit LLMGateway(const LLMGatewayConfig& config);
  ~LLMGateway();

  LLMGateway(const LLMGateway&) = delete;
  LLMGateway& operator=(const LLMGateway&) = delete;

  /// Register a provider adapter
  void register_provider(const std::string& name, std::shared_ptr<llm::LLMProvider> provider);

  /// Send a chat request through the gateway
  llm::ChatResult send(const std::string& provider_name,
                       const std::vector<llm::Message>& messages,
                       const std::vector<llm::ToolDefinition>& tools = {},
                       const std::string& response_format = "");

  /// Streaming variant
  std::string send_stream(const std::string& provider_name,
                          const std::vector<llm::Message>& messages,
                          const llm::LLMProvider::TokenCallback& callback);

  /// Cost management
  CostReport get_cost_report() const;
  void record_usage(const std::string& provider, const std::string& model,
                    const llm::UsageInfo& usage);

  /// Health
  std::unordered_map<std::string, ProviderHealth> get_provider_health() const;

private:
  struct TokenBucket
  {
    size_t max_tokens;
    double tokens;
    std::chrono::steady_clock::time_point last_refill;
    double refill_rate;
  };

  struct CacheEntry
  {
    std::string response_json;
    std::chrono::steady_clock::time_point expires;
  };

  bool try_consume_rate(const std::string& provider);
  bool is_circuit_open(const std::string& provider) const;
  void record_success(const std::string& provider);
  void record_failure(const std::string& provider);
  std::string compute_cache_key(const std::string& provider,
                                const std::vector<llm::Message>& messages) const;
  llm::ChatResult try_send(const std::string& provider_name,
                           const std::vector<llm::Message>& messages,
                           const std::vector<llm::ToolDefinition>& tools,
                           const std::string& response_format);

  LLMGatewayConfig config_;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, std::shared_ptr<llm::LLMProvider>> providers_;
  std::unordered_map<std::string, TokenBucket> rate_buckets_;
  std::unordered_map<std::string, ProviderHealth> health_;
  std::unordered_map<std::string, CacheEntry> cache_;
  std::deque<std::string> cache_order_;
  CostReport cost_report_;
};

}  // namespace neamc::vm
