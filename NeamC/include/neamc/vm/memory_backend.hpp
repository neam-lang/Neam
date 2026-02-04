// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Memory backend interface
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace neamc::vm
{
struct MemoryEventRecord
{
  int64_t timestamp{0};
  std::string type;
  std::string data;
  std::string agent;
};

class MemoryBackend
{
public:
  virtual ~MemoryBackend() = default;
  virtual void store_event(const std::string& store_name, const MemoryEventRecord& event) = 0;
  virtual std::vector<MemoryEventRecord> load_events(const std::string& store_name) = 0;
  virtual void save_checkpoint(const std::string& store_name, const std::string& label,
                               std::size_t position) = 0;
  virtual std::optional<std::size_t> load_checkpoint(const std::string& store_name,
                                                      const std::string& label) = 0;
  virtual std::vector<MemoryEventRecord> search_events(const std::string& store_name,
                                                        const std::string& query,
                                                        std::size_t limit = 10) = 0;
  virtual void clear(const std::string& store_name) = 0;
};

class InMemoryBackend final : public MemoryBackend
{
public:
  void store_event(const std::string& store_name, const MemoryEventRecord& event) override;
  std::vector<MemoryEventRecord> load_events(const std::string& store_name) override;
  void save_checkpoint(const std::string& store_name, const std::string& label,
                       std::size_t position) override;
  std::optional<std::size_t> load_checkpoint(const std::string& store_name,
                                              const std::string& label) override;
  std::vector<MemoryEventRecord> search_events(const std::string& store_name,
                                                const std::string& query,
                                                std::size_t limit = 10) override;
  void clear(const std::string& store_name) override;

private:
  struct Store
  {
    std::vector<MemoryEventRecord> events;
    std::unordered_map<std::string, std::size_t> checkpoints;
  };
  std::unordered_map<std::string, Store> stores_;
};

std::unique_ptr<MemoryBackend> create_memory_backend(const std::string& backend_type,
                                                      const std::string& path = "");
}  // namespace neamc::vm
