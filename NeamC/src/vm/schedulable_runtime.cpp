//
// Neam v0.8 Phase 6 — Schedulable runtime implementation
//

#include "neamc/vm/schedulable_runtime.hpp"
#include "neamc/vm/vm.hpp"

#include <chrono>

namespace neamc::vm
{

SchedulableRuntime::~SchedulableRuntime()
{
  stop();
}

void SchedulableRuntime::start(VirtualMachine& vm, std::atomic<bool>& draining_flag)
{
  if (running_.load()) return;
  running_ = true;

  // Scan claw_agents for Schedulable trait impls
  for (const auto& [agent_name, claw] : vm.claw_agents())
  {
    if (!vm.has_agent_trait_impl(agent_name, "Schedulable"))
      continue;

    // Get heartbeat_interval from trait method
    int interval_ms = 60000;  // default 60s
    try
    {
      Value self = Value::ClawAgent(claw);
      Value result = vm.invoke_trait_method(agent_name, "heartbeat_interval", self, {});
      if (result.is_number())
        interval_ms = static_cast<int>(result.as_number());
    }
    catch (...)
    {
      // Use default on failure
    }

    contexts_.push_back(HeartbeatContext{agent_name, interval_ms, {}});
    auto& ctx = contexts_.back();

    ctx.thread = std::thread([this, &vm, &draining_flag, &ctx]()
    {
      ++active_count_;
      while (running_.load() && !draining_flag.load())
      {
        // Sleep in 100ms increments for responsiveness
        int slept = 0;
        while (slept < ctx.interval_ms && running_.load() && !draining_flag.load())
        {
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          slept += 100;
        }

        if (!running_.load() || draining_flag.load()) break;

        // Invoke on_heartbeat(self)
        try
        {
          auto it = vm.claw_agents().find(ctx.agent_name);
          if (it != vm.claw_agents().end())
          {
            Value self = Value::ClawAgent(it->second);
            vm.invoke_trait_method(ctx.agent_name, "on_heartbeat", self, {self});
          }
        }
        catch (...)
        {
          // Heartbeat errors are silently swallowed
        }
      }
      --active_count_;
    });
  }
}

void SchedulableRuntime::stop()
{
  if (!running_.load()) return;
  running_ = false;

  for (auto& ctx : contexts_)
  {
    if (ctx.thread.joinable())
      ctx.thread.join();
  }
  contexts_.clear();
}

}  // namespace neamc::vm
