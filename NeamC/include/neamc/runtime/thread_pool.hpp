//
// Neam Runtime - Thread Pool with Work Stealing
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <thread>
#include <vector>

namespace neamc::runtime
{
class WorkStealingQueue
{
public:
  void push(std::function<void()> task)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back(std::move(task));
  }

  std::optional<std::function<void()>> pop()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.empty())
    {
      return std::nullopt;
    }
    auto task = std::move(tasks_.back());
    tasks_.pop_back();
    return task;
  }

  std::optional<std::function<void()>> steal()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (tasks_.empty())
    {
      return std::nullopt;
    }
    auto task = std::move(tasks_.front());
    tasks_.pop_front();
    return task;
  }

  bool empty() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.empty();
  }

  size_t size() const
  {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
  }

private:
  std::deque<std::function<void()>> tasks_{};
  mutable std::mutex mutex_{};
};

struct ThreadPoolConfig
{
  size_t min_threads{1};
  size_t max_threads{std::thread::hardware_concurrency() * 2};
  std::chrono::milliseconds keep_alive{std::chrono::seconds{60}};
  bool enable_work_stealing{true};
};

class ThreadPool
{
public:
  explicit ThreadPool(ThreadPoolConfig config = {}) : config_(config)
  {
    if (config_.min_threads == 0)
    {
      config_.min_threads = 1;
    }
    if (config_.max_threads < config_.min_threads)
    {
      config_.max_threads = config_.min_threads;
    }
    queues_.reserve(config_.max_threads);
    for (size_t i = 0; i < config_.max_threads; ++i)
    {
      queues_.push_back(std::make_unique<WorkStealingQueue>());
    }
    for (size_t i = 0; i < config_.min_threads; ++i)
    {
      spawn_worker(i);
    }
  }

  ~ThreadPool()
  {
    shutdown();
    await_termination();
  }

  void submit(std::function<void()> task)
  {
    if (!running_.load(std::memory_order_relaxed))
    {
      return;
    }
    auto idx = pick_queue();
    queues_[idx]->push(std::move(task));
    pending_.fetch_add(1, std::memory_order_relaxed);
    work_available_.notify_one();
    maybe_spawn_worker();
  }

  void submit_urgent(std::function<void()> task)
  {
    if (!running_.load(std::memory_order_relaxed))
    {
      return;
    }
    auto idx = pick_queue();
    queues_[idx]->push(std::move(task));
    pending_.fetch_add(1, std::memory_order_relaxed);
    work_available_.notify_one();
    maybe_spawn_worker();
  }

  void shutdown()
  {
    running_.store(false, std::memory_order_relaxed);
    work_available_.notify_all();
  }

  void await_termination()
  {
    for (auto& worker : workers_)
    {
      if (worker && worker->joinable())
      {
        worker->join();
      }
    }
  }

  size_t active_threads() const { return active_count_.load(std::memory_order_relaxed); }
  size_t pending_tasks() const { return pending_.load(std::memory_order_relaxed); }

private:
  void spawn_worker(size_t worker_id)
  {
    std::lock_guard<std::mutex> lock(spawn_mutex_);
    if (workers_.size() > worker_id && workers_[worker_id])
    {
      return;
    }
    if (workers_.size() <= worker_id)
    {
      workers_.resize(worker_id + 1);
    }
    workers_[worker_id] = std::make_unique<std::thread>([this, worker_id]() { worker_loop(worker_id); });
  }

  void worker_loop(size_t worker_id)
  {
    active_count_.fetch_add(1, std::memory_order_relaxed);
    while (running_.load(std::memory_order_relaxed))
    {
      std::function<void()> task;
      auto local = queues_[worker_id]->pop();
      if (!local && config_.enable_work_stealing)
      {
        local = steal_task(worker_id);
      }
      if (!local)
      {
        std::unique_lock<std::mutex> lock(wait_mutex_);
        work_available_.wait_for(lock, config_.keep_alive, [this]() {
          return !running_.load(std::memory_order_relaxed) || pending_.load(std::memory_order_relaxed) > 0;
        });
        continue;
      }
      task = std::move(*local);
      pending_.fetch_sub(1, std::memory_order_relaxed);
      if (task)
      {
        task();
      }
    }
    active_count_.fetch_sub(1, std::memory_order_relaxed);
  }

  std::optional<std::function<void()>> steal_task(size_t thief_id)
  {
    if (queues_.empty())
    {
      return std::nullopt;
    }
    std::uniform_int_distribution<size_t> dist(0, queues_.size() - 1);
    for (size_t attempt = 0; attempt < queues_.size(); ++attempt)
    {
      size_t victim = dist(rng_);
      if (victim == thief_id)
      {
        continue;
      }
      auto stolen = queues_[victim]->steal();
      if (stolen)
      {
        return stolen;
      }
    }
    return std::nullopt;
  }

  void maybe_spawn_worker()
  {
    if (active_count_.load(std::memory_order_relaxed) >= config_.max_threads)
    {
      return;
    }
    if (pending_.load(std::memory_order_relaxed) > active_count_.load(std::memory_order_relaxed))
    {
      spawn_worker(active_count_.load(std::memory_order_relaxed));
    }
  }

  size_t pick_queue()
  {
    std::uniform_int_distribution<size_t> dist(0, queues_.size() - 1);
    return dist(rng_);
  }

  ThreadPoolConfig config_{};
  std::vector<std::unique_ptr<std::thread>> workers_{};
  std::vector<std::unique_ptr<WorkStealingQueue>> queues_{};
  std::atomic<bool> running_{true};
  std::atomic<size_t> active_count_{0};
  std::atomic<size_t> pending_{0};
  std::mutex spawn_mutex_{};
  std::mutex wait_mutex_{};
  std::condition_variable work_available_{};

  thread_local static std::mt19937 rng_;
};

inline thread_local std::mt19937 ThreadPool::rng_{std::random_device{}()};
}  // namespace neamc::runtime
