// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - LLM Gateway (v0.6.0)
//

#include "neamc/vm/llm_gateway.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <random>
#include <sstream>
#include <thread>

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

namespace neamc::vm
{
namespace
{
std::string sha256_hex(const std::string& input)
{
  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (ctx)
  {
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, input.data(), input.size());
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
  }

  std::ostringstream oss;
  for (unsigned int i = 0; i < hash_len; ++i)
  {
    oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
  }
  return oss.str();
}

double jitter(double base)
{
  thread_local std::mt19937 gen(std::random_device{}());
  std::uniform_real_distribution<double> dist(0.75, 1.25);
  return base * dist(gen);
}

bool is_transient_error(const std::exception& e)
{
  std::string msg = e.what();
  // Check for HTTP status codes that indicate transient errors
  return msg.find("429") != std::string::npos || msg.find("500") != std::string::npos ||
         msg.find("502") != std::string::npos || msg.find("503") != std::string::npos ||
         msg.find("504") != std::string::npos || msg.find("timeout") != std::string::npos ||
         msg.find("connection") != std::string::npos;
}
}  // namespace

LLMGateway::LLMGateway(const LLMGatewayConfig& config)
    : config_(config)
{
  // Initialize rate buckets
  for (const auto& [provider, limit] : config_.rate_limits)
  {
    TokenBucket bucket;
    bucket.max_tokens = limit.requests_per_minute > 0 ? limit.requests_per_minute : 1000000;
    bucket.tokens = static_cast<double>(bucket.max_tokens);
    bucket.last_refill = std::chrono::steady_clock::now();
    bucket.refill_rate =
        limit.requests_per_minute > 0 ? static_cast<double>(limit.requests_per_minute) / 60.0 : 0.0;
    rate_buckets_[provider] = bucket;
  }
}

LLMGateway::~LLMGateway() = default;

void LLMGateway::register_provider(const std::string& name,
                                    std::shared_ptr<llm::LLMProvider> provider)
{
  std::lock_guard lock(mutex_);
  providers_[name] = std::move(provider);
  health_[name] = ProviderHealth{};
}

llm::ChatResult LLMGateway::send(const std::string& provider_name,
                                  const std::vector<llm::Message>& messages,
                                  const std::vector<llm::ToolDefinition>& tools,
                                  const std::string& response_format)
{
  // Check cache first
  if (config_.cache.enabled)
  {
    std::string cache_key = compute_cache_key(provider_name, messages);
    std::lock_guard lock(mutex_);
    auto it = cache_.find(cache_key);
    if (it != cache_.end() && it->second.expires > std::chrono::steady_clock::now())
    {
      try
      {
        auto cached = nlohmann::json::parse(it->second.response_json);
        llm::ChatResult result;
        result.content = cached.value("content", "");
        if (cached.contains("usage"))
        {
          result.usage.prompt_tokens = cached["usage"].value("prompt_tokens", size_t{0});
          result.usage.completion_tokens = cached["usage"].value("completion_tokens", size_t{0});
          result.usage.total_tokens = cached["usage"].value("total_tokens", size_t{0});
        }
        return result;
      }
      catch (...)
      {
        cache_.erase(it);
      }
    }
  }

  // Build provider chain: primary + fallbacks
  std::vector<std::string> chain;
  chain.push_back(provider_name);
  for (const auto& fallback : config_.fallback_chain)
  {
    if (fallback != provider_name)
    {
      chain.push_back(fallback);
    }
  }

  // Try each provider in chain
  std::string last_error;
  for (const auto& provider : chain)
  {
    // Check circuit breaker
    {
      std::lock_guard lock(mutex_);
      if (is_circuit_open(provider))
      {
        last_error = "Circuit open for provider: " + provider;
        continue;
      }
    }

    // Rate limiting
    if (!try_consume_rate(provider))
    {
      last_error = "Rate limit exceeded for provider: " + provider;
      continue;
    }

    // Retry with exponential backoff
    const int max_retries = 3;
    double backoff_ms = 1000.0;

    for (int attempt = 0; attempt <= max_retries; ++attempt)
    {
      if (attempt > 0)
      {
        auto sleep_ms = static_cast<int>(jitter(backoff_ms));
        std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
        backoff_ms *= 2.0;
      }

      try
      {
        auto result = try_send(provider, messages, tools, response_format);
        record_success(provider);

        // Cache the result if caching enabled and temperature is 0
        if (config_.cache.enabled)
        {
          std::string cache_key = compute_cache_key(provider, messages);
          nlohmann::json cached_json = {
              {"content", result.content},
              {"usage",
               {{"prompt_tokens", result.usage.prompt_tokens},
                {"completion_tokens", result.usage.completion_tokens},
                {"total_tokens", result.usage.total_tokens}}}};

          std::lock_guard lock(mutex_);
          // LRU eviction
          while (cache_.size() >= config_.cache.max_entries && !cache_order_.empty())
          {
            cache_.erase(cache_order_.front());
            cache_order_.pop_front();
          }
          cache_[cache_key] = {cached_json.dump(),
                               std::chrono::steady_clock::now() + config_.cache.ttl};
          cache_order_.push_back(cache_key);
        }

        return result;
      }
      catch (const std::exception& e)
      {
        last_error = e.what();
        if (!is_transient_error(e) || attempt == max_retries)
        {
          record_failure(provider);
          break;
        }
      }
    }
  }

  throw std::runtime_error("All LLM providers exhausted. Last error: " + last_error);
}

