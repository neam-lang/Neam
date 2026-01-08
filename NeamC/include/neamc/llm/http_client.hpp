//
// Neam LLM - HTTP client helpers
//

#pragma once

#include <string>
#include <vector>

namespace neamc::llm
{
std::string http_post_json(const std::string& url, const std::string& body,
                           const std::vector<std::string>& headers,
                           long timeout_ms = 30000);
}  // namespace neamc::llm
