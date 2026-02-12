//
// Neam Virtual Machine - VM Pool (v0.7.2 HotVM)
//
// Reusable pool of VirtualMachine instances. Eliminates per-request VM
// construction overhead (~92ms) by recycling VMs via reset_for_reuse().
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <queue>
#include <vector>

#include "neamc/vm/vm.hpp"

namespace neamc::vm
{
class VMPool;

// RAII handle — returns VM to pool on destruction
class PooledVM
{
public:
  PooledVM() : vm_(nullptr), pool_(nullptr) {}
  PooledVM(VirtualMachine* vm, VMPool* pool);
  ~PooledVM();

  // Move-only
  PooledVM(PooledVM&& other) noexcept;
  PooledVM& operator=(PooledVM&& other) noexcept;
  PooledVM(const PooledVM&) = delete;
  PooledVM& operator=(const PooledVM&) = delete;

  VirtualMachine* operator->() { return vm_; }
  const VirtualMachine* operator->() const { return vm_; }
  VirtualMachine* get() { return vm_; }
  const VirtualMachine* get() const { return vm_; }
  explicit operator bool() const { return vm_ != nullptr; }

private:
  VirtualMachine* vm_;
  VMPool* pool_;
};

class VMPool
{
public:
  struct Config
  {
    size_t min_vms{2};       // Pre-create at startup
    size_t max_vms{16};      // Hard ceiling
    long timeout_ms{5000};   // Acquire timeout
    ResetPolicy reset_policy{ResetPolicy::Standard};  // v0.7.2: VM reset level
  };

  struct Stats
  {
    size_t total{0};
    size_t available{0};
    size_t in_use{0};
    size_t peak{0};
  };

  explicit VMPool(Config config);
  ~VMPool();

  // Get a VM from the pool (blocks if exhausted, returns empty PooledVM on timeout)
  PooledVM acquire();

  // Return a VM to the pool (called by PooledVM destructor)
  void release(VirtualMachine* vm);

  // Pre-create VMs at startup
  void warm(size_t count);

  // Pool statistics
  Stats stats() const;

private:
  VirtualMachine* create_vm();

  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::queue<VirtualMachine*> idle_;
  std::vector<VirtualMachine*> all_;  // Owns all VMs for cleanup
  std::atomic<size_t> total_{0};
  std::atomic<size_t> in_use_{0};
  std::atomic<size_t> peak_{0};
  Config config_;
};
}  // namespace neamc::vm
