//
// Neam Virtual Machine - Bytecode Cache (v0.7.2 HotVM)
//

#include "neamc/vm/bytecode_cache.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include "neamc/pipeline.hpp"

namespace neamc::vm
{
void BytecodeCache::preload(const std::string& key, const std::string& source)
{
  neamc::Pipeline pipeline;
  auto unit = pipeline.compile(source, {});

  std::unique_lock lock(mutex_);
  cache_.emplace(key, std::move(unit.chunk));
}

const Bytecode* BytecodeCache::get(const std::string& key) const
{
  std::shared_lock lock(mutex_);
  auto it = cache_.find(key);
  if (it != cache_.end())
  {
    ++hits_;
    return &it->second;
  }
  ++misses_;
  return nullptr;
}

const Bytecode& BytecodeCache::get_or_compile(const std::string& key, const std::string& source)
{
  // Fast path: shared read lock
  {
    std::shared_lock lock(mutex_);
    auto it = cache_.find(key);
    if (it != cache_.end())
    {
      ++hits_;
      return it->second;
    }
  }

  // Slow path: compile and insert under exclusive lock
  ++misses_;
  neamc::Pipeline pipeline;
  auto unit = pipeline.compile(source, {});

  std::unique_lock lock(mutex_);
  // Double-check — another thread may have inserted while we compiled
  auto it = cache_.find(key);
  if (it != cache_.end())
  {
    return it->second;
  }
  auto [inserted, _] = cache_.emplace(key, std::move(unit.chunk));
  return inserted->second;
}

void BytecodeCache::precompile_file(const std::string& key, const std::string& path)
{
  std::ifstream file(path);
  if (!file)
  {
    throw std::runtime_error("BytecodeCache: cannot open file: " + path);
  }
  std::ostringstream buf;
  buf << file.rdbuf();
  preload(key, buf.str());
}

void BytecodeCache::load_bytecode(const std::string& key, Bytecode&& bytecode)
{
  std::unique_lock lock(mutex_);
  cache_.emplace(key, std::move(bytecode));
}

BytecodeCache::Stats BytecodeCache::stats() const
{
  std::shared_lock lock(mutex_);
  return {hits_.load(), misses_.load(), cache_.size()};
}
}  // namespace neamc::vm
