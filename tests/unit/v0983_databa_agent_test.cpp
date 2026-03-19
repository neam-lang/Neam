//
// Neam v0.9.8.3 — Data Business Analyst Agent Unit Tests
//
#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include <string>

namespace {
using namespace neamc;
using namespace neamc::vm;

static void assert_compiles(const std::string& source) { Pipeline p; EXPECT_NO_THROW(p.compile(source, {})); }

TEST(V0983DataBAAgent, MinimalAgent) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    databa agent MinBA { provider: "openai", model: "gpt-4o", temperature: 0.3, budget: B }
  )");
}

TEST(V0983DataBAAgent, WithBRD) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    brd_generator ChurnBRD { project: { name: "Churn" }, objectives: { primary: "Reduce churn" } }
    databa agent BA { provider: "openai", model: "gpt-4o", brd: ChurnBRD, budget: B }
  )");
}

TEST(V0983DataBAAgent, ContextualKeywords) {
  assert_compiles(R"(
    let databa = "not_agent";
    let brd_generator = 42;
  )");
}
} // namespace
