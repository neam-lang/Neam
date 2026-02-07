//
// Neam LLM - Provider factory
//

#include "neamc/llm/provider_factory.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

#include "neamc/llm/bedrock_adapter.hpp"
#include "neamc/llm/ollama_adapter.hpp"
#include "neamc/llm/openai_adapter.hpp"

namespace neamc::llm
{
namespace
{
std::string to_lower(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

std::string default_openai_endpoint()
{
  if (const char* env_value = std::getenv("NEAM_OPENAI_HOST"))
  {
    if (*env_value != '\0')
    {
      return std::string(env_value);
    }
  }
  return "https://api.openai.com/v1/chat/completions";
}

std::string resolve_openai_key(const ProviderConfig& config)
{
  if (!config.api_key.empty())
  {
    return config.api_key;
  }
  if (const char* env_value = std::getenv("OPENAI_API_KEY"))
  {
    if (*env_value != '\0')
    {
      return std::string(env_value);
    }
  }
  if (const char* env_value = std::getenv("NEAM_OPENAI_API_KEY"))
  {
    if (*env_value != '\0')
    {
      return std::string(env_value);
    }
  }
  return {};
}
}  // namespace

std::unique_ptr<LLMProvider> create_provider(const std::string& provider,
                                             const ProviderConfig& config)
{
  const auto normalized = to_lower(provider);
  if (normalized == "ollama")
  {
    return create_ollama_provider(config);
  }
  if (normalized == "openai")
  {
    ProviderConfig with_key = config;
    with_key.api_key = resolve_openai_key(config);
    if (with_key.endpoint.empty())
    {
      with_key.endpoint = default_openai_endpoint();
    }
    return create_openai_provider(with_key);
  }
  if (normalized == "bedrock")
  {
    return create_bedrock_provider(config);
  }
  throw std::runtime_error("Unknown LLM provider: " + provider);
}
}  // namespace neamc::llm
