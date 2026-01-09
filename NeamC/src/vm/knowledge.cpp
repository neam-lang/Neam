//
// Neam Virtual Machine - Knowledge (RAG) subsystem
//

#include "neamc/vm/knowledge.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include <curl/curl.h>

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

VectorStore::VectorStore(std::size_t dimensions) : dimensions_(dimensions) {}

void VectorStore::add(const std::vector<float>& embedding, Chunk chunk)
{
  if (embedding.empty())
  {
    return;
  }
  entries_.push_back(Entry{normalize(embedding), std::move(chunk)});
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

std::vector<float> embed_text(const std::string& text, std::size_t dimensions)
{
  std::vector<float> embedding(dimensions, 0.0f);
  if (dimensions == 0)
  {
    return embedding;
  }
  std::hash<std::string> hasher;
  auto words = split_words(text);
  for (const auto& word : words)
  {
    const std::size_t hash = hasher(word);
    const std::size_t index = hash % dimensions;
    embedding[index] += 1.0f;
  }
  return normalize(embedding);
}

Ingester::Ingester(VectorStore& store, std::size_t chunk_size, std::size_t chunk_overlap,
                   std::string embedding_model)
    : store_(store),
      chunk_size_(chunk_size),
      chunk_overlap_(chunk_overlap),
      embedding_model_(std::move(embedding_model))
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
    auto embedding = embed_text(chunk, store_.dimensions());
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
