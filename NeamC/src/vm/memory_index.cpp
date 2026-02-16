//
// Neam v0.8 Phase 7 — Memory Index implementation
//

#include "neamc/vm/memory_index.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "neamc/vm/knowledge.hpp"

namespace neamc::vm
{
namespace fs = std::filesystem;

namespace
{

float dot_product(const std::vector<float>& a, const std::vector<float>& b)
{
  float sum = 0.0f;
  std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i)
  {
    sum += a[i] * b[i];
  }
  return sum;
}

float l2_norm(const std::vector<float>& v)
{
  float sum = 0.0f;
  for (float x : v)
  {
    sum += x * x;
  }
  return std::sqrt(sum);
}

void normalize(std::vector<float>& v)
{
  float norm = l2_norm(v);
  if (norm > 1e-9f)
  {
    for (float& x : v)
    {
      x /= norm;
    }
  }
}

std::vector<std::string> split_words(const std::string& text)
{
  std::vector<std::string> words;
  std::string word;
  for (char c : text)
  {
    if (std::isalnum(static_cast<unsigned char>(c)))
    {
      word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    else if (!word.empty())
    {
      words.push_back(std::move(word));
      word.clear();
    }
  }
  if (!word.empty())
  {
    words.push_back(std::move(word));
  }
  return words;
}

int64_t file_mtime_ms(const fs::path& p)
{
  // Use portable time_t approach (file_clock::to_sys may not be available)
  auto ftime = fs::last_write_time(p);
  auto duration = ftime.time_since_epoch();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
  return static_cast<int64_t>(ms);
}

float bm25_score(const std::vector<std::string>& query_words,
                 const std::vector<std::string>& doc_words, double avg_dl,
                 std::size_t num_docs,
                 const std::unordered_map<std::string, std::size_t>& df)
{
  const double k1 = 1.2;
  const double b = 0.75;
  double dl = static_cast<double>(doc_words.size());

  std::unordered_map<std::string, int> tf;
  for (const auto& w : doc_words)
  {
    tf[w]++;
  }

  double score = 0.0;
  for (const auto& qw : query_words)
  {
    auto tf_it = tf.find(qw);
    if (tf_it == tf.end()) continue;

    double freq = static_cast<double>(tf_it->second);
    double n_q = 0.0;
    auto df_it = df.find(qw);
    if (df_it != df.end())
    {
      n_q = static_cast<double>(df_it->second);
    }

    double idf = std::log((static_cast<double>(num_docs) - n_q + 0.5) / (n_q + 0.5) + 1.0);
    double tf_norm = (freq * (k1 + 1.0)) / (freq + k1 * (1.0 - b + b * dl / avg_dl));
    score += idf * tf_norm;
  }
  return static_cast<float>(score);
}

}  // anonymous namespace

MemoryIndex::MemoryIndex(const std::string& workspace_dir, const std::string& search_mode,
                         std::size_t dimensions)
    : workspace_dir_(workspace_dir), search_mode_(search_mode), dimensions_(dimensions)
{
}

std::vector<std::string> MemoryIndex::chunk_text(const std::string& text,
                                                 std::size_t chunk_size,
                                                 std::size_t overlap) const
{
  std::vector<std::string> chunks;
  if (text.empty()) return chunks;

  std::size_t pos = 0;
  while (pos < text.size())
  {
    std::size_t end = std::min(pos + chunk_size, text.size());
    chunks.push_back(text.substr(pos, end - pos));
    if (end >= text.size()) break;
    pos += chunk_size - overlap;
  }
  return chunks;
}

void MemoryIndex::index_document(const std::string& rel_path, const std::string& content)
{
  // Remove old chunks for this file
  auto it = chunks_.begin();
  auto emb_it = embeddings_.begin();
  while (it != chunks_.end())
  {
    if (it->file_path == rel_path)
    {
      it = chunks_.erase(it);
      emb_it = embeddings_.erase(emb_it);
    }
    else
    {
      ++it;
      ++emb_it;
    }
  }

  // Chunk and embed
  auto text_chunks = chunk_text(content);
  for (int i = 0; i < static_cast<int>(text_chunks.size()); ++i)
  {
    MemoryChunk mc;
    mc.file_path = rel_path;
    mc.text = text_chunks[i];
    mc.chunk_index = i;
    chunks_.push_back(std::move(mc));

    auto embedding = knowledge::embed_text(text_chunks[i], dimensions_);
    normalize(embedding);
    embeddings_.push_back(std::move(embedding));
  }
}

void MemoryIndex::index_workspace()
{
  fs::path ws(workspace_dir_);
  if (!fs::exists(ws)) return;

  for (auto& entry : fs::recursive_directory_iterator(ws))
  {
    if (!entry.is_regular_file()) continue;

    auto ext = entry.path().extension().string();
    if (ext != ".md" && ext != ".txt") continue;

    // Skip sessions/ and memory_* files
    auto rel = fs::relative(entry.path(), ws).string();
    if (rel.rfind("sessions/", 0) == 0) continue;
    if (rel.rfind("memory_", 0) == 0) continue;

    std::ifstream f(entry.path());
    if (!f.is_open()) continue;
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    index_document(rel, content);
    file_mtimes_[rel] = file_mtime_ms(entry.path());
  }
}

void MemoryIndex::reindex_changed()
{
  fs::path ws(workspace_dir_);
  if (!fs::exists(ws)) return;

  for (auto& entry : fs::recursive_directory_iterator(ws))
  {
    if (!entry.is_regular_file()) continue;

    auto ext = entry.path().extension().string();
    if (ext != ".md" && ext != ".txt") continue;

    auto rel = fs::relative(entry.path(), ws).string();
    if (rel.rfind("sessions/", 0) == 0) continue;
    if (rel.rfind("memory_", 0) == 0) continue;

    int64_t mtime = file_mtime_ms(entry.path());
    auto it = file_mtimes_.find(rel);
    if (it != file_mtimes_.end() && it->second == mtime) continue;

    std::ifstream f(entry.path());
    if (!f.is_open()) continue;
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    index_document(rel, content);
    file_mtimes_[rel] = mtime;
  }
}

std::vector<MemorySearchResult> MemoryIndex::vector_search(const std::string& query,
                                                           std::size_t top_k) const
{
  if (chunks_.empty()) return {};

  auto query_emb = knowledge::embed_text(query, dimensions_);
  normalize(query_emb);

  std::vector<std::pair<float, std::size_t>> scored;
  scored.reserve(chunks_.size());
  for (std::size_t i = 0; i < chunks_.size(); ++i)
  {
    float sim = dot_product(query_emb, embeddings_[i]);
    scored.push_back({sim, i});
  }

  std::partial_sort(scored.begin(),
                    scored.begin() + std::min(top_k, scored.size()),
                    scored.end(),
                    [](const auto& a, const auto& b) { return a.first > b.first; });

  std::vector<MemorySearchResult> results;
  for (std::size_t i = 0; i < std::min(top_k, scored.size()); ++i)
  {
    auto idx = scored[i].second;
    MemorySearchResult r;
    r.file_path = chunks_[idx].file_path;
    r.chunk = chunks_[idx].text;
    r.score = scored[i].first;
    results.push_back(std::move(r));
  }
  return results;
}

std::vector<MemorySearchResult> MemoryIndex::keyword_search(const std::string& query,
                                                            std::size_t top_k) const
{
  if (chunks_.empty()) return {};

  auto query_words = split_words(query);
  if (query_words.empty()) return {};

  // Compute document frequencies
  std::unordered_map<std::string, std::size_t> df;
  std::vector<std::vector<std::string>> doc_words_vec;
  doc_words_vec.reserve(chunks_.size());

  double total_len = 0.0;
  for (const auto& chunk : chunks_)
  {
    auto words = split_words(chunk.text);
    total_len += static_cast<double>(words.size());

    std::unordered_set<std::string> unique_words(words.begin(), words.end());
    for (const auto& w : unique_words)
    {
      df[w]++;
    }
    doc_words_vec.push_back(std::move(words));
  }

  double avg_dl = total_len / static_cast<double>(chunks_.size());

  std::vector<std::pair<float, std::size_t>> scored;
  for (std::size_t i = 0; i < chunks_.size(); ++i)
  {
    float s = bm25_score(query_words, doc_words_vec[i], avg_dl, chunks_.size(), df);
    if (s > 0.0f)
    {
      scored.push_back({s, i});
    }
  }

  std::partial_sort(scored.begin(),
                    scored.begin() + std::min(top_k, scored.size()),
                    scored.end(),
                    [](const auto& a, const auto& b) { return a.first > b.first; });

  std::vector<MemorySearchResult> results;
  for (std::size_t i = 0; i < std::min(top_k, scored.size()); ++i)
  {
    auto idx = scored[i].second;
    MemorySearchResult r;
    r.file_path = chunks_[idx].file_path;
    r.chunk = chunks_[idx].text;
    r.score = scored[i].first;
    results.push_back(std::move(r));
  }
  return results;
}

std::vector<MemorySearchResult> MemoryIndex::hybrid_search(const std::string& query,
                                                           std::size_t top_k) const
{
  auto vec_results = vector_search(query, top_k * 2);
  auto kw_results = keyword_search(query, top_k * 2);

  // Combine scores: 0.7 * vector + 0.3 * keyword
  // Normalize each set to [0,1] first
  float vec_max = 0.0f;
  for (const auto& r : vec_results)
    vec_max = std::max(vec_max, r.score);
  float kw_max = 0.0f;
  for (const auto& r : kw_results)
    kw_max = std::max(kw_max, r.score);

  // Build combined score map keyed by chunk index
  std::unordered_map<std::size_t, float> combined;

  for (const auto& r : vec_results)
  {
    // Find matching chunk index
    for (std::size_t i = 0; i < chunks_.size(); ++i)
    {
      if (chunks_[i].file_path == r.file_path && chunks_[i].text == r.chunk)
      {
        float norm_score = (vec_max > 1e-9f) ? (r.score / vec_max) : 0.0f;
        combined[i] += 0.7f * norm_score;
        break;
      }
    }
  }

  for (const auto& r : kw_results)
  {
    for (std::size_t i = 0; i < chunks_.size(); ++i)
    {
      if (chunks_[i].file_path == r.file_path && chunks_[i].text == r.chunk)
      {
        float norm_score = (kw_max > 1e-9f) ? (r.score / kw_max) : 0.0f;
        combined[i] += 0.3f * norm_score;
        break;
      }
    }
  }

  std::vector<std::pair<float, std::size_t>> scored(combined.begin(), combined.end());
  std::partial_sort(scored.begin(),
                    scored.begin() + std::min(top_k, scored.size()),
                    scored.end(),
                    [](const auto& a, const auto& b) { return a.first > b.first; });

  std::vector<MemorySearchResult> results;
  for (std::size_t i = 0; i < std::min(top_k, scored.size()); ++i)
  {
    auto idx = scored[i].second;
    MemorySearchResult r;
    r.file_path = chunks_[idx].file_path;
    r.chunk = chunks_[idx].text;
    r.score = scored[i].first;
    results.push_back(std::move(r));
  }
  return results;
}

std::vector<MemorySearchResult> MemoryIndex::search(const std::string& query,
                                                    std::size_t top_k) const
{
  if (search_mode_ == "keyword")
  {
    return keyword_search(query, top_k);
  }
  if (search_mode_ == "vector")
  {
    return vector_search(query, top_k);
  }
  // Default: hybrid
  return hybrid_search(query, top_k);
}

void MemoryIndex::save() const
{
  fs::path ws(workspace_dir_);
  fs::create_directories(ws);

  // 1. Save chunks as JSONL
  {
    std::ofstream out(ws / "memory_chunks.jsonl", std::ios::trunc);
    for (const auto& chunk : chunks_)
    {
      nlohmann::json j;
      j["file_path"] = chunk.file_path;
      j["text"] = chunk.text;
      j["chunk_index"] = chunk.chunk_index;
      out << j.dump() << "\n";
    }
  }

  // 2. Save embeddings as binary: uint32 dims + uint32 count + flat floats
  {
    std::ofstream out(ws / "memory_embeddings.bin", std::ios::binary | std::ios::trunc);
    uint32_t dims = static_cast<uint32_t>(dimensions_);
    uint32_t count = static_cast<uint32_t>(embeddings_.size());
    out.write(reinterpret_cast<const char*>(&dims), sizeof(dims));
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));
    for (const auto& emb : embeddings_)
    {
      out.write(reinterpret_cast<const char*>(emb.data()),
                static_cast<std::streamsize>(emb.size() * sizeof(float)));
    }
  }

  // 3. Save mtimes as JSON
  {
    nlohmann::json j(file_mtimes_);
    std::ofstream out(ws / "memory_mtimes.json", std::ios::trunc);
    out << j.dump(2);
  }
}

