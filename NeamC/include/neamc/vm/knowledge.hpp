//
// Neam Virtual Machine - Knowledge (RAG) subsystem
//

#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace neamc::vm::knowledge
{
struct Source
{
  std::string type;
  std::string path;
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

class VectorStore
{
public:
  explicit VectorStore(std::size_t dimensions = 8);
  std::size_t dimensions() const { return dimensions_; }
  std::size_t size() const { return entries_.size(); }

  void add(const std::vector<float>& embedding, Chunk chunk);
  std::vector<SearchResult> search(const std::vector<float>& embedding, std::size_t top_k) const;

private:
  struct Entry
  {
    std::vector<float> embedding;
    Chunk chunk;
  };

  std::size_t dimensions_{8};
  std::vector<Entry> entries_{};
};

std::vector<float> embed_text(const std::string& text, std::size_t dimensions);

class Ingester
{
public:
  Ingester(VectorStore& store, std::size_t chunk_size, std::size_t chunk_overlap,
           std::string embedding_model);
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
};
}  // namespace neamc::vm::knowledge
