//
// Neam LLM - HTTP client helpers
//
// v0.6.5: Connection pooling, retry with exponential backoff, structured logging.
//

#pragma once

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

}  // namespace neamc::llm
