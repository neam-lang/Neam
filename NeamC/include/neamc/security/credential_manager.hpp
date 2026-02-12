//
// Neam v0.6.9 — Credential Isolation (D7)
//
// Redacts secrets from logs/errors, builds clean environments for child
// processes, supports multi-key rotation and fingerprinting.
//

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace neamc::security {

class CredentialManager {
 public:
  static CredentialManager& instance();

  // Redact known secret patterns (API keys, bearer tokens, AWS creds)
  std::string redact(const std::string& text) const;

  // Parse comma-separated API keys from env value
  std::vector<std::string> parse_api_keys(const std::string& env_value) const;

  // Compute fingerprint (last 4 chars) for safe logging
  std::string fingerprint(const std::string& key) const;

  // Build a clean environment map for child processes — only declared vars
  // plus minimal required (PATH, HOME, LANG)
  std::unordered_map<std::string, std::string> build_clean_env(
      const std::unordered_map<std::string, std::string>& declared_vars) const;

  // Constant-time string comparison to prevent timing attacks
  static bool constant_time_compare(const std::string& a, const std::string& b);

 private:
  CredentialManager() = default;
};

}  // namespace neamc::security
