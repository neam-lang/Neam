//
// NeamC - Testing framework implementation
//

#include "neamc/testing/framework.hpp"

#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>

#include "neamc/testing/assertions.hpp"

namespace neamc::testing
{
namespace
{
bool matches_filter(const std::optional<std::string>& filter, const std::string& name)
{
  if (!filter.has_value())
  {
    return true;
  }
  return name.find(*filter) != std::string::npos;
}

std::string outcome_to_string(TestResult result)
{
  switch (result)
  {
    case TestResult::kPassed:
      return "passed";
    case TestResult::kFailed:
      return "failed";
    case TestResult::kSkipped:
      return "skipped";
    case TestResult::kTimedOut:
      return "timed_out";
    case TestResult::kPanicked:
      return "panicked";
  }
  return "unknown";
}

}  // namespace

TestRunner::TestRunner() : config_() {}
TestRunner::TestRunner(Config config) : config_(std::move(config)) {}

void TestRunner::register_suite(TestSuite suite)
{
  suites_.push_back(std::move(suite));
}

void TestRunner::register_test(TestCase test)
{
  standalone_tests_.push_back(std::move(test));
}

TestRunner::RunResult TestRunner::run()
{
  RunResult result;
  auto start = std::chrono::steady_clock::now();

  for (const auto& suite : suites_)
  {
    run_suite_recursive(suite, suite.name, result, {});
    if (config_.fail_fast && result.failed > 0)
    {
      break;
    }
  }

  for (const auto& test : standalone_tests_)
  {
    if (!matches_filter(config_.filter, test.name))
    {
      continue;
    }
    auto outcome = run_single_test(test, {});
    result.outcomes.emplace_back(test.name, outcome);
    result.total += 1;
    if (outcome.result == TestResult::kPassed)
    {
      result.passed += 1;
    }
    else if (outcome.result == TestResult::kSkipped)
    {
      result.skipped += 1;
    }
    else
    {
      result.failed += 1;
    }
    if (config_.fail_fast && outcome.result == TestResult::kFailed)
    {
      break;
    }
  }

  result.total_duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  return result;
}

TestRunner::RunResult TestRunner::run_suite(const std::string& suite_name)
{
  RunResult result;
  for (const auto& suite : suites_)
  {
    if (suite.name == suite_name)
    {
      run_suite_recursive(suite, suite.name, result, {});
    }
  }
  return result;
}

TestRunner::RunResult TestRunner::run_test(const std::string& test_name)
{
  RunResult result;
  for (const auto& test : standalone_tests_)
  {
    if (test.name == test_name)
    {
      auto outcome = run_single_test(test, {});
      result.outcomes.emplace_back(test.name, outcome);
      result.total = 1;
      if (outcome.result == TestResult::kPassed)
      {
        result.passed = 1;
      }
      else if (outcome.result == TestResult::kSkipped)
      {
        result.skipped = 1;
      }
      else
      {
        result.failed = 1;
      }
      return result;
    }
  }
  return result;
}

void TestRunner::print_results(const RunResult& result)
{
  std::ostringstream out;
  out << "Total: " << result.total << ", Passed: " << result.passed << ", Failed: "
      << result.failed << ", Skipped: " << result.skipped << "\n";
  for (const auto& [name, outcome] : result.outcomes)
  {
    out << name << ": " << outcome_to_string(outcome.result) << " ("
        << outcome.duration.count() << "ms)";
    if (outcome.failure)
    {
      out << " - " << outcome.failure->message;
    }
    out << "\n";
  }

  if (config_.output_file)
  {
    std::ofstream file(*config_.output_file);
    file << out.str();
  }
  else
  {
    std::cout << out.str();
  }
}

void TestRunner::export_junit(const RunResult& result, const std::string& path)
{
  std::ofstream file(path);
  file << "<testsuite tests=\"" << result.total << "\" failures=\"" << result.failed
       << "\" skipped=\"" << result.skipped << "\">\n";
  for (const auto& [name, outcome] : result.outcomes)
  {
    file << "  <testcase name=\"" << name << "\" time=\"" << outcome.duration.count() / 1000.0
         << "\">";
    if (outcome.result == TestResult::kFailed || outcome.result == TestResult::kTimedOut)
    {
      file << "<failure message=\"" << (outcome.failure ? outcome.failure->message : "failed") << "\"/>";
    }
    if (outcome.result == TestResult::kSkipped)
    {
      file << "<skipped/>";
    }
    file << "</testcase>\n";
  }
  file << "</testsuite>\n";
}

void TestRunner::export_json(const RunResult& result, const std::string& path)
{
  std::ofstream file(path);
  file << "{\"total\":" << result.total << ",\"passed\":" << result.passed
       << ",\"failed\":" << result.failed << ",\"skipped\":" << result.skipped << ",\"tests\":[";
  for (size_t i = 0; i < result.outcomes.size(); ++i)
  {
    const auto& [name, outcome] = result.outcomes[i];
    file << "{\"name\":\"" << name << "\",\"result\":\"" << outcome_to_string(outcome.result)
         << "\",\"duration_ms\":" << outcome.duration.count() << "}";
    if (i + 1 < result.outcomes.size())
    {
      file << ",";
    }
  }
  file << "]}";
}

TestOutcome TestRunner::run_single_test(const TestCase& test, const SuiteContext& context)
{
  TestOutcome outcome;
  if (test.ignore)
  {
    outcome.result = TestResult::kSkipped;
    return outcome;
  }

  run_before_each(context);
  auto start = std::chrono::steady_clock::now();

  struct State
  {
    std::mutex mutex;
    std::condition_variable cv;
    bool done{false};
    std::optional<TestOutcome> outcome;
  };

  auto state = std::make_shared<State>();
  std::thread worker([state, test]() mutable {
    TestOutcome local;
    auto local_start = std::chrono::steady_clock::now();
    try
    {
      test.body();
      if (test.should_panic)
      {
        local.result = TestResult::kFailed;
        local.failure = TestFailure{"Expected panic but test completed", {}, {}, {}, std::nullopt};
      }
      else
      {
        local.result = TestResult::kPassed;
      }
    }
    catch (const AssertionFailure& failure)
    {
      local.result = TestResult::kFailed;
      local.failure = TestFailure{failure.message(), {}, {}, failure.location(), std::nullopt};
    }
    catch (const std::exception& ex)
    {
      if (test.should_panic)
      {
        if (test.expected_panic_message && std::string(ex.what()).find(*test.expected_panic_message) == std::string::npos)
        {
          local.result = TestResult::kFailed;
          local.failure = TestFailure{"Panic message mismatch", *test.expected_panic_message, ex.what(), {}, std::nullopt};
        }
        else
        {
          local.result = TestResult::kPassed;
        }
      }
      else
      {
        local.result = TestResult::kPanicked;
        local.failure = TestFailure{ex.what(), {}, {}, {}, std::nullopt};
      }
    }
    catch (...)
    {
      local.result = TestResult::kPanicked;
      local.failure = TestFailure{"Unknown panic", {}, {}, {}, std::nullopt};
    }
    local.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - local_start);

    {
      std::lock_guard<std::mutex> lock(state->mutex);
      state->done = true;
      state->outcome = local;
    }
    state->cv.notify_one();
  });

