//
// Neam v0.8 Phase 6 — Lane queue engine for concurrent task scheduling
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace neamc::vm
{

struct LaneTask
{
  std::string session_key;
  std::function<void()> work;
  int priority{0};
};

class LaneQueueEngine
{
public:
  LaneQueueEngine() = default;
  ~LaneQueueEngine();

  void configure_lane(const std::string& name, int concurrency, int priority = 0);
  void enqueue(const std::string& lane, LaneTask task);
  void start();
  void stop();
  std::unordered_map<std::string, std::size_t> pending_counts() const;

private:
  struct Lane
  {
    std::string name;
    int max_concurrency{1};
    int priority{0};
    std::atomic<int> active_count{0};
    std::queue<LaneTask> pending;
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool stopped{false};
  };

  void lane_worker(Lane& lane);

  std::unordered_map<std::string, std::unique_ptr<Lane>> lanes_;
  std::vector<std::thread> workers_;
  bool started_{false};
};

}  // namespace neamc::vm
