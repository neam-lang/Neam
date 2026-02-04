// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Event loop implementation
//

#include "neamc/voice/event_loop.hpp"

#include <chrono>

namespace neamc::voice
{
VoiceEventLoop::VoiceEventLoop() = default;
VoiceEventLoop::~VoiceEventLoop()
{
  stop();
}

void VoiceEventLoop::push(VoiceEvent event)
{
  {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(std::move(event));
  }
  cv_.notify_one();
}

int VoiceEventLoop::poll(const VoiceEventHandler& handler)
{
  std::queue<VoiceEvent> batch;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    batch.swap(queue_);
  }

  int count = 0;
  while (!batch.empty())
  {
    handler(batch.front());
    batch.pop();
    ++count;
  }
  return count;
}

int VoiceEventLoop::poll_blocking(const VoiceEventHandler& handler)
{
  std::queue<VoiceEvent> batch;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !queue_.empty() || !running_.load(); });
    if (queue_.empty())
    {
      return 0;
    }
    batch.swap(queue_);
  }

  int count = 0;
  while (!batch.empty())
  {
    handler(batch.front());
    batch.pop();
    ++count;
  }
  return count;
}

int VoiceEventLoop::poll_timeout(const VoiceEventHandler& handler,
                                  int timeout_ms)
{
  std::queue<VoiceEvent> batch;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                 [this] { return !queue_.empty() || !running_.load(); });
    if (queue_.empty())
    {
      return 0;
    }
    batch.swap(queue_);
  }

  int count = 0;
  while (!batch.empty())
  {
    handler(batch.front());
    batch.pop();
    ++count;
  }
  return count;
}

void VoiceEventLoop::run(const VoiceEventHandler& handler)
{
  running_.store(true);
  while (running_.load())
  {
    poll_blocking(handler);
  }
  // Drain remaining events
  poll(handler);
}

void VoiceEventLoop::stop()
{
  running_.store(false);
  cv_.notify_all();
}

std::size_t VoiceEventLoop::pending() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  return queue_.size();
}
}  // namespace neamc::voice
