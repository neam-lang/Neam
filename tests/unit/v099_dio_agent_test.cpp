//
// Neam v0.9.9 — Data Intelligent Orchestrator (DIO) Unit Tests
//
#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include <string>

namespace {
using namespace neamc;
using namespace neamc::vm;

static void assert_compiles(const std::string& source) { Pipeline p; EXPECT_NO_THROW(p.compile(source, {})); }

TEST(V099DIOAgent, MinimalAutoMode) {
  assert_compiles(R"(
    budget B { cost: 500.00, tokens: 2000000 }
    dio agent MinDIO { mode: "auto", task: "Test DIO", provider: "openai", model: "gpt-4o", budget: B }
  )");
}

TEST(V099DIOAgent, ConfigModeWithInfraProfile) {
  assert_compiles(R"(
    budget B { cost: 500.00, tokens: 2000000 }
    infrastructure_profile SF { data_warehouse: { platform: "snowflake", connection: "test" }, data_science: { mlflow: { uri: "http://localhost:5000" } } }
    dio agent ConfigDIO { mode: "config", task: "Reduce churn", infrastructure: SF, provider: "openai", model: "gpt-4o", budget: B }
  )");
}

TEST(V099DIOAgent, AllSevenNewAgentsCoexist) {
  assert_compiles(R"(
    budget B { cost: 10.00, tokens: 100000 }
    datascientist agent DS { provider: "openai", model: "gpt-4o", budget: B }
    causal agent CA { provider: "openai", model: "o3-mini", budget: B }
    mlops agent MO { provider: "openai", model: "gpt-4o", budget: B }
    databa agent BA { provider: "openai", model: "gpt-4o", budget: B }
    datatest agent QA { provider: "openai", model: "gpt-4o", budget: B }
    dio agent DIO { mode: "auto", task: "Test all", provider: "openai", model: "gpt-4o", budget: B }
  )");
}

TEST(V099DIOAgent, ContextualKeywords) {
  assert_compiles(R"(
    let dio = "not_agent";
    let infrastructure_profile = 42;
    let agent_registry = "test";
  )");
}

TEST(V099DIOAgent, HybridModeWithGuardrails) {
  assert_compiles(R"(
    budget B { cost: 500.00, tokens: 2000000 }
    infrastructure_profile Infra { data_warehouse: { platform: "postgres", connection: "test" } }
    dio agent HybridDIO {
      mode: "hybrid", task: "Reduce churn",
      infrastructure: Infra,
      guardrails: { require_approval: ["deployment"], max_budget_per_phase: 100.00 },
      provider: "openai", model: "gpt-4o", budget: B
    }
  )");
}
} // namespace
