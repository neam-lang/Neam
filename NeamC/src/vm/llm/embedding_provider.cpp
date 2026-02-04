// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Embedding provider factory
//

#include "neamc/llm/embedding_provider.hpp"

namespace neamc::llm
{
// Forward declarations for concrete adapters
std::unique_ptr<EmbeddingProvider> create_openai_embedding_provider(const EmbeddingConfig& config);
std::unique_ptr<EmbeddingProvider> create_ollama_embedding_provider(const EmbeddingConfig& config);

std::unique_ptr<EmbeddingProvider> create_embedding_provider(const EmbeddingConfig& config)
{
  if (config.provider == "openai")
  {
    return create_openai_embedding_provider(config);
  }
  if (config.provider == "ollama")
  {
    return create_ollama_embedding_provider(config);
  }
  // Unknown provider — caller should fall back to hash-based embeddings
  return nullptr;
}
}  // namespace neamc::llm
