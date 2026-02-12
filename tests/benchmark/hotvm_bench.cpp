//
// HotVM Benchmark (v0.7.2)
//
// Measures cold-path vs warm-path overhead to verify HotVM
// achieves <10ms warm request overhead.
//
// Usage: ./hotvm_bench
//

#include <chrono>
#include <iostream>
#include <sstream>
#include <vector>

#include "neamc/pipeline.hpp"
#include "neamc/vm/bytecode_cache.hpp"
#include "neamc/vm/memory.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/vm_pool.hpp"

namespace
{
// Simple agent template that emits a string (no LLM call)
const char* kTestProgram = R"(
{
  let msg = "Hello from HotVM";
  emit msg;
}
)";

// Agent template with __query__ injection
const char* kQueryProgram = R"(
{
  let msg = __query__;
  emit msg;
}
)";

struct BenchResult
{
  std::string name;
  double min_us;
  double avg_us;
  double max_us;
  int iterations;
};

using Clock = std::chrono::high_resolution_clock;

double to_us(std::chrono::nanoseconds ns)
{
  return static_cast<double>(ns.count()) / 1000.0;
}

BenchResult run_bench(const std::string& name, int iterations, std::function<void()> fn)
{
  // Warmup
  for (int i = 0; i < 3; ++i) fn();

  double min_us = 1e12, max_us = 0, total_us = 0;
  for (int i = 0; i < iterations; ++i)
  {
    auto start = Clock::now();
    fn();
    auto elapsed = Clock::now() - start;
    double us = to_us(elapsed);
    min_us = std::min(min_us, us);
    max_us = std::max(max_us, us);
    total_us += us;
  }
  return {name, min_us, total_us / iterations, max_us, iterations};
}

void print_result(const BenchResult& r)
{
  std::cout << "  " << r.name << ":\n"
            << "    min=" << r.min_us << "us  avg=" << r.avg_us
            << "us  max=" << r.max_us << "us  (n=" << r.iterations << ")\n";
}
}  // namespace

int main()
{
  std::cout << "=== HotVM Benchmark (v0.7.2) ===\n\n";

  const int N = 100;

  // Benchmark 1: Cold path — compile + new VM + run
  auto cold = run_bench("Cold path (compile + new VM + run)", N, [] {
    neamc::Pipeline pipeline;
    auto unit = pipeline.compile(kTestProgram, {});

    neamc::vm::VirtualMachine vm;
    std::ostringstream out;
    std::istringstream in;
    vm.set_io(&in, &out);
    vm.run(unit.chunk);
  });

  // Benchmark 2: Cache lookup overhead alone
  neamc::vm::BytecodeCache cache;
  cache.preload("test", kTestProgram);

  auto cache_lookup = run_bench("Cache lookup", N * 100, [&cache] {
    auto* bc = cache.get("test");
    (void)bc;
  });

  // Benchmark 3: Pool acquire/release overhead alone
  neamc::vm::VMPool::Config pool_cfg;
  pool_cfg.min_vms = 4;
  pool_cfg.max_vms = 8;
  pool_cfg.timeout_ms = 5000;
  neamc::vm::VMPool pool(pool_cfg);
  pool.warm(4);

  auto pool_cycle = run_bench("Pool acquire+release", N, [&pool] {
    auto vm = pool.acquire();
    // PooledVM dtor releases
  });

  // Benchmark 4: Warm path — cache.get + pool.acquire + inject query + run
  cache.preload("query", kQueryProgram);

  auto warm = run_bench("Warm path (cache + pool + inject + run)", N, [&cache, &pool] {
    auto vm = pool.acquire();
    auto* key_str = neamc::vm::copy_string("__query__", 9);
    vm->globals().set(key_str, neamc::vm::Value::String("Hello world", 11));

    const auto* bytecode = cache.get("query");
    std::ostringstream out;
    std::istringstream in;
    vm->set_io(&in, &out);
    vm->run(*bytecode);
  });

  // Print results
  std::cout << "\nResults (" << N << " iterations):\n";
  print_result(cold);
  print_result(cache_lookup);
  print_result(pool_cycle);
  print_result(warm);

  double speedup = cold.avg_us / warm.avg_us;
  std::cout << "\nSpeedup: " << speedup << "x (cold/warm)\n";

  if (warm.avg_us < 10000.0)  // < 10ms
  {
    std::cout << "PASS: Warm path avg " << warm.avg_us / 1000.0 << "ms < 10ms target\n";
    return 0;
  }
  else
  {
    std::cout << "WARN: Warm path avg " << warm.avg_us / 1000.0 << "ms >= 10ms target\n";
    return 1;
  }
}
