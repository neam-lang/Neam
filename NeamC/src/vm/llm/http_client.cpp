//
// Neam LLM - HTTP client helpers
//

#include "neamc/llm/http_client.hpp"

#include <curl/curl.h>

#include <cstdlib>
#include <sstream>
#include <stdexcept>

namespace neamc::llm
{
namespace
{
struct CurlGlobal
{
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
};

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* buffer = static_cast<std::string*>(userdata);
  const size_t total = size * nmemb;
  buffer->append(ptr, total);
  return total;
}

const CurlGlobal& curl_global()
{
  static const CurlGlobal global_init;
  return global_init;
}
}  // namespace

std::string http_post_json(const std::string& url, const std::string& body,
                           const std::vector<std::string>& headers,
                           long timeout_ms)
{
  (void)curl_global();
  CURL* handle = curl_easy_init();
  if (!handle)
  {
    throw std::runtime_error("Failed to initialize curl");
  }

  std::string response;
  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle, CURLOPT_POST, 1L);
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, body.size());
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);

  // TLS certificate verification (v0.6.5 security fix)
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
  curl_easy_cleanup(handle);

  if (res != CURLE_OK)
  {
    std::ostringstream message;
    message << "HTTP request failed: " << curl_easy_strerror(res);
    throw std::runtime_error(message.str());
  }
  if (status >= 400)
  {
    std::ostringstream message;
    message << "HTTP error " << status << ": " << response;
    throw std::runtime_error(message.str());
  }

  return response;
}
}  // namespace neamc::llm
