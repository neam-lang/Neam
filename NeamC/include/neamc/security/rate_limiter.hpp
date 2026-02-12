//
// Neam v0.6.9 — Rate Limiting & Throttling (D5)
//
// Sliding-window rate limiter for API keys, IPs, and per-tool calls.
// Circuit breaker for failing tools with cooldown period.
//

#pragma once

#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>

namespace neamc::security {

struct RateLimitConfig {
  int api_key_rpm = 60;
  int ip_rpm = 20;
  int concurrent_max = 10;
  std::unordered_map<std::string, int> per_tool_rpm;  // tool_name → max RPM
};

struct InputLimits {
  size_t max_prompt_bytes = 32768;
  size_t max_tool_arg_bytes = 16384;
  size_t max_request_body_bytes = 1048576;
  int max_agent_depth = 5;
};

struct CircuitBreakerConfig {
  int failure_threshold = 5;
  int window_seconds = 60;
  int cooldown_seconds = 30;
};

class RateLimiter {
 public:
  static RateLimiter& instance();

  void configure(const RateLimitConfig& config);
  void configure_input_limits(const InputLimits& limits);

  // Returns true if allowed, false if rate-limited
  bool check_api_key(const std::string& key);
  bool check_ip(const std::string& ip);
  bool check_tool(const std::string& tool_name);

  // Concurrent request tracking — returns false if at limit
  bool check_concurrent(const std::string& key);
  void release_concurrent(const std::string& key);

  const RateLimitConfig& config() const { return config_; }
  const InputLimits& input_limits() const { return input_limits_; }

 private:
  RateLimiter() = default;

  struct SlidingWindow {
    std::deque<std::chrono::steady_clock::time_point> timestamps;
  };

  bool check_window(SlidingWindow& window, int max_rpm);

  RateLimitConfig config_;
  InputLimits input_limits_;

  std::mutex mutex_;
  std::unordered_map<std::string, SlidingWindow> key_windows_;
  std::unordered_map<std::string, SlidingWindow> ip_windows_;
  std::unordered_map<std::string, SlidingWindow> tool_windows_;
  std::unordered_map<std::string, int> concurrent_counts_;
};

class CircuitBreaker {
 public:
  static CircuitBreaker& instance();

  void configure(const CircuitBreakerConfig& config);

  // Returns true if tool circuit is open (should NOT be called)
  bool is_open(const std::string& tool_name);

  void record_success(const std::string& tool_name);
  void record_failure(const std::string& tool_name);

 private:
  CircuitBreaker() = default;

  struct ToolState {
    std::deque<std::chrono::steady_clock::time_point> failures;
    std::chrono::steady_clock::time_point cooldown_until;
    bool open = false;
  };

  CircuitBreakerConfig config_;
  std::mutex mutex_;
  std::unordered_map<std::string, ToolState> states_;
};

}  // namespace neamc::security
