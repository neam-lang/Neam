//
// Neam LLM - HTTP client helpers
//
// v0.6.5: Connection pooling, retry with exponential backoff, structured logging.
//

#include "neamc/llm/http_client.hpp"

#include <curl/curl.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "neamc/llm/llm_logger.hpp"

namespace neamc::llm
{
namespace
{
// ---------------------------------------------------------------------------
// Global curl init
// ---------------------------------------------------------------------------
struct CurlGlobal
{
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
};

const CurlGlobal& curl_global()
{
  static const CurlGlobal global_init;
  return global_init;
}

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* buffer = static_cast<std::string*>(userdata);
  const size_t total = size * nmemb;
  buffer->append(ptr, total);
  return total;
}

// ---------------------------------------------------------------------------
// Connection pool — reuses CURL handles to avoid repeated TLS handshakes
// ---------------------------------------------------------------------------
struct PoolEntry
{
  CURL* handle = nullptr;
  std::chrono::steady_clock::time_point last_used;
};

class ConnectionPool
{
public:
  static ConnectionPool& instance()
  {
    static ConnectionPool pool;
    return pool;
  }

  CURL* acquire()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    evict_stale();

    if (!pool_.empty())
    {
      CURL* handle = pool_.back().handle;
      pool_.pop_back();
      curl_easy_reset(handle);
      LLMLogger::debug("http", "Reused pooled connection");
      return handle;
    }

    CURL* handle = curl_easy_init();
    if (handle)
    {
      LLMLogger::debug("http", "Created new connection");
    }
    return handle;
  }

  void release(CURL* handle)
  {
    if (!handle)
    {
      return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (pool_.size() >= max_pool_size_)
    {
      curl_easy_cleanup(handle);
      LLMLogger::debug("http", "Pool full, destroyed connection");
      return;
    }
    pool_.push_back({handle, std::chrono::steady_clock::now()});
  }

  ~ConnectionPool()
  {
    for (auto& entry : pool_)
    {
      curl_easy_cleanup(entry.handle);
    }
  }

private:
  ConnectionPool() = default;
  ConnectionPool(const ConnectionPool&) = delete;
  ConnectionPool& operator=(const ConnectionPool&) = delete;

  void evict_stale()
  {
    auto now = std::chrono::steady_clock::now();
    pool_.erase(std::remove_if(pool_.begin(), pool_.end(),
                               [&now](const PoolEntry& e) {
                                 bool stale = (now - e.last_used) > std::chrono::seconds(60);
                                 if (stale)
                                 {
                                   curl_easy_cleanup(e.handle);
                                   LLMLogger::debug("http", "Evicted stale connection");
                                 }
                                 return stale;
                               }),
                pool_.end());
  }

  std::mutex mutex_;
  std::vector<PoolEntry> pool_;
  static constexpr std::size_t max_pool_size_ = 8;
};

// ---------------------------------------------------------------------------
// RAII handle wrapper — returns to pool on scope exit
// ---------------------------------------------------------------------------
struct PooledHandle
{
  CURL* handle;
  explicit PooledHandle(CURL* h) : handle(h) {}
  ~PooledHandle()
  {
    if (handle)
    {
      ConnectionPool::instance().release(handle);
    }
  }
  PooledHandle(const PooledHandle&) = delete;
  PooledHandle& operator=(const PooledHandle&) = delete;
};

// ---------------------------------------------------------------------------
// TLS configuration
// ---------------------------------------------------------------------------
void configure_tls(CURL* handle)
{
  const char* skip_verify = std::getenv("NEAM_TLS_SKIP_VERIFY");
  if (skip_verify && std::string(skip_verify) == "1")
  {
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 0L);
  }
  else
  {
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
  }
}

// ---------------------------------------------------------------------------
// Retry configuration
// ---------------------------------------------------------------------------
struct RetryConfig
{
  int max_attempts = 3;
  int base_delay_ms = 1000;
  int max_delay_ms = 10000;
};

RetryConfig load_retry_config()
{
  RetryConfig config;
  if (const char* env = std::getenv("NEAM_HTTP_MAX_RETRIES"))
  {
    int val = std::atoi(env);
    if (val >= 0 && val <= 10)
    {
      config.max_attempts = val + 1;  // attempts = retries + 1
    }
  }
  return config;
}

bool is_retryable_status(long status)
{
  return status == 429 || status == 500 || status == 502 || status == 503 || status == 504;
}

}  // namespace

