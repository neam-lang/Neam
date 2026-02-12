//
// Neam Virtual Machine - Bytecode Cache (v0.7.2 HotVM)
//
// Thread-safe cache for pre-compiled bytecode. Eliminates per-request
// compilation overhead by caching Pipeline::compile() results keyed by
// agent ID or source hash.
//

#pragma once

#include <cstdint>
#include <shared_mutex>
#include <string>
#include <unordered_map>

#include "neamc/vm/bytecode.hpp"

namespace neamc::vm
{
class BytecodeCache
{
public:
  struct Stats
  {
    uint64_t hits{0};
    uint64_t misses{0};
    size_t entries{0};
  };

  // Pre-compile and cache a .neam source under a key (agent_id)
  void preload(const std::string& key, const std::string& source);

  // Get cached bytecode (returns nullptr if not found)
  const Bytecode* get(const std::string& key) const;

  // Get or compile on cache miss (thread-safe)
  const Bytecode& get_or_compile(const std::string& key, const std::string& source);

  // v0.7.2: Pre-compile from a .neam file on disk
  void precompile_file(const std::string& key, const std::string& path);

  // v0.7.2: Insert pre-deserialized bytecode (for AOT loading)
  void load_bytecode(const std::string& key, Bytecode&& bytecode);

  // Cache statistics
  Stats stats() const;

private:
  mutable std::shared_mutex mutex_;
  std::unordered_map<std::string, Bytecode> cache_;
  mutable std::atomic<uint64_t> hits_{0};
  mutable std::atomic<uint64_t> misses_{0};
};
}  // namespace neamc::vm
