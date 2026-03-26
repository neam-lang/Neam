//
// Neam v0.9.8 — DataScientist Agent Unit Tests
//
#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include <string>

namespace {
using namespace neamc;
using namespace neamc::vm;

static void assert_compiles(const std::string& source) { Pipeline p; EXPECT_NO_THROW(p.compile(source, {})); }

TEST(V098DataScientistAgent, MinimalAgent) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    datascientist agent MinDS { provider: "openai", model: "gpt-4o", temperature: 0.2, budget: B }
  )");
}

TEST(V098DataScientistAgent, WithMLExperiment) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    ml_experiment ChurnML { problem_type: "binary_classification", target: "churned", algorithms: "auto", metrics: { primary: "auc_roc" } }
    datascientist agent DS { provider: "openai", model: "gpt-4o", experiment: ChurnML, budget: B }
  )");
}

TEST(V098DataScientistAgent, WithEDAAndVolumeRouter) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    eda_config EDA { univariate: { numeric: { statistics: ["mean", "std"] } } }
    volume_router VR { routing_rules: { local_memory: { condition: "n_rows < 100000" } } }
    datascientist agent DS { provider: "openai", model: "gpt-4o", eda_config: EDA, volume_router: VR, budget: B }
  )");
}

TEST(V098DataScientistAgent, ContextualKeywordDualUse) {
  assert_compiles(R"(
    let datascientist = "variable";
    let ml_experiment = 42;
    let volume_router = true;
  )");
}

TEST(V098DataScientistAgent, CrossVersionCoexistence) {
  assert_compiles(R"(
    budget B { cost: 10.00, tokens: 100000 }
    sql_connection DB { platform: "postgres", connection: "test", database: "db" }
    analyst agent A { provider: "openai", model: "gpt-4o-mini", connections: [DB], budget: B }
    datascientist agent DS { provider: "openai", model: "gpt-4o", budget: B }
  )");
}
} // namespace
