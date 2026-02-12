//
// Neam Virtual Machine - VM Pool (v0.7.2 HotVM)
//

#include "neamc/vm/vm_pool.hpp"

#include "neamc/vm/memory.hpp"

namespace neamc::vm
{
// --- PooledVM ---

PooledVM::PooledVM(VirtualMachine* vm, VMPool* pool)
    : vm_(vm), pool_(pool)
{
}

PooledVM::~PooledVM()
{
  if (vm_ && pool_)
  {
    pool_->release(vm_);
  }
}

PooledVM::PooledVM(PooledVM&& other) noexcept
    : vm_(other.vm_), pool_(other.pool_)
{
  other.vm_ = nullptr;
  other.pool_ = nullptr;
}

PooledVM& PooledVM::operator=(PooledVM&& other) noexcept
{
  if (this != &other)
  {
    if (vm_ && pool_)
    {
      pool_->release(vm_);
    }
    vm_ = other.vm_;
    pool_ = other.pool_;
    other.vm_ = nullptr;
    other.pool_ = nullptr;
  }
  return *this;
}

// --- VMPool ---

VMPool::VMPool(Config config) : config_(config)
{
}

VMPool::~VMPool()
{
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto* vm : all_)
  {
    delete vm;
  }
  all_.clear();
  while (!idle_.empty())
  {
    idle_.pop();
  }
}

VirtualMachine* VMPool::create_vm()
{
  auto* vm = new VirtualMachine();
  all_.push_back(vm);
  ++total_;
  return vm;
}

PooledVM VMPool::acquire()
{
  std::unique_lock<std::mutex> lock(mutex_);

  // Try to get an idle VM
  if (!idle_.empty())
  {
    auto* vm = idle_.front();
    idle_.pop();
    ++in_use_;
    size_t current = in_use_.load();
    size_t peak = peak_.load();
    while (current > peak && !peak_.compare_exchange_weak(peak, current))
    {
    }
    lock.unlock();
    set_current_vm(vm);
    return PooledVM(vm, this);
  }

  // Create new VM if under limit
  if (total_.load() < config_.max_vms)
  {
    auto* vm = create_vm();
    ++in_use_;
    size_t current = in_use_.load();
    size_t peak = peak_.load();
    while (current > peak && !peak_.compare_exchange_weak(peak, current))
    {
    }
    lock.unlock();
    set_current_vm(vm);
    return PooledVM(vm, this);
  }

  // Wait for a VM to become available
  auto deadline = std::chrono::steady_clock::now() +
                  std::chrono::milliseconds(config_.timeout_ms);
  bool got_vm = cv_.wait_until(lock, deadline, [this] { return !idle_.empty(); });

  if (!got_vm)
  {
    return PooledVM();  // Timeout — empty handle
  }

  auto* vm = idle_.front();
  idle_.pop();
  ++in_use_;
  size_t current = in_use_.load();
  size_t peak = peak_.load();
  while (current > peak && !peak_.compare_exchange_weak(peak, current))
  {
  }
  lock.unlock();
  set_current_vm(vm);
  return PooledVM(vm, this);
}

void VMPool::release(VirtualMachine* vm)
{
  if (!vm) return;

  vm->reset_for_reuse(config_.reset_policy);

  {
    std::lock_guard<std::mutex> lock(mutex_);
    idle_.push(vm);
    --in_use_;
  }
  set_current_vm(nullptr);
  cv_.notify_one();
}

void VMPool::warm(size_t count)
{
  std::lock_guard<std::mutex> lock(mutex_);
  size_t to_create = std::min(count, config_.max_vms - total_.load());
  for (size_t i = 0; i < to_create; ++i)
  {
    auto* vm = create_vm();
    set_current_vm(nullptr);  // Don't leave warmed VM as current
    idle_.push(vm);
  }
}

VMPool::Stats VMPool::stats() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return {total_.load(), idle_.size(), in_use_.load(), peak_.load()};
}
}  // namespace neamc::vm
