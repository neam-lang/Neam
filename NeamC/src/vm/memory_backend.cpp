// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Memory backend implementation
//

#include "neamc/vm/memory_backend.hpp"

#include <algorithm>

namespace neamc::vm
{
void InMemoryBackend::store_event(const std::string& store_name, const MemoryEventRecord& event)
{
  stores_[store_name].events.push_back(event);
}

std::vector<MemoryEventRecord> InMemoryBackend::load_events(const std::string& store_name)
{
  auto it = stores_.find(store_name);
  if (it == stores_.end())
  {
    return {};
  }
  return it->second.events;
}

void InMemoryBackend::save_checkpoint(const std::string& store_name, const std::string& label,
                                       std::size_t position)
{
  stores_[store_name].checkpoints[label] = position;
}

std::optional<std::size_t> InMemoryBackend::load_checkpoint(const std::string& store_name,
                                                             const std::string& label)
{
  auto store_it = stores_.find(store_name);
  if (store_it == stores_.end())
  {
    return std::nullopt;
  }
  auto cp_it = store_it->second.checkpoints.find(label);
  if (cp_it == store_it->second.checkpoints.end())
  {
    return std::nullopt;
  }
  return cp_it->second;
}

std::vector<MemoryEventRecord> InMemoryBackend::search_events(const std::string& store_name,
                                                               const std::string& query,
                                                               std::size_t limit)
{
  auto store_it = stores_.find(store_name);
  if (store_it == stores_.end())
  {
    return {};
  }
  std::vector<MemoryEventRecord> results;
  for (const auto& event : store_it->second.events)
  {
    if (event.data.find(query) != std::string::npos)
    {
      results.push_back(event);
      if (results.size() >= limit)
      {
        break;
      }
    }
  }
  return results;
}

void InMemoryBackend::clear(const std::string& store_name)
{
  stores_.erase(store_name);
}

// Forward declaration - implemented in sqlite_memory.cpp
std::unique_ptr<MemoryBackend> create_sqlite_backend(const std::string& path);

std::unique_ptr<MemoryBackend> create_memory_backend(const std::string& backend_type,
                                                      const std::string& path)
{
  if (backend_type == "sqlite")
  {
    return create_sqlite_backend(path);
  }
  return std::make_unique<InMemoryBackend>();
}
}  // namespace neamc::vm
