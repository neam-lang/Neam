//
// Neam v0.9.8.4 — Data Testing Agent Unit Tests
//
#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include <string>

namespace {
using namespace neamc;
using namespace neamc::vm;

static void assert_compiles(const std::string& source) { Pipeline p; EXPECT_NO_THROW(p.compile(source, {})); }

TEST(V0984DataTestAgent, MinimalAgent) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    datatest agent MinTest { provider: "openai", model: "gpt-4o", temperature: 0.2, budget: B }
  )");
}

TEST(V0984DataTestAgent, WithETLTestSuite) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    etl_test_suite ETL { tests: { row_count: { tolerance: 0.01 } } }
    datatest agent QA { provider: "openai", model: "gpt-4o", etl_tests: ETL, budget: B }
  )");
}

TEST(V0984DataTestAgent, ContextualKeywords) {
  assert_compiles(R"(
    let datatest = "not_agent";
    let quality_gate = 42;
  )");
}
} // namespace
