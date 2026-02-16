//
// Neam v0.8 Phase 7 — Memory Index
// Filesystem-based vector + keyword search over workspace documents
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace neamc::vm
{

struct MemoryChunk
{
  std::string file_path;  // relative to workspace
  std::string text;
  int chunk_index{0};
};

struct MemorySearchResult
{
  std::string file_path;
  std::string chunk;
  float score{0.0f};
};

class MemoryIndex
{
public:
  MemoryIndex(const std::string& workspace_dir, const std::string& search_mode,
              std::size_t dimensions = 64);

  void index_workspace();
  void index_document(const std::string& rel_path, const std::string& content);
  void reindex_changed();
  std::vector<MemorySearchResult> search(const std::string& query, std::size_t top_k) const;
  void save() const;
  void load();

private:
  std::vector<std::string> chunk_text(const std::string& text, std::size_t chunk_size = 400,
                                      std::size_t overlap = 80) const;
  std::vector<MemorySearchResult> vector_search(const std::string& query,
                                                std::size_t top_k) const;
  std::vector<MemorySearchResult> keyword_search(const std::string& query,
                                                 std::size_t top_k) const;
  std::vector<MemorySearchResult> hybrid_search(const std::string& query,
                                                std::size_t top_k) const;

  std::string workspace_dir_;
  std::string search_mode_;  // "vector", "keyword", "hybrid"
  std::size_t dimensions_;

  std::vector<MemoryChunk> chunks_;
  std::vector<std::vector<float>> embeddings_;  // parallel to chunks_
  std::unordered_map<std::string, int64_t> file_mtimes_;  // rel_path -> mtime_ms
};

}  // namespace neamc::vm
