//
// Neam v0.6.9 — Credential Isolation Implementation (D7)
//

#include "neamc/security/credential_manager.hpp"

#include <algorithm>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace neamc::security {

CredentialManager& CredentialManager::instance()
{
  static CredentialManager inst;
  return inst;
}

std::string CredentialManager::redact(const std::string& text) const
{
  if (text.empty()) return text;

  std::string result = text;

  // OpenAI / Anthropic style keys: sk-... (20+ alphanum chars)
  result = std::regex_replace(result, std::regex(R"(sk-[A-Za-z0-9]{20,})"), "sk-***");

  // Bearer tokens
  result = std::regex_replace(result, std::regex(R"(Bearer [A-Za-z0-9._\-]{10,})"),
                              "Bearer ***");

  // AWS access key IDs
  result = std::regex_replace(result, std::regex(R"(AKIA[A-Z0-9]{16})"), "AKIA***");

  // AWS secret access key in config/env
  result = std::regex_replace(
      result, std::regex(R"(aws_secret_access_key\s*=\s*\S+)"),
      "aws_secret_access_key=***");

  // Generic key=value patterns for known secret env var names
  // Matches: API_KEY=xxx, SECRET_KEY=xxx, TOKEN=xxx, PASSWORD=xxx
  result = std::regex_replace(
      result,
      std::regex(
          R"(([A-Z_]*(KEY|SECRET|TOKEN|PASSWORD)[A-Z_]*)\s*[=:]\s*[^\s,;"'}{]+)",
          std::regex_constants::icase),
      "$1=***");

  return result;
}

std::vector<std::string> CredentialManager::parse_api_keys(
    const std::string& env_value) const
{
  std::vector<std::string> keys;
  if (env_value.empty()) return keys;

  std::istringstream stream(env_value);
  std::string key;
  while (std::getline(stream, key, ','))
  {
    // Trim whitespace
    auto start = key.find_first_not_of(" \t\r\n");
    auto end = key.find_last_not_of(" \t\r\n");
    if (start != std::string::npos)
    {
      keys.push_back(key.substr(start, end - start + 1));
    }
  }
  return keys;
}

std::string CredentialManager::fingerprint(const std::string& key) const
{
  if (key.size() <= 4) return "****";
  return "***" + key.substr(key.size() - 4);
}

std::unordered_map<std::string, std::string> CredentialManager::build_clean_env(
    const std::unordered_map<std::string, std::string>& declared_vars) const
{
  std::unordered_map<std::string, std::string> clean;

  // Minimal required environment variables
  const char* path = std::getenv("PATH");
  if (path) clean["PATH"] = path;

  const char* home = std::getenv("HOME");
  if (home) clean["HOME"] = home;

  const char* lang = std::getenv("LANG");
  if (lang) clean["LANG"] = lang;

  // Add only explicitly declared variables
  for (const auto& [key, value] : declared_vars)
  {
    // If value starts with $, resolve from current environment
    if (!value.empty() && value[0] == '$')
    {
      const char* env_val = std::getenv(value.substr(1).c_str());
      if (env_val)
      {
        clean[key] = env_val;
      }
    }
    else
    {
      clean[key] = value;
    }
  }

  return clean;
}

bool CredentialManager::constant_time_compare(const std::string& a,
                                              const std::string& b)
{
  if (a.size() != b.size()) return false;

  volatile unsigned char result = 0;
  for (size_t i = 0; i < a.size(); ++i)
  {
    result |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
  }
  return result == 0;
}

}  // namespace neamc::security
