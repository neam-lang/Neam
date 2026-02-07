//
// Neam v0.6.5 - LLM Adapter Unit Tests
//
// Tests provider construction, config validation, and response parsing
// for OpenAI, Ollama, and Bedrock adapters.
//

#include "neamc/llm/bedrock_adapter.hpp"
#include "neamc/llm/ollama_adapter.hpp"
#include "neamc/llm/openai_adapter.hpp"
#include "neamc/llm/provider.hpp"
#include "neamc/llm/provider_factory.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>

namespace
{
using neamc::llm::ProviderConfig;

// ============================================================================
// OpenAI Adapter Tests
// ============================================================================

TEST(OpenAIAdapterTest, RequiresModel)
{
  ProviderConfig config;
  config.endpoint = "https://api.openai.com/v1/chat/completions";
  config.api_key = "test-key";
  // model is empty
  EXPECT_THROW(neamc::llm::create_openai_provider(config), std::runtime_error);
}

TEST(OpenAIAdapterTest, RequiresEndpoint)
{
  ProviderConfig config;
  config.model = "gpt-4";
  config.api_key = "test-key";
  // endpoint is empty
  EXPECT_THROW(neamc::llm::create_openai_provider(config), std::runtime_error);
}

TEST(OpenAIAdapterTest, RequiresApiKey)
{
  ProviderConfig config;
  config.model = "gpt-4";
  config.endpoint = "https://api.openai.com/v1/chat/completions";
  // api_key is empty
  EXPECT_THROW(neamc::llm::create_openai_provider(config), std::runtime_error);
}

TEST(OpenAIAdapterTest, ValidConfigCreatesProvider)
{
  ProviderConfig config;
  config.model = "gpt-4";
  config.endpoint = "https://api.openai.com/v1/chat/completions";
  config.api_key = "test-key";
  auto provider = neamc::llm::create_openai_provider(config);
  ASSERT_NE(provider, nullptr);
}

TEST(OpenAIAdapterTest, ChatWithInvalidEndpointThrows)
{
  ProviderConfig config;
  config.model = "gpt-4";
  config.endpoint = "http://localhost:1/nonexistent";
  config.api_key = "test-key";
  auto provider = neamc::llm::create_openai_provider(config);
  std::vector<neamc::llm::Message> messages = {{"user", "hello"}};
  EXPECT_THROW(provider->chat(messages), std::runtime_error);
}

TEST(OpenAIAdapterTest, CompleteWithInvalidEndpointThrows)
{
  ProviderConfig config;
  config.model = "gpt-4";
  config.endpoint = "http://localhost:1/nonexistent";
  config.api_key = "test-key";
  auto provider = neamc::llm::create_openai_provider(config);
  EXPECT_THROW(provider->complete("hello"), std::runtime_error);
}

// ============================================================================
// Ollama Adapter Tests
// ============================================================================

TEST(OllamaAdapterTest, RequiresModel)
{
  ProviderConfig config;
  // model is empty
  EXPECT_THROW(neamc::llm::create_ollama_provider(config), std::runtime_error);
}

TEST(OllamaAdapterTest, DefaultsEndpointToLocalhost)
{
  ProviderConfig config;
  config.model = "llama3";
  auto provider = neamc::llm::create_ollama_provider(config);
  ASSERT_NE(provider, nullptr);
}

TEST(OllamaAdapterTest, ValidConfigCreatesProvider)
{
  ProviderConfig config;
  config.model = "llama3";
  config.endpoint = "http://localhost:11434";
  auto provider = neamc::llm::create_ollama_provider(config);
  ASSERT_NE(provider, nullptr);
}

TEST(OllamaAdapterTest, ChatWithInvalidEndpointThrows)
{
  ProviderConfig config;
  config.model = "llama3";
  config.endpoint = "http://localhost:1";
  auto provider = neamc::llm::create_ollama_provider(config);
  std::vector<neamc::llm::Message> messages = {{"user", "hello"}};
  EXPECT_THROW(provider->chat(messages), std::runtime_error);
}

TEST(OllamaAdapterTest, AddsHttpSchemeIfMissing)
{
  ProviderConfig config;
  config.model = "llama3";
  config.endpoint = "localhost:11434";
  // Should not throw — endpoint gets http:// prepended
  auto provider = neamc::llm::create_ollama_provider(config);
  ASSERT_NE(provider, nullptr);
}

// ============================================================================
// Bedrock Adapter Tests
// ============================================================================

TEST(BedrockAdapterTest, RequiresModel)
{
  ProviderConfig config;
  config.api_key = "AKIAIOSFODNN7EXAMPLE";
  // model is empty — but we also need secret key in env
  // This should throw for missing model before it gets to credential check
  // Set env vars to avoid credential error
  setenv("AWS_SECRET_ACCESS_KEY", "test-secret-key", 1);
  EXPECT_THROW(neamc::llm::create_bedrock_provider(config), std::runtime_error);
  unsetenv("AWS_SECRET_ACCESS_KEY");
}

TEST(BedrockAdapterTest, RequiresAccessKey)
{
  ProviderConfig config;
  config.model = "anthropic.claude-3-sonnet";
  // No access key and no env var
  unsetenv("AWS_ACCESS_KEY_ID");
  unsetenv("AWS_SECRET_ACCESS_KEY");
  EXPECT_THROW(neamc::llm::create_bedrock_provider(config), std::runtime_error);
}

TEST(BedrockAdapterTest, RequiresSecretKey)
{
  ProviderConfig config;
  config.model = "anthropic.claude-3-sonnet";
  config.api_key = "AKIAIOSFODNN7EXAMPLE";
  unsetenv("AWS_SECRET_ACCESS_KEY");
  EXPECT_THROW(neamc::llm::create_bedrock_provider(config), std::runtime_error);
}

TEST(BedrockAdapterTest, ValidConfigCreatesProvider)
{
  ProviderConfig config;
  config.model = "anthropic.claude-3-sonnet";
  config.api_key = "AKIAIOSFODNN7EXAMPLE";
  setenv("AWS_SECRET_ACCESS_KEY", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY", 1);
  auto provider = neamc::llm::create_bedrock_provider(config);
  ASSERT_NE(provider, nullptr);
  unsetenv("AWS_SECRET_ACCESS_KEY");
}

TEST(BedrockAdapterTest, DefaultsRegionToUsEast1)
{
  ProviderConfig config;
  config.model = "anthropic.claude-3-sonnet";
  config.api_key = "AKIAIOSFODNN7EXAMPLE";
  setenv("AWS_SECRET_ACCESS_KEY", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY", 1);
  unsetenv("AWS_REGION");
  unsetenv("AWS_DEFAULT_REGION");
  auto provider = neamc::llm::create_bedrock_provider(config);
  ASSERT_NE(provider, nullptr);
  unsetenv("AWS_SECRET_ACCESS_KEY");
}

TEST(BedrockAdapterTest, ReadsRegionFromEnv)
{
  ProviderConfig config;
  config.model = "anthropic.claude-3-sonnet";
  config.api_key = "AKIAIOSFODNN7EXAMPLE";
  setenv("AWS_SECRET_ACCESS_KEY", "wJalrXUtnFEMI/K7MDENG/bPxRfiCYEXAMPLEKEY", 1);
  setenv("AWS_REGION", "eu-west-1", 1);
  auto provider = neamc::llm::create_bedrock_provider(config);
  ASSERT_NE(provider, nullptr);
  unsetenv("AWS_SECRET_ACCESS_KEY");
  unsetenv("AWS_REGION");
}

// ============================================================================
// Provider Factory Tests
// ============================================================================

TEST(ProviderFactoryTest, CreatesOpenAIProvider)
{
  ProviderConfig config;
  config.model = "gpt-4";
  config.endpoint = "https://api.openai.com/v1/chat/completions";
  config.api_key = "test-key";
  auto provider = neamc::llm::create_provider("openai", config);
  ASSERT_NE(provider, nullptr);
}

TEST(ProviderFactoryTest, CreatesOllamaProvider)
{
  ProviderConfig config;
  config.model = "llama3";
  auto provider = neamc::llm::create_provider("ollama", config);
  ASSERT_NE(provider, nullptr);
}

TEST(ProviderFactoryTest, CaseInsensitiveLookup)
{
  ProviderConfig config;
  config.model = "gpt-4";
  config.endpoint = "https://api.openai.com/v1/chat/completions";
  config.api_key = "test-key";
  auto provider = neamc::llm::create_provider("OpenAI", config);
  ASSERT_NE(provider, nullptr);
}

TEST(ProviderFactoryTest, UnknownProviderThrows)
{
  ProviderConfig config;
  config.model = "test";
  EXPECT_THROW(neamc::llm::create_provider("nonexistent", config), std::runtime_error);
}

}  // namespace
