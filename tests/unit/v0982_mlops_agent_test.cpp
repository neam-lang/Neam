//
// Neam v0.9.8.2 — MLOps Agent Unit Tests
//
#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include <string>

namespace {
using namespace neamc;
using namespace neamc::vm;

static void assert_compiles(const std::string& source) { Pipeline p; EXPECT_NO_THROW(p.compile(source, {})); }

TEST(V0982MLOpsAgent, MinimalAgent) {
  assert_compiles(R"(
    budget B { cost: 100.00, tokens: 500000 }
    mlops agent MinMLOps { provider: "openai", model: "gpt-4o", temperature: 0.2, budget: B }
  )");
}

TEST(V0982MLOpsAgent, WithDriftMonitor) {
  assert_compiles(R"(
    budget B { cost: 100.00, tokens: 500000 }
    drift_monitor DM { data_drift: { method: "evidently", check_frequency: "hourly" }, alerts: { channels: ["slack"] } }
    mlops agent MO { provider: "openai", model: "gpt-4o", drift_monitor: DM, budget: B }
  )");
}

TEST(V0982MLOpsAgent, ContextualKeywords) {
  assert_compiles(R"(
    let mlops = "not_agent";
    let drift_monitor = 42;
  )");
}
} // namespace
