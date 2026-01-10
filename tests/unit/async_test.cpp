#include "neamc/vm/async/executor.hpp"

#include <chrono>
#include <thread>

#include <gtest/gtest.h>

namespace
{
using neamc::vm::async::Executor;
using neamc::vm::async::Future;

TEST(AsyncExecutorTest, SubmitCompletes)
{
  Executor executor(2);
  auto future = executor.submit([]() { return 42; });
  EXPECT_EQ(future.wait(), 42);
}

TEST(AsyncExecutorTest, ThenChains)
{
  Executor executor(2);
  auto future = executor.submit([]() { return 10; });
  auto chained = future.then([](int value) { return value + 5; });
  EXPECT_EQ(chained.wait(), 15);
}

TEST(AsyncExecutorTest, WithTimeout)
{
  Executor executor(1);
  auto future = executor.submit([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    return 7;
  });
  auto result = executor.with_timeout(future, std::chrono::milliseconds(1));
  EXPECT_FALSE(result.ok);
}

TEST(AsyncExecutorTest, AllAggregates)
{
  Executor executor(2);
  auto f1 = executor.submit([]() { return 1; });
  auto f2 = executor.submit([]() { return 2; });
  auto all_future = executor.all(f1, f2);
  auto result = all_future.wait();
  EXPECT_EQ(std::get<0>(result), 1);
  EXPECT_EQ(std::get<1>(result), 2);
}

TEST(AsyncExecutorTest, RaceReturnsFirst)
{
  Executor executor(2);
  auto fast = executor.submit([]() { return 3; });
  auto slow = executor.submit([]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return 4;
  });
  std::vector<Future<int>> futures;
  futures.push_back(std::move(fast));
  futures.push_back(std::move(slow));
  auto race_future = executor.race(std::move(futures));
  EXPECT_EQ(race_future.wait(), 3);
}
}  // namespace
