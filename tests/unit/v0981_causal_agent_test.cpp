//
// Neam v0.9.8.1 — Causal Agent Unit Tests
//
#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include <string>

namespace {
using namespace neamc;
using namespace neamc::vm;

static void assert_compiles(const std::string& source) { Pipeline p; EXPECT_NO_THROW(p.compile(source, {})); }

TEST(V0981CausalAgent, MinimalAgent) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    causal agent MinCausal { provider: "openai", model: "o3-mini", temperature: 0.3, budget: B }
  )");
}

TEST(V0981CausalAgent, WithSCMAndIntervention) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    scm TestSCM { variables: { x: { parents: [], equation: "linear" }, y: { parents: ["x"], equation: "linear" } } }
    intervention TestDo { scm: TestSCM, do: { variable: "x", value: "high" }, outcome: "y" }
    causal agent CA { provider: "openai", model: "o3-mini", scm: TestSCM, intervention: TestDo, budget: B }
  )");
}

TEST(V0981CausalAgent, ContextualKeywords) {
  assert_compiles(R"(
    let causal = "not_agent";
    let scm = "just_string";
    let counterfactual = 42;
  )");
}
} // namespace
