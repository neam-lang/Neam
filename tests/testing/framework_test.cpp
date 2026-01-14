#include <gtest/gtest.h>

#include <thread>

#include "neamc/testing/assertions.hpp"
#include "neamc/testing/framework.hpp"

namespace neamc::testing
{
TEST(TestFrameworkTest, PassingTest)
{
  TestRunner runner;

  runner.register_test(TestCase{.name = "should pass", .body = []() { assert_eq(1 + 1, 2); }});

  auto result = runner.run();
  EXPECT_EQ(result.passed, 1u);
  EXPECT_EQ(result.failed, 0u);
}

TEST(TestFrameworkTest, FailingTest)
{
  TestRunner runner;

  runner.register_test(TestCase{.name = "should fail", .body = []() { assert_eq(1, 2); }});

  auto result = runner.run();
  EXPECT_EQ(result.passed, 0u);
  EXPECT_EQ(result.failed, 1u);
}

TEST(TestFrameworkTest, Timeout)
{
  TestRunner runner;

  runner.register_test(TestCase{.name = "should timeout",
                               .body = []() { std::this_thread::sleep_for(std::chrono::seconds{1}); },
                               .timeout = std::chrono::milliseconds{10}});

  auto result = runner.run();
  EXPECT_EQ(result.failed, 1u);
}
}  // namespace neamc::testing