std::string LLMGateway::send_stream(const std::string& provider_name,
                                     const std::vector<llm::Message>& messages,
                                     const llm::LLMProvider::TokenCallback& callback)
{
  std::lock_guard lock(mutex_);
  auto it = providers_.find(provider_name);
  if (it == providers_.end())
  {
    throw std::runtime_error("Unknown provider: " + provider_name);
  }
  return it->second->chat_stream(messages, callback);
}

CostReport LLMGateway::get_cost_report() const
{
  std::lock_guard lock(mutex_);
  return cost_report_;
}

void LLMGateway::record_usage(const std::string& provider, const std::string& model,
                               const llm::UsageInfo& usage)
{
  // Simple cost estimation (can be enhanced with cost_table.cpp)
  double cost_per_1k_prompt = 0.0015;
  double cost_per_1k_completion = 0.002;
  double cost = (static_cast<double>(usage.prompt_tokens) / 1000.0) * cost_per_1k_prompt +
                (static_cast<double>(usage.completion_tokens) / 1000.0) * cost_per_1k_completion;

  std::lock_guard lock(mutex_);
  cost_report_.daily_total += cost;
  cost_report_.monthly_total += cost;
  cost_report_.by_provider[provider] += cost;
  cost_report_.by_model[model] += cost;

  // Alert check
  if (config_.cost.alert_threshold_daily > 0.0 &&
      cost_report_.daily_total > config_.cost.alert_threshold_daily)
  {
    std::cerr << "[WARN] [llm-gateway] Daily cost alert: $" << cost_report_.daily_total << " exceeds $"
              << config_.cost.alert_threshold_daily << "\n";
  }
  if (config_.cost.alert_threshold_monthly > 0.0 &&
      cost_report_.monthly_total > config_.cost.alert_threshold_monthly)
  {
    std::cerr << "[WARN] [llm-gateway] Monthly cost alert: $" << cost_report_.monthly_total << " exceeds $"
              << config_.cost.alert_threshold_monthly << "\n";
  }
}

std::unordered_map<std::string, ProviderHealth> LLMGateway::get_provider_health() const
{
  std::lock_guard lock(mutex_);
  return health_;
}

bool LLMGateway::try_consume_rate(const std::string& provider)
{
  std::lock_guard lock(mutex_);
  auto it = rate_buckets_.find(provider);
  if (it == rate_buckets_.end())
  {
    return true;  // No rate limit configured
  }

  auto& bucket = it->second;
  if (bucket.refill_rate <= 0.0)
  {
    return true;
  }

  auto now = std::chrono::steady_clock::now();
  auto elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - bucket.last_refill).count();
  double refill = (static_cast<double>(elapsed) / 1000.0) * bucket.refill_rate;
  bucket.tokens = std::min(static_cast<double>(bucket.max_tokens), bucket.tokens + refill);
  bucket.last_refill = now;

  if (bucket.tokens < 1.0)
  {
    return false;
  }

  bucket.tokens -= 1.0;
  return true;
}

bool LLMGateway::is_circuit_open(const std::string& provider) const
{
  auto it = health_.find(provider);
  if (it == health_.end())
  {
    return false;
  }

  const auto& h = it->second;
  if (h.circuit == ProviderHealth::CircuitState::kClosed)
  {
    return false;
  }

  if (h.circuit == ProviderHealth::CircuitState::kOpen)
  {
    auto elapsed = std::chrono::steady_clock::now() - h.last_failure;
    if (elapsed >= config_.circuit_breaker.recovery_timeout)
    {
      // Transition to half-open (allow one probe)
      // Note: const_cast needed since this is a const method that transitions state
      const_cast<ProviderHealth&>(h).circuit = ProviderHealth::CircuitState::kHalfOpen;
      return false;
    }
    return true;
  }

  // HalfOpen: allow one request
  return false;
}

void LLMGateway::record_success(const std::string& provider)
{
  std::lock_guard lock(mutex_);
  auto& h = health_[provider];
  h.total_requests++;
  h.consecutive_failures = 0;
  if (h.circuit == ProviderHealth::CircuitState::kHalfOpen)
  {
    h.circuit = ProviderHealth::CircuitState::kClosed;
  }
}

void LLMGateway::record_failure(const std::string& provider)
{
  std::lock_guard lock(mutex_);
  auto& h = health_[provider];
  h.total_requests++;
  h.total_failures++;
  h.consecutive_failures++;
  h.last_failure = std::chrono::steady_clock::now();

  if (h.consecutive_failures >= config_.circuit_breaker.failure_threshold)
  {
    h.circuit = ProviderHealth::CircuitState::kOpen;
    std::cerr << "[WARN] [llm-gateway] Circuit opened for provider: " << provider
              << " (failures: " << h.consecutive_failures << ")\n";
  }
  else if (h.circuit == ProviderHealth::CircuitState::kHalfOpen)
  {
    h.circuit = ProviderHealth::CircuitState::kOpen;
  }
}

std::string LLMGateway::compute_cache_key(const std::string& provider,
                                           const std::vector<llm::Message>& messages) const
{
  std::string input = provider;
  for (const auto& msg : messages)
  {
    input += "|" + msg.role + ":" + msg.content;
  }
  return sha256_hex(input);
}

llm::ChatResult LLMGateway::try_send(const std::string& provider_name,
                                      const std::vector<llm::Message>& messages,
                                      const std::vector<llm::ToolDefinition>& tools,
                                      const std::string& response_format)
{
  std::shared_ptr<llm::LLMProvider> provider;
  {
    std::lock_guard lock(mutex_);
    auto it = providers_.find(provider_name);
    if (it == providers_.end())
    {
      throw std::runtime_error("Unknown provider: " + provider_name);
    }
    provider = it->second;
  }

  return provider->chat_with_tools(messages, tools, response_format);
}

}  // namespace neamc::vm
