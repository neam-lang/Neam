//
// v0.7.2 HotVM Memory Benchmark — Google Benchmark tests for VM pool, cache, GC
//

#include <benchmark/benchmark.h>

#include "neamc/pipeline.hpp"
#include "neamc/vm/bytecode_cache.hpp"
#include "neamc/vm/metrics.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/vm_pool.hpp"

namespace
{

const std::string kSimpleProgram = R"(
agent TestAgent {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a test agent."
}
{
  emit "hello";
}
)";

// Benchmark: VM pool acquire + release cycle
static void BM_VMPoolAcquireRelease(benchmark::State& state)
{
  neamc::vm::VMPool::Config cfg;
  cfg.min_vms = 4;
  cfg.max_vms = 8;
  cfg.reset_policy = neamc::vm::ResetPolicy::Standard;
  neamc::vm::VMPool pool(cfg);
  pool.warm(4);

  for (auto _ : state)
  {
    auto vm = pool.acquire();
    benchmark::DoNotOptimize(vm.get());
    // PooledVM destructor returns to pool
  }
}
BENCHMARK(BM_VMPoolAcquireRelease);

// Benchmark: VM pool with Minimal reset
static void BM_VMPoolMinimalReset(benchmark::State& state)
{
  neamc::vm::VMPool::Config cfg;
  cfg.min_vms = 4;
  cfg.max_vms = 8;
  cfg.reset_policy = neamc::vm::ResetPolicy::Minimal;
  neamc::vm::VMPool pool(cfg);
  pool.warm(4);

  for (auto _ : state)
  {
    auto vm = pool.acquire();
    benchmark::DoNotOptimize(vm.get());
  }
}
BENCHMARK(BM_VMPoolMinimalReset);

// Benchmark: Bytecode cache hit (read path)
static void BM_BytecodeCacheHit(benchmark::State& state)
{
  neamc::vm::BytecodeCache cache;
  cache.preload("test", kSimpleProgram);

  for (auto _ : state)
  {
    const auto* bc = cache.get("test");
    benchmark::DoNotOptimize(bc);
  }
}
BENCHMARK(BM_BytecodeCacheHit);

// Benchmark: Bytecode cache miss
static void BM_BytecodeCacheMiss(benchmark::State& state)
{
  neamc::vm::BytecodeCache cache;
  cache.preload("test", kSimpleProgram);

  for (auto _ : state)
  {
    const auto* bc = cache.get("nonexistent");
    benchmark::DoNotOptimize(bc);
  }
}
BENCHMARK(BM_BytecodeCacheMiss);

// Benchmark: RequestMetrics record throughput
static void BM_MetricsRecord(benchmark::State& state)
{
  neamc::vm::RequestMetrics metrics;

  neamc::vm::RequestTiming timing;
  timing.total_us = 3200;
  timing.warm = true;
  timing.agent_id = "test";

  for (auto _ : state)
  {
    metrics.record(timing);
  }
}
BENCHMARK(BM_MetricsRecord);

// Benchmark: RequestMetrics compute percentiles
static void BM_MetricsCompute(benchmark::State& state)
{
  neamc::vm::RequestMetrics metrics;

  // Pre-fill with data
  neamc::vm::RequestTiming timing;
  timing.warm = true;
  timing.agent_id = "test";
  for (int i = 0; i < 5000; ++i)
  {
    timing.total_us = 1000 + (i % 100) * 50;
    metrics.record(timing);
  }

  for (auto _ : state)
  {
    auto snap = metrics.compute();
    benchmark::DoNotOptimize(snap);
  }
}
BENCHMARK(BM_MetricsCompute);

// Benchmark: VM construction cost (baseline)
static void BM_VMConstruction(benchmark::State& state)
{
  for (auto _ : state)
  {
    auto* vm = new neamc::vm::VirtualMachine();
    benchmark::DoNotOptimize(vm);
    delete vm;
  }
}
BENCHMARK(BM_VMConstruction);

// Benchmark: reset_for_reuse at each level
static void BM_ResetMinimal(benchmark::State& state)
{
  neamc::vm::VirtualMachine vm;
  for (auto _ : state)
  {
    vm.reset_for_reuse(neamc::vm::ResetPolicy::Minimal);
  }
}
BENCHMARK(BM_ResetMinimal);

static void BM_ResetStandard(benchmark::State& state)
{
  neamc::vm::VirtualMachine vm;
  for (auto _ : state)
  {
    vm.reset_for_reuse(neamc::vm::ResetPolicy::Standard);
  }
}
BENCHMARK(BM_ResetStandard);

static void BM_ResetFull(benchmark::State& state)
{
  neamc::vm::VirtualMachine vm;
  for (auto _ : state)
  {
    vm.reset_for_reuse(neamc::vm::ResetPolicy::Full);
  }
}
BENCHMARK(BM_ResetFull);

static void BM_ResetComplete(benchmark::State& state)
{
  neamc::vm::VirtualMachine vm;
  for (auto _ : state)
  {
    vm.reset_for_reuse(neamc::vm::ResetPolicy::Complete);
  }
}
BENCHMARK(BM_ResetComplete);

}  // namespace

BENCHMARK_MAIN();
