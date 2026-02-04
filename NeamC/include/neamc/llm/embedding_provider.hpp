// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Embedding provider interface
//

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace neamc::llm
{
struct EmbeddingConfig
{
  std::string provider;   // "openai" or "ollama"
  std::string model;      // e.g. "text-embedding-3-small", "nomic-embed-text"
  std::string endpoint;   // custom endpoint (optional, defaults per provider)
  std::string api_key;    // API key (required for openai)
  std::size_t dimensions{0};  // 0 = auto-detect from first response
};

class EmbeddingProvider
{
public:
  virtual ~EmbeddingProvider() = default;

  /// Embed a single text string, returning a float vector.
  virtual std::vector<float> embed(const std::string& text) = 0;

  /// Embed multiple texts in a single API call where supported.
  virtual std::vector<std::vector<float>> embed_batch(const std::vector<std::string>& texts) = 0;

  /// Return the embedding dimensions (0 until first call auto-detects).
  virtual std::size_t dimensions() const = 0;
};

/// Factory: creates an EmbeddingProvider for the given config.
/// Returns nullptr for unknown provider (caller falls back to hash).
std::unique_ptr<EmbeddingProvider> create_embedding_provider(const EmbeddingConfig& config);
}  // namespace neamc::llm
