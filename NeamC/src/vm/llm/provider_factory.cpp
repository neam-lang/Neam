// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Provider factory
//

#include "neamc/llm/provider_factory.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <stdexcept>

#include "neamc/llm/anthropic_adapter.hpp"
#include "neamc/llm/azure_openai_adapter.hpp"
#include "neamc/llm/gemini_adapter.hpp"
#include "neamc/llm/ollama_adapter.hpp"
#include "neamc/llm/openai_adapter.hpp"

#ifdef NEAM_BACKEND_AWS
#include "neamc/llm/bedrock_adapter.hpp"
#endif

#ifdef NEAM_BACKEND_GCP
#include "neamc/llm/vertex_adapter.hpp"
#endif

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

std::string resolve_anthropic_key(const ProviderConfig& config)
{
  if (!config.api_key.empty())
  {
    return config.api_key;
  }
  if (const char* env_value = std::getenv("ANTHROPIC_API_KEY"))
  {
    if (*env_value != '\0')
    {
      return std::string(env_value);
    }
  }
  if (const char* env_value = std::getenv("NEAM_ANTHROPIC_API_KEY"))
  {
    if (*env_value != '\0')
    {
      return std::string(env_value);
    }
  }
  return {};
}

std::string resolve_gemini_key(const ProviderConfig& config)
{
  if (!config.api_key.empty())
  {
    return config.api_key;
  }
  if (const char* env_value = std::getenv("GEMINI_API_KEY"))
  {
    if (*env_value != '\0')
    {
      return std::string(env_value);
    }
  }
  if (const char* env_value = std::getenv("NEAM_GEMINI_API_KEY"))
  {
    if (*env_value != '\0')
    {
      return std::string(env_value);
    }
  }
  return {};
}

std::string resolve_azure_openai_key(const ProviderConfig& config)
{
  if (!config.api_key.empty())
  {
    return config.api_key;
  }
  if (const char* env_value = std::getenv("AZURE_OPENAI_API_KEY"))
  {
    if (*env_value != '\0')
    {
      return std::string(env_value);
    }
  }
  if (const char* env_value = std::getenv("NEAM_AZURE_OPENAI_API_KEY"))
  {
    if (*env_value != '\0')
    {
      return std::string(env_value);
    }
  }
  return {};
}

std::string default_azure_openai_endpoint()
{
  if (const char* env_value = std::getenv("AZURE_OPENAI_ENDPOINT"))
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
  if (normalized == "anthropic" || normalized == "claude")
  {
    ProviderConfig with_key = config;
    with_key.api_key = resolve_anthropic_key(config);
    if (with_key.endpoint.empty())
    {
      with_key.endpoint = "https://api.anthropic.com/v1/messages";
    }
    return create_anthropic_provider(with_key);
  }
  if (normalized == "gemini" || normalized == "google")
  {
    ProviderConfig with_key = config;
    with_key.api_key = resolve_gemini_key(config);
    // Gemini doesn't use a single endpoint; URL is built per-request
    return create_gemini_provider(with_key);
  }
  if (normalized == "azure_openai" || normalized == "azure-openai" || normalized == "azureopenai")
  {
    ProviderConfig with_key = config;
    with_key.api_key = resolve_azure_openai_key(config);
    if (with_key.endpoint.empty())
    {
      with_key.endpoint = default_azure_openai_endpoint();
    }
    return create_azure_openai_provider(with_key);
  }
#ifdef NEAM_BACKEND_AWS
  if (normalized == "bedrock" || normalized == "aws_bedrock" || normalized == "aws-bedrock")
  {
    ProviderConfig cfg = config;
    if (cfg.endpoint.empty())
    {
      const char* region = std::getenv("AWS_REGION");
      std::string r = region ? region : "us-east-1";
      cfg.endpoint = "https://bedrock-runtime." + r + ".amazonaws.com";
    }
    return create_bedrock_provider(cfg);
  }
#endif
#ifdef NEAM_BACKEND_GCP
  if (normalized == "vertex" || normalized == "vertex_ai" || normalized == "vertex-ai")
  {
    return create_vertex_provider(config);
  }
#endif
  throw std::runtime_error("Unknown LLM provider: " + provider);
}
}  // namespace neamc::llm
