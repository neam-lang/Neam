//
// Neam v0.8 Phase 6 — Schedulable runtime (heartbeat loop manager)
//

#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace neamc::vm
{

class VirtualMachine;

class SchedulableRuntime
{
public:
  SchedulableRuntime() = default;
  ~SchedulableRuntime();

  void start(VirtualMachine& vm, std::atomic<bool>& draining_flag);
  void stop();
  int active_count() const { return active_count_.load(); }

private:
  struct HeartbeatContext
  {
    std::string agent_name;
    int interval_ms{60000};
    std::thread thread;
  };

  std::vector<HeartbeatContext> contexts_;
  std::atomic<bool> running_{false};
  std::atomic<int> active_count_{0};
};

}  // namespace neamc::vm
