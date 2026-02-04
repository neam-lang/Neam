// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Knowledge (RAG) subsystem
//

#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#if NEAM_USE_HNSW
namespace unum::usearch
{
template <typename, typename>
class index_dense_gt;
using index_dense_t = index_dense_gt<std::uint64_t, std::uint32_t>;
}  // namespace unum::usearch
#endif

namespace neamc::vm::knowledge
{
struct Source
{
  std::string type;
  std::string path;
  std::string content;
};

struct Chunk
{
  std::string text;
  std::string source;
  std::size_t index{0};
};

struct SearchResult
{
  Chunk chunk;
  float score{0.0f};
};

// Embedding callback: text -> float vector (provided by EmbeddingProvider or hash fallback)
using EmbeddingCallback = std::function<std::vector<float>(const std::string&)>;

// Retrieval strategy enum
enum class Strategy
{
  kBasic = 0,
  kMMR = 1,
  kHybrid = 2,
  kHyDE = 3,
  kSelfRAG = 4,
  kCRAG = 5,
  kAgentic = 6,
  kGraphRAG = 7
};

// Strategy options
struct StrategyOptions
{
  std::size_t top_k{4};
  double relevance_threshold{0.5};
  double mmr_lambda{0.5};
  std::size_t num_hypothetical{1};
  bool enable_relevance_check{true};
  bool enable_support_check{true};
  bool enable_web_fallback{false};
  bool enable_query_decomposition{true};
  std::size_t max_corrections{2};
  std::size_t max_iterations{5};
  bool enable_reflection{true};
  std::size_t search_depth{2};
  bool include_communities{true};
};

// Retrieval result with metadata
struct RetrievalResult
{
  std::vector<SearchResult> documents;
  std::string strategy_used;
  std::vector<std::string> hypothetical_docs;  // For HyDE
  std::vector<std::string> sub_queries;        // For CRAG
  bool relevance_checked{false};               // For Self-RAG
  bool support_checked{false};                 // For Self-RAG
};

class VectorStore
{
public:
  explicit VectorStore(std::size_t dimensions = 0);
  ~VectorStore();

  // Non-copyable, movable
  VectorStore(const VectorStore&) = delete;
  VectorStore& operator=(const VectorStore&) = delete;
  VectorStore(VectorStore&& other) noexcept;
  VectorStore& operator=(VectorStore&& other) noexcept;

  std::size_t dimensions() const { return dimensions_; }
  std::size_t size() const { return entries_.size(); }

  /// Set dimensions and initialize HNSW index when dims >= 64.
  void set_dimensions(std::size_t dims);

  void add(const std::vector<float>& embedding, Chunk chunk);

  // Basic similarity search
  std::vector<SearchResult> search(const std::vector<float>& embedding, std::size_t top_k) const;

  // MMR (Maximal Marginal Relevance) search for diversity
  std::vector<SearchResult> search_mmr(const std::vector<float>& embedding, std::size_t top_k,
                                       double lambda = 0.5) const;

  // Hybrid search (keyword + vector) - simplified version
  std::vector<SearchResult> search_hybrid(const std::vector<float>& embedding,
                                          const std::string& query_text, std::size_t top_k,
                                          double vector_weight = 0.7) const;

  // Get all entries for custom processing
  const std::vector<std::pair<std::vector<float>, Chunk>>& entries() const;

private:
  struct Entry
  {
    std::vector<float> embedding;
    Chunk chunk;
  };

  std::size_t dimensions_{0};
  std::vector<Entry> entries_{};
#if NEAM_USE_HNSW
  bool use_hnsw_{false};
  std::unique_ptr<unum::usearch::index_dense_t> hnsw_index_;
#endif
};

/// Hash-based embedding fallback (original behavior).
std::vector<float> embed_text(const std::string& text, std::size_t dimensions);

/// Embed text using callback if available, else hash fallback.
std::vector<float> embed_text(const std::string& text, std::size_t dimensions,
                              const EmbeddingCallback& callback);

// LLM callback type for advanced strategies
using LLMCallback = std::function<std::string(const std::string& prompt)>;

// Strategy-aware retrieval
RetrievalResult retrieve_with_strategy(VectorStore& store, const std::string& query,
                                       Strategy strategy, const StrategyOptions& options,
                                       LLMCallback llm_callback = nullptr,
                                       EmbeddingCallback embed_callback = nullptr);

class Ingester
{
public:
  Ingester(VectorStore& store, std::size_t chunk_size, std::size_t chunk_overlap,
           std::string embedding_model, EmbeddingCallback embed_callback = nullptr);
  void ingest(const Source& source);

private:
  std::vector<std::string> chunk_text(const std::string& text) const;
  void ingest_text(const std::string& text, const std::string& source_label);
  std::string fetch_url(const std::string& url) const;
  std::string strip_html(const std::string& html) const;
  std::vector<std::string> expand_glob(const std::string& pattern) const;

  VectorStore& store_;
  std::size_t chunk_size_{0};
  std::size_t chunk_overlap_{0};
  std::string embedding_model_;
  EmbeddingCallback embed_callback_;
};
}  // namespace neamc::vm::knowledge
