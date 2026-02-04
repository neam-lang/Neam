// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Knowledge (RAG) subsystem
//

#include "neamc/vm/knowledge.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <regex>
#include <set>
#include <sstream>

#include <curl/curl.h>
#if NEAM_USE_HNSW
#include <usearch/index_dense.hpp>
#endif

namespace neamc::vm::knowledge
{
namespace
{
float dot_product(const std::vector<float>& a, const std::vector<float>& b)
{
  const std::size_t n = std::min(a.size(), b.size());
  float sum = 0.0f;
  for (std::size_t i = 0; i < n; ++i)
  {
    sum += a[i] * b[i];
  }
  return sum;
}

float l2_norm(const std::vector<float>& v)
{
  float sum = 0.0f;
  for (float value : v)
  {
    sum += value * value;
  }
  return std::sqrt(sum);
}

std::vector<float> normalize(const std::vector<float>& v)
{
  const float norm = l2_norm(v);
  if (norm == 0.0f)
  {
    return v;
  }
  std::vector<float> out = v;
  for (auto& value : out)
  {
    value /= norm;
  }
  return out;
}

size_t curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
  const size_t total = size * nmemb;
  auto* buffer = static_cast<std::string*>(userp);
  buffer->append(static_cast<char*>(contents), total);
  return total;
}

std::vector<std::string> split_words(const std::string& text)
{
  std::istringstream stream(text);
  std::vector<std::string> words;
  std::string word;
  while (stream >> word)
  {
    words.push_back(word);
  }
  return words;
}

std::string join_words(const std::vector<std::string>& words, std::size_t start, std::size_t count)
{
  std::ostringstream out;
  const std::size_t end = std::min(words.size(), start + count);
  for (std::size_t i = start; i < end; ++i)
  {
    if (i > start)
    {
      out << ' ';
    }
    out << words[i];
  }
  return out.str();
}
}  // namespace

// --- VectorStore ---

VectorStore::VectorStore(std::size_t dimensions) : dimensions_(dimensions)
{
  if (dimensions_ >= 64)
  {
    set_dimensions(dimensions_);
  }
}

VectorStore::~VectorStore() = default;

VectorStore::VectorStore(VectorStore&& other) noexcept
    : dimensions_(other.dimensions_),
      entries_(std::move(other.entries_))
#if NEAM_USE_HNSW
      , use_hnsw_(other.use_hnsw_),
      hnsw_index_(std::move(other.hnsw_index_))
#endif
{
  other.dimensions_ = 0;
#if NEAM_USE_HNSW
  other.use_hnsw_ = false;
#endif
}

VectorStore& VectorStore::operator=(VectorStore&& other) noexcept
{
  if (this != &other)
  {
    dimensions_ = other.dimensions_;
    entries_ = std::move(other.entries_);
#if NEAM_USE_HNSW
    use_hnsw_ = other.use_hnsw_;
    hnsw_index_ = std::move(other.hnsw_index_);
    other.use_hnsw_ = false;
#endif
    other.dimensions_ = 0;
  }
  return *this;
}

void VectorStore::set_dimensions(std::size_t dims)
{
  dimensions_ = dims;
#if NEAM_USE_HNSW
  if (dims >= 64)
  {
    namespace us = unum::usearch;
    us::metric_punned_t metric(dims, us::metric_kind_t::cos_k, us::scalar_kind_t::f32_k);
    hnsw_index_ = std::make_unique<us::index_dense_t>(us::index_dense_t::make(metric));
    use_hnsw_ = true;
  }
  else
  {
    hnsw_index_.reset();
    use_hnsw_ = false;
  }
#endif
}

void VectorStore::add(const std::vector<float>& embedding, Chunk chunk)
{
  if (embedding.empty())
  {
    return;
  }
  auto normed = normalize(embedding);
  std::size_t entry_id = entries_.size();
  entries_.push_back(Entry{normed, std::move(chunk)});

#if NEAM_USE_HNSW
  if (use_hnsw_ && hnsw_index_)
  {
    hnsw_index_->reserve(entries_.size());
    hnsw_index_->add(static_cast<std::uint64_t>(entry_id), normed.data());
  }
#endif
}

