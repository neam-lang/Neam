// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - OpenAI embedding adapter
//

#include "neamc/llm/embedding_provider.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"

namespace neamc::llm
{
class OpenAIEmbeddingAdapter final : public EmbeddingProvider
{
public:
  explicit OpenAIEmbeddingAdapter(EmbeddingConfig config)
      : config_(std::move(config))
  {
    if (config_.model.empty())
    {
      throw std::runtime_error("OpenAI embedding model is required");
    }
    if (config_.endpoint.empty())
    {
      config_.endpoint = "https://api.openai.com/v1/embeddings";
    }
    if (config_.api_key.empty())
    {
      throw std::runtime_error("OpenAI API key is required for embeddings");
    }
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
    if (config_.dimensions > 0)
    {
      payload["dimensions"] = config_.dimensions;
    }

    const std::string response = http_post_json(
        config_.endpoint, payload.dump(),
        {"Content-Type: application/json",
         "Authorization: Bearer " + config_.api_key});

    auto json = nlohmann::json::parse(response);
    if (!json.contains("data") || !json["data"].is_array())
    {
      throw std::runtime_error("OpenAI embeddings response missing 'data' array");
    }

    std::vector<std::vector<float>> results;
    results.reserve(json["data"].size());
    for (const auto& item : json["data"])
    {
      if (!item.contains("embedding") || !item["embedding"].is_array())
      {
        throw std::runtime_error("OpenAI embeddings response missing 'embedding' in data item");
      }
      auto vec = item["embedding"].get<std::vector<float>>();
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

std::unique_ptr<EmbeddingProvider> create_openai_embedding_provider(const EmbeddingConfig& config)
{
  return std::make_unique<OpenAIEmbeddingAdapter>(config);
}
}  // namespace neamc::llm
