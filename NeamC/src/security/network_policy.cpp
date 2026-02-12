//
// Neam v0.6.9 — Network & SSRF Protection Implementation (D4)
//

#include "neamc/security/network_policy.hpp"
#include "neamc/security/audit_log.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#else
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace neamc::security {

// --- Global state ---

static bool g_network_policy_enabled = false;
static NetworkPolicy g_network_policy;

NetworkPolicy& global_network_policy()
{
  return g_network_policy;
}

void set_network_policy_enabled(bool enabled)
{
  g_network_policy_enabled = enabled;
}

bool network_policy_enabled()
{
  return g_network_policy_enabled;
}

// --- IP validation ---

bool is_private_ipv4(uint32_t addr)
{
  // 10.0.0.0/8
  if ((addr & 0xFF000000) == 0x0A000000) return true;
  // 172.16.0.0/12
  if ((addr & 0xFFF00000) == 0xAC100000) return true;
  // 192.168.0.0/16
  if ((addr & 0xFFFF0000) == 0xC0A80000) return true;
  // 127.0.0.0/8
  if ((addr & 0xFF000000) == 0x7F000000) return true;
  // 169.254.0.0/16 (link-local / AWS metadata)
  if ((addr & 0xFFFF0000) == 0xA9FE0000) return true;
  // 0.0.0.0/8
  if ((addr & 0xFF000000) == 0x00000000) return true;
  return false;
}

bool is_private_ip(const std::string& ip)
{
  if (ip.empty()) return false;

  // IPv6 private ranges
  if (ip == "::1" || ip == "0:0:0:0:0:0:0:1")
  {
    return true;
  }
  if (ip.size() >= 4 && (ip.substr(0, 2) == "fc" || ip.substr(0, 2) == "fd"))
  {
    return true;  // fc00::/7
  }
  if (ip.substr(0, 4) == "fe80")
  {
    return true;  // link-local IPv6
  }

  // IPv4
  struct in_addr addr;
  if (inet_pton(AF_INET, ip.c_str(), &addr) == 1)
  {
    uint32_t host_order = ntohl(addr.s_addr);
    return is_private_ipv4(host_order);
  }

  return false;
}

// --- URL parsing ---

bool parse_url(const std::string& url, URLComponents& out)
{
  // scheme://host[:port][/path][?query]
  auto scheme_end = url.find("://");
  if (scheme_end == std::string::npos)
  {
    return false;
  }

  out.scheme = url.substr(0, scheme_end);
  std::transform(out.scheme.begin(), out.scheme.end(), out.scheme.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  auto host_start = scheme_end + 3;
  auto path_start = url.find('/', host_start);
  auto query_start = url.find('?', host_start);

  std::string host_port;
  if (path_start != std::string::npos)
  {
    host_port = url.substr(host_start, path_start - host_start);
    if (query_start != std::string::npos && query_start > path_start)
    {
      out.path = url.substr(path_start, query_start - path_start);
      out.query = url.substr(query_start + 1);
    }
    else
    {
      out.path = url.substr(path_start);
    }
  }
  else if (query_start != std::string::npos)
  {
    host_port = url.substr(host_start, query_start - host_start);
    out.query = url.substr(query_start + 1);
  }
  else
  {
    host_port = url.substr(host_start);
  }

  // Parse host:port
  auto colon = host_port.rfind(':');
  if (colon != std::string::npos && host_port[0] != '[')
  {
    out.host = host_port.substr(0, colon);
    try
    {
      out.port = std::stoi(host_port.substr(colon + 1));
    }
    catch (...)
    {
      out.port = 0;
    }
  }
  else
  {
    out.host = host_port;
    out.port = (out.scheme == "https") ? 443 : 80;
  }

  return !out.host.empty();
}

// --- URL validation ---

// Forward-declare match_glob from tool_policy
bool match_glob(const std::string& pattern, const std::string& value);

URLValidationResult validate_url(const std::string& url, const NetworkPolicy& policy)
{
  URLComponents parts;
  if (!parse_url(url, parts))
  {
    return {false, "Invalid URL format"};
  }

  // Only allow http and https schemes
  if (parts.scheme != "http" && parts.scheme != "https")
  {
    return {false, "Scheme '" + parts.scheme + "' not allowed (only http/https)"};
  }

  // Check deny hosts (deny takes priority)
  for (const auto& pattern : policy.deny_hosts)
  {
    if (match_glob(pattern, parts.host))
    {
      return {false, "Host '" + parts.host + "' matches deny pattern '" + pattern + "'"};
    }
  }

  // Check allow hosts (if non-empty, host must match at least one)
  if (!policy.allow_hosts.empty())
  {
    bool matched = false;
    for (const auto& pattern : policy.allow_hosts)
    {
      if (match_glob(pattern, parts.host))
      {
        matched = true;
        break;
      }
    }
    if (!matched)
    {
      return {false, "Host '" + parts.host + "' not in allow list"};
    }
  }

  // Resolve hostname and check against private IP ranges
  if (policy.block_private_ips)
  {
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo* result = nullptr;
    int ret = getaddrinfo(parts.host.c_str(), nullptr, &hints, &result);
    if (ret != 0)
    {
      return {false, "DNS resolution failed for '" + parts.host + "'"};
    }

    bool is_private = false;
    for (struct addrinfo* rp = result; rp != nullptr; rp = rp->ai_next)
    {
      char ip_str[INET6_ADDRSTRLEN];
      if (rp->ai_family == AF_INET)
      {
        auto* addr4 = reinterpret_cast<struct sockaddr_in*>(rp->ai_addr);
        inet_ntop(AF_INET, &addr4->sin_addr, ip_str, sizeof(ip_str));
      }
      else if (rp->ai_family == AF_INET6)
      {
        auto* addr6 = reinterpret_cast<struct sockaddr_in6*>(rp->ai_addr);
        inet_ntop(AF_INET6, &addr6->sin6_addr, ip_str, sizeof(ip_str));
      }
      else
      {
        continue;
      }

      if (is_private_ip(ip_str))
      {
        is_private = true;
        break;
      }
    }
    freeaddrinfo(result);

    if (is_private)
    {
      return {false,
              "Host '" + parts.host + "' resolves to a private/internal IP address"};
    }
  }

  return {true, "URL allowed"};
}

// --- Sanitization ---

std::string url_encode_value(const std::string& value)
{
  std::ostringstream encoded;
  encoded.fill('0');
  encoded << std::hex;

  for (unsigned char c : value)
  {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
    {
      encoded << c;
    }
    else
    {
      encoded << '%' << std::setw(2) << static_cast<int>(c);
    }
  }
  return encoded.str();
}

std::string sanitize_header_value(const std::string& value)
{
  // Reject CRLF injection
  for (char c : value)
  {
    if (c == '\r' || c == '\n')
    {
      return "";  // Return empty — caller should skip this header
    }
  }
  return value;
}

}  // namespace neamc::security
