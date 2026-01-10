#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"

#include <benchmark/benchmark.h>

static void BM_VMArithmetic(benchmark::State& state)
{
  neamc::Pipeline pipeline;
  auto unit = pipeline.compile("return 1 + 2 + 3 + 4;", {});
  neamc::vm::VirtualMachine vm;
  for (auto _ : state)
  {
    auto result = vm.run(unit.chunk);
    benchmark::DoNotOptimize(result);
  }
}

BENCHMARK(BM_VMArithmetic);

BENCHMARK_MAIN();