// ---------------------------------------------------------------------------
// http_post_json_raw — single attempt, returns status + body
// ---------------------------------------------------------------------------
HttpResult http_post_json_raw(const std::string& url, const std::string& body,
                              const std::vector<std::string>& headers, long timeout_ms)
{
  (void)curl_global();

  CURL* handle = ConnectionPool::instance().acquire();
  if (!handle)
  {
    throw std::runtime_error("Failed to initialize curl");
  }
  PooledHandle guard(handle);

  LLMLogger::debug("http", "POST " + url + " (" + std::to_string(body.size()) + " bytes)");

  std::string response;
  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle, CURLOPT_POST, 1L);
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);

  configure_tls(handle);

  struct curl_slist* header_list = nullptr;
  for (const auto& header : headers)
  {
    header_list = curl_slist_append(header_list, header.c_str());
  }
  if (header_list)
  {
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header_list);
  }

  auto start = std::chrono::steady_clock::now();
  CURLcode res = curl_easy_perform(handle);
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count();

  long status = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);

  if (header_list)
  {
    curl_slist_free_all(header_list);
  }

  if (res != CURLE_OK)
  {
    LLMLogger::error("http", "Request failed: " + std::string(curl_easy_strerror(res)) +
                                 " (" + std::to_string(elapsed) + "ms)");
    // Don't return handle to pool on network error — connection may be broken
    guard.handle = nullptr;
    curl_easy_cleanup(handle);
    throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(res));
  }

  LLMLogger::info("http", "POST " + url + " -> " + std::to_string(status) + " (" +
                               std::to_string(elapsed) + "ms, " +
                               std::to_string(response.size()) + " bytes)");

  return HttpResult{status, std::move(response)};
}

// ---------------------------------------------------------------------------
// http_post_json — with retry and exponential backoff
// ---------------------------------------------------------------------------
std::string http_post_json(const std::string& url, const std::string& body,
                           const std::vector<std::string>& headers, long timeout_ms)
{
  static const RetryConfig config = load_retry_config();

  for (int attempt = 0; attempt < config.max_attempts; ++attempt)
  {
    try
    {
      auto result = http_post_json_raw(url, body, headers, timeout_ms);

      if (result.status < 400)
      {
        return std::move(result.body);
      }

      // Non-retryable client error
      if (result.status >= 400 && result.status < 500 && result.status != 429)
      {
        std::ostringstream message;
        message << "HTTP error " << result.status << ": " << result.body;
        throw std::runtime_error(message.str());
      }

      // Retryable error — check if we have attempts left
      if (!is_retryable_status(result.status) || attempt == config.max_attempts - 1)
      {
        std::ostringstream message;
        message << "HTTP error " << result.status << ": " << result.body;
        throw std::runtime_error(message.str());
      }

      // Exponential backoff
      int delay_ms = config.base_delay_ms * (1 << attempt);
      delay_ms = std::min(delay_ms, config.max_delay_ms);

      LLMLogger::warn("http", "Retryable error " + std::to_string(result.status) +
                                   ", attempt " + std::to_string(attempt + 1) + "/" +
                                   std::to_string(config.max_attempts) + ", retrying in " +
                                   std::to_string(delay_ms) + "ms");

      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
    catch (const std::runtime_error& e)
    {
      // Network-level errors (curl failures) are retryable
      std::string msg = e.what();
      bool is_http_error = msg.rfind("HTTP error", 0) == 0;

      if (is_http_error || attempt == config.max_attempts - 1)
      {
        throw;  // Non-retryable or last attempt
      }

      int delay_ms = config.base_delay_ms * (1 << attempt);
      delay_ms = std::min(delay_ms, config.max_delay_ms);

      LLMLogger::warn("http", "Network error: " + msg + ", attempt " +
                                   std::to_string(attempt + 1) + "/" +
                                   std::to_string(config.max_attempts) + ", retrying in " +
                                   std::to_string(delay_ms) + "ms");

      std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
    }
  }

  throw std::runtime_error("HTTP request failed after " + std::to_string(config.max_attempts) +
                           " attempts");
}

// ---------------------------------------------------------------------------
// http_request — generic HTTP method (GET, POST, DELETE, etc.)
// ---------------------------------------------------------------------------
HttpResult http_request(const std::string& method, const std::string& url,
                        const std::string& body,
                        const std::vector<std::string>& headers,
                        long timeout_ms)
{
  (void)curl_global();

  CURL* handle = ConnectionPool::instance().acquire();
  if (!handle)
  {
    throw std::runtime_error("Failed to initialize curl");
  }
  PooledHandle guard(handle);

  LLMLogger::debug("http", method + " " + url);

  std::string response;
  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);

  if (!body.empty())
  {
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  }

  configure_tls(handle);

  struct curl_slist* header_list = nullptr;
  for (const auto& header : headers)
  {
    header_list = curl_slist_append(header_list, header.c_str());
  }
  if (header_list)
  {
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, header_list);
  }

  CURLcode res = curl_easy_perform(handle);

  long status = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);

  if (header_list)
  {
    curl_slist_free_all(header_list);
  }

  if (res != CURLE_OK)
  {
    guard.handle = nullptr;
    curl_easy_cleanup(handle);
    throw std::runtime_error(std::string("HTTP request failed: ") + curl_easy_strerror(res));
  }

  return HttpResult{status, std::move(response)};
}
}  // namespace neamc::llm
