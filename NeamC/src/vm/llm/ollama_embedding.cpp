// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Ollama embedding adapter
//

#include "neamc/llm/embedding_provider.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"

namespace neamc::llm
{
namespace
{
std::string ensure_scheme(std::string host)
{
  if (host.rfind("http://", 0) == 0 || host.rfind("https://", 0) == 0)
  {
    return host;
  }
  return "http://" + host;
}

std::string trim_trailing_slash(std::string value)
{
  while (!value.empty() && value.back() == '/')
  {
    value.pop_back();
  }
  return value;
}
}  // namespace

class OllamaEmbeddingAdapter final : public EmbeddingProvider
{
public:
  explicit OllamaEmbeddingAdapter(EmbeddingConfig config)
      : config_(std::move(config))
  {
    if (config_.model.empty())
    {
      throw std::runtime_error("Ollama embedding model is required");
    }
    if (config_.endpoint.empty())
    {
      config_.endpoint = "http://localhost:11434";
    }
    config_.endpoint = ensure_scheme(config_.endpoint);
    dimensions_ = config_.dimensions;
  }

  std::vector<float> embed(const std::string& text) override
  {
    auto batch = embed_batch({text});
    if (batch.empty())
    {
      return {};
    }
    return batch[0];
  }

  std::vector<std::vector<float>> embed_batch(const std::vector<std::string>& texts) override
  {
    if (texts.empty())
    {
      return {};
    }

    nlohmann::json payload;
    payload["model"] = config_.model;
    if (texts.size() == 1)
    {
      payload["input"] = texts[0];
    }
    else
    {
      payload["input"] = texts;
    }

    const std::string url = trim_trailing_slash(config_.endpoint) + "/api/embed";
    const std::string response = http_post_json(
        url, payload.dump(),
        {"Content-Type: application/json"});

    auto json = nlohmann::json::parse(response);
    if (!json.contains("embeddings") || !json["embeddings"].is_array())
    {
      throw std::runtime_error("Ollama embed response missing 'embeddings' array");
    }

    std::vector<std::vector<float>> results;
    results.reserve(json["embeddings"].size());
    for (const auto& emb : json["embeddings"])
    {
      auto vec = emb.get<std::vector<float>>();
      // Auto-detect dimensions from first response
      if (dimensions_ == 0 && !vec.empty())
      {
        dimensions_ = vec.size();
      }
      results.push_back(std::move(vec));
    }
    return results;
  }

  std::size_t dimensions() const override { return dimensions_; }

private:
  EmbeddingConfig config_;
  std::size_t dimensions_{0};
};

std::unique_ptr<EmbeddingProvider> create_ollama_embedding_provider(const EmbeddingConfig& config)
{
  return std::make_unique<OllamaEmbeddingAdapter>(config);
}
}  // namespace neamc::llm
