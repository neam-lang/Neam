//
// NeamC - Testing framework
//

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "neamc/diagnostic.hpp"

namespace neamc::testing
{
enum class TestResult
{
  kPassed,
  kFailed,
  kSkipped,
  kTimedOut,
  kPanicked
};

struct TestFailure
{
  std::string message;
  std::string expected;
  std::string actual;
  SourceLocation location;
  std::optional<std::string> diff;
};

struct TestOutcome
{
  TestResult result{TestResult::kPassed};
  std::chrono::milliseconds duration{0};
  std::optional<TestFailure> failure;
  std::vector<std::string> logs;
};

struct TestCase
{
  std::string name;
  std::string suite_name;
  std::function<void()> body;
  bool is_async{false};
  bool should_panic{false};
  std::optional<std::string> expected_panic_message;
  bool ignore{false};
  std::optional<std::chrono::milliseconds> timeout;
};

struct TestSuite
{
  std::string name;
  std::vector<TestCase> tests;
  std::vector<TestSuite> nested_suites;
  std::optional<std::function<void()>> before_each;
  std::optional<std::function<void()>> after_each;
  std::optional<std::function<void()>> before_all;
  std::optional<std::function<void()>> after_all;
};

class TestRunner
{
public:
  struct Config
  {
    bool parallel{true};
    size_t thread_count{std::thread::hardware_concurrency()};
    bool fail_fast{false};
    std::optional<std::string> filter;
    bool verbose{false};
    std::optional<std::string> output_file;
    std::string output_format{"text"};
  };

  explicit TestRunner(Config config = {});

  void register_suite(TestSuite suite);
  void register_test(TestCase test);

  struct RunResult
  {
    size_t total{0};
    size_t passed{0};
    size_t failed{0};
    size_t skipped{0};
    std::chrono::milliseconds total_duration{0};
    std::vector<std::pair<std::string, TestOutcome>> outcomes;
  };

  RunResult run();
  RunResult run_suite(const std::string& suite_name);
  RunResult run_test(const std::string& test_name);

  void print_results(const RunResult& result);
  void export_junit(const RunResult& result, const std::string& path);
  void export_json(const RunResult& result, const std::string& path);

private:
  struct SuiteContext
  {
    std::vector<std::function<void()>> before_each;
    std::vector<std::function<void()>> after_each;
  };

  TestOutcome run_single_test(const TestCase& test, const SuiteContext& context);
  void run_before_each(const SuiteContext& context);
  void run_after_each(const SuiteContext& context);
  void run_suite_recursive(const TestSuite& suite, const std::string& prefix, RunResult& result,
                           SuiteContext context);

  Config config_;
  std::vector<TestSuite> suites_;
  std::vector<TestCase> standalone_tests_;
};

TestRunner& global_runner();

}  // namespace neamc::testing
