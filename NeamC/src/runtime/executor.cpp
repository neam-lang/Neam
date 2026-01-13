//
// Neam Runtime - Executor implementation
//

#include "neamc/runtime/executor.hpp"

namespace neamc::runtime
{
namespace
{
std::unique_ptr<Executor> global_instance;
std::mutex global_mutex;
}  // namespace

Executor::Executor(ExecutorConfig config)
    : config_(config),
      thread_pool_(ThreadPoolConfig{config.thread_count == 0 ? 1 : config.thread_count,
                                    config.thread_count == 0 ? 1 : config.thread_count,
                                    config.idle_timeout,
                                    config.enable_work_stealing})
{
  if (config_.thread_count == 0)
  {
    config_.thread_count = 1;
  }
}

Executor::~Executor()
{
  shutdown();
}

bool Executor::schedule_task(const std::shared_ptr<TaskBase>& task)
{
  if (!running_.load(std::memory_order_relaxed))
  {
    return false;
  }
  if (pending_.load(std::memory_order_relaxed) >= config_.max_queue_size)
  {
    return false;
  }
  pending_.fetch_add(1, std::memory_order_relaxed);
  thread_pool_.submit([this, task]() {
    task->execute();
    pending_.fetch_sub(1, std::memory_order_relaxed);
    if (config_.enable_metrics)
    {
      std::lock_guard<std::mutex> lock(metrics_mutex_);
      metrics_.push_back(task->metrics);
    }
  });
  return true;
}

void Executor::shutdown()
{
  running_.store(false, std::memory_order_relaxed);
  thread_pool_.shutdown();
  thread_pool_.await_termination();
}

void Executor::shutdown_now()
{
  shutdown();
}

bool Executor::is_running() const
{
  return running_.load(std::memory_order_relaxed);
}

size_t Executor::pending_tasks() const
{
  return pending_.load(std::memory_order_relaxed);
}

size_t Executor::active_workers() const
{
  return thread_pool_.active_threads();
}

std::vector<TaskMetrics> Executor::get_metrics() const
{
  std::lock_guard<std::mutex> lock(metrics_mutex_);
  return metrics_;
}

Executor& global_executor()
{
  std::lock_guard<std::mutex> lock(global_mutex);
  if (!global_instance)
  {
    global_instance = std::make_unique<Executor>();
  }
  return *global_instance;
}

void set_global_executor(std::unique_ptr<Executor> executor)
{
  std::lock_guard<std::mutex> lock(global_mutex);
  global_instance = std::move(executor);
}
}  // namespace neamc::runtime
