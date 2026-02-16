//
// Neam v0.8 Phase 6 — Lane queue engine implementation
//

#include "neamc/vm/lane_queue.hpp"

namespace neamc::vm
{

LaneQueueEngine::~LaneQueueEngine()
{
  stop();
}

void LaneQueueEngine::configure_lane(const std::string& name, int concurrency, int priority)
{
  auto lane = std::make_unique<Lane>();
  lane->name = name;
  lane->max_concurrency = concurrency > 0 ? concurrency : 1;
  lane->priority = priority;
  lanes_[name] = std::move(lane);
}

void LaneQueueEngine::enqueue(const std::string& lane_name, LaneTask task)
{
  auto it = lanes_.find(lane_name);
  if (it == lanes_.end()) return;

  auto& lane = *it->second;
  {
    std::lock_guard<std::mutex> lock(lane.mutex);
    lane.pending.push(std::move(task));
  }
  lane.cv.notify_one();
}

void LaneQueueEngine::start()
{
  if (started_) return;
  started_ = true;

  for (auto& [name, lane] : lanes_)
  {
    lane->stopped = false;
    for (int i = 0; i < lane->max_concurrency; ++i)
    {
      workers_.emplace_back([this, &l = *lane]() { lane_worker(l); });
    }
  }
}

void LaneQueueEngine::stop()
{
  if (!started_) return;

  for (auto& [name, lane] : lanes_)
  {
    {
      std::lock_guard<std::mutex> lock(lane->mutex);
      lane->stopped = true;
    }
    lane->cv.notify_all();
  }

  for (auto& worker : workers_)
  {
    if (worker.joinable())
      worker.join();
  }
  workers_.clear();
  started_ = false;
}

std::unordered_map<std::string, std::size_t> LaneQueueEngine::pending_counts() const
{
  std::unordered_map<std::string, std::size_t> counts;
  for (const auto& [name, lane] : lanes_)
  {
    std::lock_guard<std::mutex> lock(lane->mutex);
    counts[name] = lane->pending.size();
  }
  return counts;
}

void LaneQueueEngine::lane_worker(Lane& lane)
{
  while (true)
  {
    LaneTask task;
    {
      std::unique_lock<std::mutex> lock(lane.mutex);
      lane.cv.wait(lock, [&] { return !lane.pending.empty() || lane.stopped; });

      if (lane.stopped && lane.pending.empty())
        return;

      task = std::move(lane.pending.front());
      lane.pending.pop();
    }

    ++lane.active_count;
    try
    {
      if (task.work) task.work();
    }
    catch (...)
    {
      // Task exceptions are silently caught; logging can be added later
    }
    --lane.active_count;
  }
}

}  // namespace neamc::vm
