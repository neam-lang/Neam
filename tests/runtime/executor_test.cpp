#include "neamc/runtime/executor.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <variant>

#include <gtest/gtest.h>

namespace
{
using neamc::runtime::Executor;
using neamc::runtime::Future;
using neamc::runtime::TimeoutError;

TEST(ExecutorTest, SpawnAndAwait)
{
  Executor executor;

  auto future = executor.spawn<int>([]() { return 42; });
  auto result = future.wait();

  ASSERT_TRUE(result.is_ok());
  EXPECT_EQ(result.unwrap(), 42);
}

TEST(ExecutorTest, AllCombinator)
{
  Executor executor;

  std::vector<Future<int>> futures;
  for (int i = 0; i < 10; ++i)
  {
    futures.push_back(executor.spawn<int>([i]() { return i * 2; }));
  }

  auto all_future = executor.all(std::move(futures));
  auto results = all_future.wait().unwrap();

  EXPECT_EQ(results.size(), 10u);
  for (int i = 0; i < 10; ++i)
  {
    EXPECT_EQ(results[static_cast<size_t>(i)], i * 2);
  }
}

TEST(ExecutorTest, RaceCombinator)
{
  Executor executor;

  auto slow = executor.spawn<int>([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    return 1;
  });

  auto fast = executor.spawn<int>([]() { return 2; });

  std::vector<Future<int>> race_futures;
  race_futures.reserve(2);
  race_futures.push_back(std::move(slow));
  race_futures.push_back(std::move(fast));

  auto race_future = executor.race(std::move(race_futures));
  auto result = race_future.wait().unwrap();

  EXPECT_EQ(result, 2);
}

TEST(ExecutorTest, TimeoutExpired)
{
  Executor executor;

  auto slow = executor.spawn<int>([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    return 42;
  });

  auto timeout_future = executor.timeout(std::move(slow), std::chrono::milliseconds{20});
  auto result = timeout_future.wait();

  ASSERT_TRUE(result.is_ok());
  EXPECT_TRUE(result.unwrap().is_err());
  EXPECT_TRUE(std::holds_alternative<TimeoutError>(result.unwrap().unwrap_err()));
}

TEST(ExecutorTest, Cancellation)
{
  Executor executor;

  std::atomic<bool> started{false};
  std::atomic<bool> completed{false};

  auto future = executor.spawn<int>([&]() {
    started = true;
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    completed = true;
    return 42;
  });

  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  future.cancel("Test cancellation");

  auto result = future.wait();

  EXPECT_TRUE(started.load());
  EXPECT_FALSE(completed.load());
  ASSERT_TRUE(result.is_err());
}
}  // namespace
