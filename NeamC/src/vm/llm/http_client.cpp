// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - HTTP client helpers
//

#include "neamc/llm/http_client.hpp"

#include <curl/curl.h>

#ifdef _WIN32
#include <windows.h>
#endif

#include <filesystem>
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

#ifdef _WIN32
void set_ca_bundle(CURL* handle)
{
  // Look for curl-ca-bundle.crt next to the executable
  wchar_t exe_path[MAX_PATH];
  if (GetModuleFileNameW(nullptr, exe_path, MAX_PATH) > 0)
  {
    std::filesystem::path ca_path = std::filesystem::path(exe_path).parent_path() / "curl-ca-bundle.crt";
    if (std::filesystem::exists(ca_path))
    {
      curl_easy_setopt(handle, CURLOPT_CAINFO, ca_path.string().c_str());
    }
  }
}
#else
void set_ca_bundle(CURL*) {}
#endif

struct StreamState
{
  const StreamCallback* callback;
  bool aborted{false};
};

size_t stream_write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* state = static_cast<StreamState*>(userdata);
  const size_t total = size * nmemb;
  if (state->aborted)
  {
    return 0;
  }
  std::string chunk(ptr, total);
  if (!(*state->callback)(chunk))
  {
    state->aborted = true;
    return 0;
  }
  return total;
}
}  // namespace

std::string http_get(const std::string& url,
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
  set_ca_bundle(handle);
  curl_easy_setopt(handle, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);

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
  set_ca_bundle(handle);
  curl_easy_setopt(handle, CURLOPT_POST, 1L);
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, body.size());
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);

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

void http_post_stream(const std::string& url, const std::string& body,
                      const std::vector<std::string>& headers,
                      const StreamCallback& callback,
                      long timeout_ms)
{
  (void)curl_global();
  CURL* handle = curl_easy_init();
  if (!handle)
  {
    throw std::runtime_error("Failed to initialize curl");
  }

  StreamState state{&callback, false};

  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
  set_ca_bundle(handle);
  curl_easy_setopt(handle, CURLOPT_POST, 1L);
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, stream_write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &state);

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

  if (!state.aborted && res != CURLE_OK)
  {
    throw std::runtime_error(std::string("HTTP stream failed: ") + curl_easy_strerror(res));
  }
  if (status >= 400)
  {
    throw std::runtime_error("HTTP stream error: status " + std::to_string(status));
  }
}
std::string http_post_multipart(const std::string& url,
                                const std::vector<std::string>& headers,
                                const std::string& file_field,
                                const std::string& file_path,
                                const std::vector<std::pair<std::string, std::string>>& fields,
                                long timeout_ms)
{
  (void)curl_global();
  CURL* handle = curl_easy_init();
  if (!handle)
  {
    throw std::runtime_error("Failed to initialize curl");
  }

  curl_mime* mime = curl_mime_init(handle);

  // Add file part
  curl_mimepart* file_part = curl_mime_addpart(mime);
  curl_mime_name(file_part, file_field.c_str());
  curl_mime_filedata(file_part, file_path.c_str());

  // Add form fields
  for (const auto& field : fields)
  {
    curl_mimepart* part = curl_mime_addpart(mime);
    curl_mime_name(part, field.first.c_str());
    curl_mime_data(part, field.second.c_str(), CURL_ZERO_TERMINATED);
  }

  std::string response;
  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
  set_ca_bundle(handle);
  curl_easy_setopt(handle, CURLOPT_MIMEPOST, mime);
  curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, timeout_ms);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);

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
  curl_mime_free(mime);
  curl_easy_cleanup(handle);

  if (res != CURLE_OK)
  {
    std::ostringstream message;
    message << "HTTP multipart request failed: " << curl_easy_strerror(res);
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