std::vector<SearchResult> VectorStore::search(const std::vector<float>& embedding,
                                              std::size_t top_k) const
{
  std::vector<SearchResult> results;
  if (entries_.empty() || embedding.empty())
  {
    return results;
  }
  const auto query = normalize(embedding);

#if NEAM_USE_HNSW
  // Use HNSW for O(log n) search when available
  if (use_hnsw_ && hnsw_index_ && hnsw_index_->size() > 0)
  {
    auto search_result = hnsw_index_->search(query.data(), top_k);
    results.reserve(search_result.count);
    for (std::size_t i = 0; i < search_result.count; ++i)
    {
      auto match = search_result[i];
      auto idx = static_cast<std::size_t>(match.member.key);
      if (idx < entries_.size())
      {
        // uSearch returns distance (lower = closer for cosine).
        // Convert to similarity: score = 1 - distance.
        float score = 1.0f - match.distance;
        results.push_back(SearchResult{entries_[idx].chunk, score});
      }
    }
    return results;
  }
#endif

  // Linear scan fallback (for low dimensions or when HNSW not available)
  results.reserve(entries_.size());
  for (const auto& entry : entries_)
  {
    const float score = dot_product(query, entry.embedding);
    results.push_back(SearchResult{entry.chunk, score});
  }
  std::sort(results.begin(), results.end(),
            [](const SearchResult& a, const SearchResult& b) { return a.score > b.score; });
  if (results.size() > top_k)
  {
    results.resize(top_k);
  }
  return results;
}

std::vector<SearchResult> VectorStore::search_mmr(const std::vector<float>& embedding,
                                                  std::size_t top_k, double lambda) const
{
  std::vector<SearchResult> results;
  if (entries_.empty() || embedding.empty())
  {
    return results;
  }
  const auto query = normalize(embedding);

  // Calculate all similarities to query
  std::vector<std::pair<std::size_t, float>> candidates;
  for (std::size_t i = 0; i < entries_.size(); ++i)
  {
    const float score = dot_product(query, entries_[i].embedding);
    candidates.emplace_back(i, score);
  }

  // MMR selection
  std::vector<std::size_t> selected_indices;
  while (selected_indices.size() < top_k && !candidates.empty())
  {
    float best_mmr = -std::numeric_limits<float>::infinity();
    std::size_t best_idx = 0;
    std::size_t best_candidate_idx = 0;

    for (std::size_t c = 0; c < candidates.size(); ++c)
    {
      const auto& [idx, query_sim] = candidates[c];

      // Find max similarity to already selected documents
      float max_selected_sim = 0.0f;
      for (std::size_t sel_idx : selected_indices)
      {
        const float sim = dot_product(entries_[idx].embedding, entries_[sel_idx].embedding);
        max_selected_sim = std::max(max_selected_sim, sim);
      }

      // MMR score = lambda * sim(d, q) - (1-lambda) * max(sim(d, d_selected))
      const float mmr_score =
          static_cast<float>(lambda) * query_sim - static_cast<float>(1.0 - lambda) * max_selected_sim;

      if (mmr_score > best_mmr)
      {
        best_mmr = mmr_score;
        best_idx = idx;
        best_candidate_idx = c;
      }
    }

    selected_indices.push_back(best_idx);
    results.push_back(SearchResult{entries_[best_idx].chunk, candidates[best_candidate_idx].second});
    candidates.erase(candidates.begin() + static_cast<std::ptrdiff_t>(best_candidate_idx));
  }

  return results;
}