  bool timed_out = false;
  {
    std::unique_lock<std::mutex> lock(state->mutex);
    if (test.timeout)
    {
      if (!state->cv.wait_for(lock, *test.timeout, [&state]() { return state->done; }))
      {
        timed_out = true;
      }
    }
    else
    {
      state->cv.wait(lock, [&state]() { return state->done; });
    }
  }

  if (timed_out)
  {
    outcome.result = TestResult::kTimedOut;
    outcome.failure = TestFailure{"Test timed out", {}, {}, {}, std::nullopt};
  }
  else
  {
    outcome = *state->outcome;
  }

  if (worker.joinable())
  {
    worker.detach();
  }

  outcome.duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);

  run_after_each(context);
  return outcome;
}

void TestRunner::run_before_each(const SuiteContext& context)
{
  for (const auto& hook : context.before_each)
  {
    hook();
  }
}

void TestRunner::run_after_each(const SuiteContext& context)
{
  for (auto it = context.after_each.rbegin(); it != context.after_each.rend(); ++it)
  {
    (*it)();
  }
}

void TestRunner::run_suite_recursive(const TestSuite& suite, const std::string& prefix,
                                     RunResult& result, SuiteContext context)
{
  if (suite.before_all)
  {
    (*suite.before_all)();
  }

  if (suite.before_each)
  {
    context.before_each.push_back(*suite.before_each);
  }
  if (suite.after_each)
  {
    context.after_each.push_back(*suite.after_each);
  }

  for (const auto& test : suite.tests)
  {
    const auto full_name = prefix.empty() ? test.name : prefix + "::" + test.name;
    if (!matches_filter(config_.filter, full_name))
    {
      continue;
    }
    auto outcome = run_single_test(test, context);
    result.outcomes.emplace_back(full_name, outcome);
    result.total += 1;
    if (outcome.result == TestResult::kPassed)
    {
      result.passed += 1;
    }
    else if (outcome.result == TestResult::kSkipped)
    {
      result.skipped += 1;
    }
    else
    {
      result.failed += 1;
    }
    if (config_.fail_fast && outcome.result == TestResult::kFailed)
    {
      break;
    }
  }

  for (const auto& nested : suite.nested_suites)
  {
    const auto nested_name = prefix.empty() ? nested.name : prefix + "::" + nested.name;
    run_suite_recursive(nested, nested_name, result, context);
    if (config_.fail_fast && result.failed > 0)
    {
      break;
    }
  }

  if (suite.after_all)
  {
    (*suite.after_all)();
  }
}

TestRunner& global_runner()
{
  static TestRunner runner{};
  return runner;
}

}  // namespace neamc::testing