void MemoryIndex::load()
{
  fs::path ws(workspace_dir_);

  // 1. Load chunks
  {
    fs::path chunks_path = ws / "memory_chunks.jsonl";
    if (fs::exists(chunks_path))
    {
      std::ifstream in(chunks_path);
      std::string line;
      chunks_.clear();
      while (std::getline(in, line))
      {
        if (line.empty()) continue;
        auto j = nlohmann::json::parse(line, nullptr, false);
        if (j.is_discarded()) continue;
        MemoryChunk mc;
        mc.file_path = j.value("file_path", "");
        mc.text = j.value("text", "");
        mc.chunk_index = j.value("chunk_index", 0);
        chunks_.push_back(std::move(mc));
      }
    }
  }

  // 2. Load embeddings
  {
    fs::path emb_path = ws / "memory_embeddings.bin";
    if (fs::exists(emb_path))
    {
      std::ifstream in(emb_path, std::ios::binary);
      uint32_t dims = 0;
      uint32_t count = 0;
      in.read(reinterpret_cast<char*>(&dims), sizeof(dims));
      in.read(reinterpret_cast<char*>(&count), sizeof(count));
      dimensions_ = dims;
      embeddings_.clear();
      embeddings_.reserve(count);
      for (uint32_t i = 0; i < count; ++i)
      {
        std::vector<float> emb(dims);
        in.read(reinterpret_cast<char*>(emb.data()),
                static_cast<std::streamsize>(dims * sizeof(float)));
        embeddings_.push_back(std::move(emb));
      }
    }
  }

  // 3. Load mtimes
  {
    fs::path mt_path = ws / "memory_mtimes.json";
    if (fs::exists(mt_path))
    {
      std::ifstream in(mt_path);
      auto j = nlohmann::json::parse(in, nullptr, false);
      if (!j.is_discarded() && j.is_object())
      {
        file_mtimes_.clear();
        for (auto& [key, val] : j.items())
        {
          file_mtimes_[key] = val.get<int64_t>();
        }
      }
    }
  }
}

}  // namespace neamc::vm
