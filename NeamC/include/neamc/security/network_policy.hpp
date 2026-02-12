//
// Neam v0.6.9 — Network & SSRF Protection (D4)
//
// Validates URLs before connection to prevent Server-Side Request Forgery.
// Blocks private/internal IPs, enforces host allow/deny lists.
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace neamc::security {

struct NetworkPolicy {
  bool block_private_ips = true;
  int max_redirects = 3;
  std::vector<std::string> allow_hosts;  // glob patterns
  std::vector<std::string> deny_hosts;   // glob patterns
};

struct URLValidationResult {
  bool allowed;
  std::string reason;
};

// Pre-connection URL validation
URLValidationResult validate_url(const std::string& url, const NetworkPolicy& policy);

// Post-DNS resolution IP check
bool is_private_ip(const std::string& ip);
bool is_private_ipv4(uint32_t addr);

// Template value sanitization (URL encode, header injection prevention)
std::string url_encode_value(const std::string& value);
std::string sanitize_header_value(const std::string& value);

// Parse URL into components
struct URLComponents {
  std::string scheme;
  std::string host;
  int port = 0;
  std::string path;
  std::string query;
};

bool parse_url(const std::string& url, URLComponents& out);

// Global network policy (configured at startup)
NetworkPolicy& global_network_policy();
void set_network_policy_enabled(bool enabled);
bool network_policy_enabled();

}  // namespace neamc::security
