//
// Neam LLM - HTTP client helpers
//
// v0.6.5: Connection pooling, retry with exponential backoff, structured logging.
//

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace neamc::llm
{

// HTTP response with status code accessible to callers (for retry logic)
struct HttpResult
{
  long status = 0;
  std::string body;
};

// Core HTTP POST — single attempt, returns status + body.
// Throws std::runtime_error on network/curl failure.
HttpResult http_post_json_raw(const std::string& url, const std::string& body,
                              const std::vector<std::string>& headers,
                              long timeout_ms = 30000);

// HTTP POST with retry and exponential backoff.
// Retries on 429, 5xx, and network errors (up to max_attempts).
// Throws std::runtime_error on final failure or non-retryable errors (4xx).
std::string http_post_json(const std::string& url, const std::string& body,
                           const std::vector<std::string>& headers,
                           long timeout_ms = 30000);

// Generic HTTP request — single attempt, returns status + body.
// Supports GET, POST, DELETE, PUT, PATCH.
// Throws std::runtime_error on network/curl failure.
HttpResult http_request(const std::string& method, const std::string& url,
                        const std::string& body,
                        const std::vector<std::string>& headers,
                        long timeout_ms = 30000);

// v0.6.8: Streaming HTTP POST — calls chunk_callback for each received chunk.
// Used for SSE (Server-Sent Events) and NDJSON streaming.
using StreamChunkCallback = std::function<void(const char* data, size_t size)>;

void http_post_streaming(const std::string& url, const std::string& body,
                         const std::vector<std::string>& headers,
                         StreamChunkCallback chunk_callback,
                         // v1.4.5.1: 300s recv-idle window — gpt-5 on hard
                         // reasoning problems can silence-think for 2+ minutes.
                         long timeout_ms = 300000);

// v0.7.2: Connection pool configuration and monitoring
struct ConnectionPoolConfig
{
  size_t max_pool_size{8};      // Max idle connections
  int stale_timeout_secs{60};   // Evict connections idle longer than this
};

struct ConnectionPoolStats
{
  uint64_t created{0};
  uint64_t reused{0};
  uint64_t evicted{0};
  size_t current_idle{0};
};

void configure_connection_pool(const ConnectionPoolConfig& config);
ConnectionPoolStats connection_pool_stats();
void prewarm_connection_pool(const std::vector<std::string>& urls);

}  // namespace neamc::llm
