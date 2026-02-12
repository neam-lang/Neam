//
// Neam v0.6.9 — Rate Limiting & Circuit Breaker Implementation (D5)
//

#include "neamc/security/rate_limiter.hpp"

namespace neamc::security {

// --- RateLimiter ---

RateLimiter& RateLimiter::instance()
{
  static RateLimiter inst;
  return inst;
}

void RateLimiter::configure(const RateLimitConfig& config)
{
  std::lock_guard<std::mutex> lock(mutex_);
  config_ = config;
}

void RateLimiter::configure_input_limits(const InputLimits& limits)
{
  std::lock_guard<std::mutex> lock(mutex_);
  input_limits_ = limits;
}

bool RateLimiter::check_window(SlidingWindow& window, int max_rpm)
{
  auto now = std::chrono::steady_clock::now();
  auto one_minute_ago = now - std::chrono::seconds(60);

  // Evict expired entries
  while (!window.timestamps.empty() && window.timestamps.front() < one_minute_ago)
  {
    window.timestamps.pop_front();
  }

  if (static_cast<int>(window.timestamps.size()) >= max_rpm)
  {
    return false;  // Rate limited
  }

  window.timestamps.push_back(now);
  return true;
}

bool RateLimiter::check_api_key(const std::string& key)
{
  std::lock_guard<std::mutex> lock(mutex_);
  return check_window(key_windows_[key], config_.api_key_rpm);
}

bool RateLimiter::check_ip(const std::string& ip)
{
  std::lock_guard<std::mutex> lock(mutex_);
  return check_window(ip_windows_[ip], config_.ip_rpm);
}

bool RateLimiter::check_tool(const std::string& tool_name)
{
  std::lock_guard<std::mutex> lock(mutex_);

  auto it = config_.per_tool_rpm.find(tool_name);
  if (it == config_.per_tool_rpm.end())
  {
    return true;  // No per-tool limit configured
  }
  return check_window(tool_windows_[tool_name], it->second);
}

bool RateLimiter::check_concurrent(const std::string& key)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto& count = concurrent_counts_[key];
  if (count >= config_.concurrent_max)
  {
    return false;
  }
  ++count;
  return true;
}

void RateLimiter::release_concurrent(const std::string& key)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = concurrent_counts_.find(key);
  if (it != concurrent_counts_.end() && it->second > 0)
  {
    --it->second;
  }
}

// --- CircuitBreaker ---

CircuitBreaker& CircuitBreaker::instance()
{
  static CircuitBreaker inst;
  return inst;
}

void CircuitBreaker::configure(const CircuitBreakerConfig& config)
{
  std::lock_guard<std::mutex> lock(mutex_);
  config_ = config;
}

bool CircuitBreaker::is_open(const std::string& tool_name)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = states_.find(tool_name);
  if (it == states_.end()) return false;

  auto& state = it->second;
  if (!state.open) return false;

  // Check if cooldown has elapsed
  auto now = std::chrono::steady_clock::now();
  if (now >= state.cooldown_until)
  {
    // Half-open: allow one attempt
    state.open = false;
    state.failures.clear();
    return false;
  }

  return true;  // Still in cooldown
}

void CircuitBreaker::record_success(const std::string& tool_name)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = states_.find(tool_name);
  if (it != states_.end())
  {
    it->second.open = false;
    it->second.failures.clear();
  }
}

void CircuitBreaker::record_failure(const std::string& tool_name)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto& state = states_[tool_name];
  auto now = std::chrono::steady_clock::now();
  auto window_start = now - std::chrono::seconds(config_.window_seconds);

  // Add failure timestamp
  state.failures.push_back(now);

  // Evict old failures outside the window
  while (!state.failures.empty() && state.failures.front() < window_start)
  {
    state.failures.pop_front();
  }

  // Trip the breaker if threshold exceeded
  if (static_cast<int>(state.failures.size()) >= config_.failure_threshold)
  {
    state.open = true;
    state.cooldown_until = now + std::chrono::seconds(config_.cooldown_seconds);
  }
}

}  // namespace neamc::security
