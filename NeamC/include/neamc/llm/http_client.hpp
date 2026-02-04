// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - HTTP client helpers
//

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace neamc::llm
{
/// GET request returning response body
std::string http_get(const std::string& url,
                     const std::vector<std::string>& headers = {},
                     long timeout_ms = 30000);

std::string http_post_json(const std::string& url, const std::string& body,
                           const std::vector<std::string>& headers,
                           long timeout_ms = 30000);

/// Streaming callback: receives each chunk of data as it arrives.
/// Return false to abort the stream.
using StreamCallback = std::function<bool(const std::string& chunk)>;

/// POST with streaming response. Calls the callback with each chunk of data.
void http_post_stream(const std::string& url, const std::string& body,
                      const std::vector<std::string>& headers,
                      const StreamCallback& callback,
                      long timeout_ms = 120000);

/// POST with multipart/form-data (for file uploads like Whisper STT).
std::string http_post_multipart(const std::string& url,
                                const std::vector<std::string>& headers,
                                const std::string& file_field,
                                const std::string& file_path,
                                const std::vector<std::pair<std::string, std::string>>& fields,
                                long timeout_ms = 60000);
}  // namespace neamc::llm
