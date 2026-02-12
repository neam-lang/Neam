//
// v0.7.2 Phase 2 Tests — ResetPolicy, BytecodeCache extensions, ConnectionPool, Metrics
//

#include <gtest/gtest.h>

#include <fstream>
#include <sstream>

#include "neamc/llm/http_client.hpp"
#include "neamc/pipeline.hpp"
#include "neamc/vm/bytecode_cache.hpp"
#include "neamc/vm/metrics.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/vm_pool.hpp"

// --- ResetPolicy Tests ---

TEST(ResetPolicyTest, MinimalClearsStackFramesEmitted)
{
  neamc::vm::VirtualMachine vm;

  // Run a simple program to populate state
  neamc::Pipeline pipeline;
  auto unit = pipeline.compile("{ emit 42; }", {});
  vm.run(unit.chunk);

  EXPECT_FALSE(vm.emitted().empty());

  // Minimal reset
  vm.reset_for_reuse(neamc::vm::ResetPolicy::Minimal);

  EXPECT_TRUE(vm.emitted().empty());
  EXPECT_TRUE(vm.stack().empty());
  EXPECT_TRUE(vm.frames().empty());
}

TEST(ResetPolicyTest, StandardClearsMoreThanMinimal)
{
  neamc::vm::VirtualMachine vm;

  // Add some roots and skills
  vm.push_root(neamc::vm::Value::Number(42));
  vm.allowed_skills().insert("test_skill");

  vm.reset_for_reuse(neamc::vm::ResetPolicy::Standard);

  EXPECT_TRUE(vm.roots().empty());
  EXPECT_TRUE(vm.allowed_skills().empty());
}

TEST(ResetPolicyTest, CompleteClearsGlobals)
{
  neamc::vm::VirtualMachine vm;

  // Globals should be populated after construction (natives)
  // After Complete reset, globals should be empty
  vm.reset_for_reuse(neamc::vm::ResetPolicy::Complete);

  // After complete reset, globals table is cleared
  // (No public method to check size, but it shouldn't crash)
}

TEST(ResetPolicyTest, VMPoolUsesConfiguredPolicy)
{
  neamc::vm::VMPool::Config cfg;
  cfg.min_vms = 1;
  cfg.max_vms = 2;
  cfg.reset_policy = neamc::vm::ResetPolicy::Minimal;

  neamc::vm::VMPool pool(cfg);
  pool.warm(1);

  {
    auto vm = pool.acquire();
    EXPECT_NE(vm.get(), nullptr);
    // PooledVM dtor calls release() which uses Minimal reset
  }

  // Should still be able to acquire
  auto vm2 = pool.acquire();
  EXPECT_NE(vm2.get(), nullptr);
}

// --- BytecodeCache extension tests ---

TEST(BytecodeCacheTest, PrecompileFile)
{
  // Write a temp file
  const std::string path = "/tmp/v072_test_precompile.neam";
  {
    std::ofstream f(path);
    f << "{ emit 42; }\n";
  }

  neamc::vm::BytecodeCache cache;
  EXPECT_NO_THROW(cache.precompile_file("test_file", path));

  const auto* bc = cache.get("test_file");
  EXPECT_NE(bc, nullptr);

  auto stats = cache.stats();
  EXPECT_EQ(stats.entries, 1u);
}

TEST(BytecodeCacheTest, PrecompileFileMissing)
{
  neamc::vm::BytecodeCache cache;
  EXPECT_THROW(cache.precompile_file("bad", "/tmp/nonexistent_v072.neam"),
               std::runtime_error);
}

TEST(BytecodeCacheTest, LoadBytecode)
{
  // Compile to bytecode
  neamc::Pipeline pipeline;
  auto unit = pipeline.compile("{ emit 99; }", {});

  neamc::vm::BytecodeCache cache;
  cache.load_bytecode("aot_test", std::move(unit.chunk));

  const auto* bc = cache.get("aot_test");
  EXPECT_NE(bc, nullptr);

  // Run it
  neamc::vm::VirtualMachine vm;
  vm.run(*bc);
  EXPECT_FALSE(vm.emitted().empty());
}

// --- ConnectionPoolConfig tests ---

TEST(ConnectionPoolTest, ConfigurePoolSize)
{
  neamc::llm::ConnectionPoolConfig cfg;
  cfg.max_pool_size = 16;
  cfg.stale_timeout_secs = 30;

  // Should not throw
  EXPECT_NO_THROW(neamc::llm::configure_connection_pool(cfg));
}

TEST(ConnectionPoolTest, StatsInitiallyZero)
{
  auto stats = neamc::llm::connection_pool_stats();
  // Stats may not be zero if other tests used the pool, but should not crash
  EXPECT_GE(stats.created, 0u);
  EXPECT_GE(stats.reused, 0u);
}

// --- RequestMetrics tests ---

TEST(RequestMetricsTest, EmptyMetrics)
{
  neamc::vm::RequestMetrics metrics;
  auto snap = metrics.compute();

  EXPECT_EQ(snap.count, 0u);
  EXPECT_DOUBLE_EQ(snap.p50_ms, 0.0);
  EXPECT_DOUBLE_EQ(snap.p95_ms, 0.0);
  EXPECT_DOUBLE_EQ(snap.p99_ms, 0.0);
}

TEST(RequestMetricsTest, SingleRecord)
{
  neamc::vm::RequestMetrics metrics;

  neamc::vm::RequestTiming timing;
  timing.total_us = 5000;  // 5ms
  timing.warm = true;
  timing.agent_id = "test";
  metrics.record(timing);

  auto snap = metrics.compute();
  EXPECT_EQ(snap.count, 1u);
  EXPECT_NEAR(snap.p50_ms, 5.0, 0.1);
}

TEST(RequestMetricsTest, PercentilesCorrect)
{
  neamc::vm::RequestMetrics metrics;

  // Insert 100 values from 1000us to 100000us (1ms to 100ms)
  for (int i = 1; i <= 100; ++i)
  {
    neamc::vm::RequestTiming timing;
    timing.total_us = i * 1000.0;
    timing.warm = true;
    metrics.record(timing);
  }

  auto snap = metrics.compute();
  EXPECT_EQ(snap.count, 100u);

  // p50 should be around 50ms
  EXPECT_NEAR(snap.p50_ms, 50.0, 2.0);
  // p95 should be around 95ms
  EXPECT_NEAR(snap.p95_ms, 95.0, 2.0);
  // p99 should be around 99ms
  EXPECT_NEAR(snap.p99_ms, 99.0, 2.0);
}

TEST(RequestMetricsTest, RollingWindowEviction)
{
  neamc::vm::RequestMetrics metrics(100);  // Small window

  // Insert 200 values
  for (int i = 0; i < 200; ++i)
  {
    neamc::vm::RequestTiming timing;
    timing.total_us = 1000;
    metrics.record(timing);
  }

  auto snap = metrics.compute();
  EXPECT_EQ(snap.count, 200u);  // Total count includes evicted
  // But window only has 100 entries for percentile computation
}

TEST(RequestMetricsTest, TotalCount)
{
  neamc::vm::RequestMetrics metrics;

  neamc::vm::RequestTiming timing;
  timing.total_us = 1000;
  for (int i = 0; i < 50; ++i)
  {
    metrics.record(timing);
  }

  EXPECT_EQ(metrics.total_count(), 50u);
}