std::vector<SearchResult> VectorStore::search_hybrid(const std::vector<float>& embedding,
                                                     const std::string& query_text, std::size_t top_k,
                                                     double vector_weight) const
{
  std::vector<SearchResult> results;
  if (entries_.empty() || embedding.empty())
  {
    return results;
  }

  const auto query = normalize(embedding);
  const auto query_words = split_words(query_text);

  // Compute hybrid scores
  std::vector<std::pair<std::size_t, float>> scores;
  for (std::size_t i = 0; i < entries_.size(); ++i)
  {
    // Vector similarity
    const float vector_score = dot_product(query, entries_[i].embedding);

    // Keyword score (simple term frequency)
    float keyword_score = 0.0f;
    const auto doc_words = split_words(entries_[i].chunk.text);
    for (const auto& qw : query_words)
    {
      for (const auto& dw : doc_words)
      {
        if (qw == dw)
        {
          keyword_score += 1.0f;
        }
      }
    }
    if (!doc_words.empty())
    {
      keyword_score /= static_cast<float>(doc_words.size());
    }

    // Hybrid score
    const float hybrid_score =
        static_cast<float>(vector_weight) * vector_score +
        static_cast<float>(1.0 - vector_weight) * keyword_score;
    scores.emplace_back(i, hybrid_score);
  }

  std::sort(scores.begin(), scores.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

  for (std::size_t i = 0; i < std::min(top_k, scores.size()); ++i)
  {
    results.push_back(SearchResult{entries_[scores[i].first].chunk, scores[i].second});
  }

  return results;
}

const std::vector<std::pair<std::vector<float>, Chunk>>& VectorStore::entries() const
{
  // Note: This is a workaround - we return a reference to an empty static vector
  // and the caller should use the search methods instead
  static std::vector<std::pair<std::vector<float>, Chunk>> empty;
  return empty;
}

// --- embed_text ---

std::vector<float> embed_text(const std::string& text, std::size_t dimensions)
{
  // Hash-based fallback: use 8 dims if dimensions is 0
  const std::size_t effective_dims = dimensions == 0 ? 8 : dimensions;
  std::vector<float> embedding(effective_dims, 0.0f);
  std::hash<std::string> hasher;
  auto words = split_words(text);
  for (const auto& word : words)
  {
    const std::size_t hash = hasher(word);
    const std::size_t index = hash % effective_dims;
    embedding[index] += 1.0f;
  }
  return normalize(embedding);
}

std::vector<float> embed_text(const std::string& text, std::size_t dimensions,
                              const EmbeddingCallback& callback)
{
  if (callback)
  {
    return callback(text);
  }
  return embed_text(text, dimensions);
}

// --- retrieve_with_strategy ---

RetrievalResult retrieve_with_strategy(VectorStore& store, const std::string& query,
                                       Strategy strategy, const StrategyOptions& options,
                                       LLMCallback llm_callback,
                                       EmbeddingCallback embed_callback)
{
  RetrievalResult result;

  // Helper to embed text using callback or hash fallback
  auto do_embed = [&](const std::string& text) -> std::vector<float> {
    return embed_text(text, store.dimensions(), embed_callback);
  };

  const auto embedding = do_embed(query);

  switch (strategy)
  {
    case Strategy::kBasic:
    {
      result.documents = store.search(embedding, options.top_k);
      result.strategy_used = "basic";
      break;
    }
    case Strategy::kMMR:
    {
      result.documents = store.search_mmr(embedding, options.top_k, options.mmr_lambda);
      result.strategy_used = "mmr";
      break;
    }
    case Strategy::kHybrid:
    {
      result.documents = store.search_hybrid(embedding, query, options.top_k, 0.7);
      result.strategy_used = "hybrid";
      break;
    }
    case Strategy::kHyDE:
    {
      // HyDE: Generate hypothetical document, then search with that
      if (llm_callback)
      {
        std::string hyde_prompt =
            "Please write a passage that would answer the following question. "
            "Write only the passage, no preamble.\n\nQuestion: " +
            query + "\n\nPassage:";

        for (std::size_t i = 0; i < options.num_hypothetical; ++i)
        {
          std::string hypothetical = llm_callback(hyde_prompt);
          result.hypothetical_docs.push_back(hypothetical);
        }

        // Average embeddings of hypothetical docs
        std::vector<float> combined_embedding(store.dimensions(), 0.0f);
        for (const auto& hyp_doc : result.hypothetical_docs)
        {
          auto hyp_emb = do_embed(hyp_doc);
          for (std::size_t j = 0; j < combined_embedding.size() && j < hyp_emb.size(); ++j)
          {
            combined_embedding[j] += hyp_emb[j];
          }
        }
        if (!result.hypothetical_docs.empty())
        {
          for (auto& val : combined_embedding)
          {
            val /= static_cast<float>(result.hypothetical_docs.size());
          }
        }

        result.documents = store.search(combined_embedding, options.top_k);
      }
      else
      {
        // Fallback to basic if no LLM callback
        result.documents = store.search(embedding, options.top_k);
      }
      result.strategy_used = "hyde";
      break;
    }
    case Strategy::kSelfRAG:
    {
      // Self-RAG: Retrieve, check relevance, optionally regenerate
      auto candidates = store.search(embedding, options.top_k * 2);  // Get more candidates

      if (options.enable_relevance_check && llm_callback)
      {
        result.relevance_checked = true;
        for (const auto& doc : candidates)
        {
          std::string relevance_prompt =
              "Is this document relevant to the question?\n\n"
              "Question: " +
              query + "\n\nDocument: " + doc.chunk.text +
              "\n\nRespond with only 'relevant' or 'not_relevant':";
          std::string response = llm_callback(relevance_prompt);

          // Simple check for "relevant" at start
          std::string lower_response = response;
          std::transform(lower_response.begin(), lower_response.end(), lower_response.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

          if (lower_response.find("relevant") == 0 || lower_response.find("yes") == 0)
          {
            result.documents.push_back(doc);
            if (result.documents.size() >= options.top_k)
            {
              break;
            }
          }
        }
      }
      else
      {
        // No relevance check, just take top_k
        for (std::size_t i = 0; i < std::min(options.top_k, candidates.size()); ++i)
        {
          result.documents.push_back(candidates[i]);
        }
      }
      result.strategy_used = "self_rag";
      break;
    }
    case Strategy::kCRAG:
    {
      // CRAG: Corrective RAG with query decomposition
      auto initial_results = store.search(embedding, options.top_k);

      // Check if results are sufficient
      bool results_sufficient = false;
      if (!initial_results.empty() && initial_results[0].score > options.relevance_threshold)
      {
        results_sufficient = true;
      }

      if (!results_sufficient && options.enable_query_decomposition && llm_callback)
      {
        // Decompose query into sub-queries
        std::string decompose_prompt =
            "Break down this complex question into 2-3 simpler sub-questions:\n\n"
            "Question: " +
            query + "\n\nSub-questions (one per line):";
        std::string sub_queries_text = llm_callback(decompose_prompt);

        // Parse sub-queries
        std::istringstream stream(sub_queries_text);
        std::string line;
        while (std::getline(stream, line))
        {
          // Remove numbering and trim
          while (!line.empty() && (std::isdigit(line[0]) || line[0] == '.' || line[0] == '-' ||
                                   line[0] == ' '))
          {
            line.erase(0, 1);
          }
          if (line.length() > 10)
          {
            result.sub_queries.push_back(line);
          }
        }

        // Search for each sub-query
        std::set<std::string> seen_texts;
        for (const auto& sub_query : result.sub_queries)
        {
          auto sub_emb = do_embed(sub_query);
          auto sub_results = store.search(sub_emb, options.top_k);
          for (const auto& doc : sub_results)
          {
            if (seen_texts.find(doc.chunk.text) == seen_texts.end())
            {
              seen_texts.insert(doc.chunk.text);
              result.documents.push_back(doc);
            }
          }
        }

        // Limit total results
        if (result.documents.size() > options.top_k * 2)
        {
          result.documents.resize(options.top_k * 2);
        }
      }
      else
      {
        result.documents = initial_results;
      }
      result.strategy_used = "crag";
      break;
    }
    case Strategy::kAgentic:
    {
      // Agentic: Multi-step retrieval with planning
      // Simplified version: iterative refinement
      result.documents = store.search(embedding, options.top_k);

      if (llm_callback && options.enable_reflection)
      {
        for (std::size_t iter = 0; iter < options.max_iterations; ++iter)
        {
          // Check if we have enough relevant info
          std::string context;
          for (const auto& doc : result.documents)
          {
            context += doc.chunk.text + "\n\n";
          }

          std::string reflection_prompt =
              "Given this context and question, do we have enough information to answer? "
              "Respond 'yes' or suggest a refined search query.\n\n"
              "Question: " +
              query + "\n\nContext:\n" + context.substr(0, 1000) + "\n\nResponse:";

          std::string response = llm_callback(reflection_prompt);
          std::string lower_response = response;
          std::transform(lower_response.begin(), lower_response.end(), lower_response.begin(),
                         [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

          if (lower_response.find("yes") != std::string::npos)
          {
            break;  // Have enough info
          }

          // Try refined query
          auto refined_emb = do_embed(response);
          auto additional = store.search(refined_emb, options.top_k);

          // Merge results
          std::set<std::string> seen;
          for (const auto& doc : result.documents)
          {
            seen.insert(doc.chunk.text);
          }
          for (const auto& doc : additional)
          {
            if (seen.find(doc.chunk.text) == seen.end())
            {
              result.documents.push_back(doc);
              seen.insert(doc.chunk.text);
            }
          }
        }
      }
      result.strategy_used = "agentic";
      break;
    }
    case Strategy::kGraphRAG:
    {
      // Graph RAG: Simplified version using keyword extraction
      // In a full implementation, this would use an actual knowledge graph
      result.documents = store.search(embedding, options.top_k);

      if (llm_callback)
      {
        // Extract entities from query
        std::string entity_prompt =
            "Extract the key entities (nouns, concepts) from this question. "
            "List them one per line:\n\n" +
            query;
        std::string entities_text = llm_callback(entity_prompt);

        // Search for each entity
        std::istringstream stream(entities_text);
        std::string entity;
        std::set<std::string> seen;
        for (const auto& doc : result.documents)
        {
          seen.insert(doc.chunk.text);
        }

        while (std::getline(stream, entity))
        {
          if (entity.length() < 3)
            continue;
          auto entity_emb = do_embed(entity);
          auto entity_results = store.search(entity_emb, 2);
          for (const auto& doc : entity_results)
          {
            if (seen.find(doc.chunk.text) == seen.end())
            {
              result.documents.push_back(doc);
              seen.insert(doc.chunk.text);
            }
          }
        }
      }
      result.strategy_used = "graph_rag";
      break;
    }
  }

  // Filter by relevance threshold
  if (options.relevance_threshold > 0.0)
  {
    std::vector<SearchResult> filtered;
    for (const auto& doc : result.documents)
    {
      if (doc.score >= options.relevance_threshold)
      {
        filtered.push_back(doc);
      }
    }
    if (!filtered.empty())
    {
      result.documents = filtered;
    }
  }

  return result;
}

// --- Ingester ---

Ingester::Ingester(VectorStore& store, std::size_t chunk_size, std::size_t chunk_overlap,
                   std::string embedding_model, EmbeddingCallback embed_callback)
    : store_(store),
      chunk_size_(chunk_size),
      chunk_overlap_(chunk_overlap),
      embedding_model_(std::move(embedding_model)),
      embed_callback_(std::move(embed_callback))
{
}

void Ingester::ingest(const Source& source)
{
  if (source.type == "web")
  {
    const std::string html = fetch_url(source.path);
    if (!html.empty())
    {
      ingest_text(strip_html(html), source.path);
    }
    return;
  }
  if (source.type == "file")
  {
    const auto paths = expand_glob(source.path);
    for (const auto& path : paths)
    {
      std::ifstream in(path, std::ios::binary);
      if (!in)
      {
        continue;
      }
      std::ostringstream buffer;
      buffer << in.rdbuf();
      ingest_text(buffer.str(), path);
    }
    return;
  }
  if (source.type == "text")
  {
    if (!source.content.empty())
    {
      ingest_text(source.content, "inline-text");
    }
    return;
  }
}

void Ingester::ingest_text(const std::string& text, const std::string& source_label)
{
  const auto chunks = chunk_text(text);
  std::size_t index = 0;
  for (const auto& chunk : chunks)
  {
    if (chunk.empty())
    {
      continue;
    }
    auto embedding = embed_text(chunk, store_.dimensions(), embed_callback_);

    // Auto-detect dimensions on first real embedding
    if (store_.dimensions() == 0 && !embedding.empty())
    {
      store_.set_dimensions(embedding.size());
    }

    store_.add(embedding, Chunk{chunk, source_label, index++});
  }
}

std::vector<std::string> Ingester::chunk_text(const std::string& text) const
{
  std::vector<std::string> chunks;
  if (text.empty())
  {
    return chunks;
  }
  const auto words = split_words(text);
  if (words.empty())
  {
    return chunks;
  }
  const std::size_t window = chunk_size_ == 0 ? words.size() : chunk_size_;
  const std::size_t overlap = std::min(chunk_overlap_, window);
  const std::size_t step = window > overlap ? window - overlap : window;
  for (std::size_t start = 0; start < words.size(); start += step)
  {
    const auto chunk = join_words(words, start, window);
    if (!chunk.empty())
    {
      chunks.push_back(chunk);
    }
    if (start + window >= words.size())
    {
      break;
    }
  }
  return chunks;
}

std::string Ingester::fetch_url(const std::string& url) const
{
  CURL* curl = curl_easy_init();
  if (!curl)
  {
    return {};
  }
  std::string buffer;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
  CURLcode result = curl_easy_perform(curl);
  curl_easy_cleanup(curl);
  if (result != CURLE_OK)
  {
    return {};
  }
  return buffer;
}

std::string Ingester::strip_html(const std::string& html) const
{
  std::string text;
  text.reserve(html.size());
  bool in_tag = false;
  bool in_entity = false;
  std::string entity;
  for (char c : html)
  {
    if (in_tag)
    {
      if (c == '>')
      {
        in_tag = false;
        text.push_back(' ');
      }
      continue;
    }
    if (in_entity)
    {
      if (c == ';')
      {
        if (entity == "amp")
        {
          text.push_back('&');
        }
        else if (entity == "lt")
        {
          text.push_back('<');
        }
        else if (entity == "gt")
        {
          text.push_back('>');
        }
        else if (entity == "quot")
        {
          text.push_back('"');
        }
        else if (entity == "apos")
        {
          text.push_back('\'');
        }
        else if (entity == "nbsp")
        {
          text.push_back(' ');
        }
        entity.clear();
        in_entity = false;
      }
      else
      {
        entity.push_back(c);
      }
      continue;
    }
    if (c == '<')
    {
      in_tag = true;
      continue;
    }
    if (c == '&')
    {
      in_entity = true;
      entity.clear();
      continue;
    }
    text.push_back(c);
  }
  return text;
}

std::vector<std::string> Ingester::expand_glob(const std::string& pattern) const
{
  if (pattern.find('*') == std::string::npos)
  {
    return {pattern};
  }
  std::filesystem::path path(pattern);
  const auto dir = path.parent_path();
  const std::string filename_pattern = path.filename().string();
  std::string regex_pattern;
  regex_pattern.reserve(filename_pattern.size() * 2);
  for (char c : filename_pattern)
  {
    if (c == '*')
    {
      regex_pattern += ".*";
    }
    else if (c == '.')
    {
      regex_pattern += "\\.";
    }
    else
    {
      regex_pattern.push_back(c);
    }
  }
  std::regex matcher(regex_pattern);
  std::vector<std::string> matches;
  if (dir.empty() || !std::filesystem::exists(dir))
  {
    return matches;
  }
  for (const auto& entry : std::filesystem::directory_iterator(dir))
  {
    if (!entry.is_regular_file())
    {
      continue;
    }
    const auto name = entry.path().filename().string();
    if (std::regex_match(name, matcher))
    {
      matches.push_back(entry.path().string());
    }
  }
  return matches;
}
}  // namespace neamc::vm::knowledge
