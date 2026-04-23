//
// Neam v1.4.5 — NeamHarness Phase 0 Unit Tests
// Tests the minimal harness declaration (sub-type 25 on OP_DEFINE_DIO_DECLARATION):
// parser recognition, AST variant registration, bytecode emission, VM dispatch.
//
// Phase 0 scope: declaration parses and compiles; runtime behavior (handoff,
// tool_registry, assertion_registry, forge roles, trace replay, benchmarking)
// lands in subsequent phases.
//

#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include <string>

namespace {
using namespace neamc;
using namespace neamc::vm;

static void assert_compiles(const std::string& source) {
  Pipeline p;
  EXPECT_NO_THROW(p.compile(source, {}));
}

static void assert_fails(const std::string& source) {
  Pipeline p;
  EXPECT_ANY_THROW(p.compile(source, {}));
}

// ═══════════════════════════════════════════════════════════════
// PART 1: Minimal Harness Declaration (Phase 0)
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, BasicHarness) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    agent Main {
        provider: "anthropic",
        model: "claude-sonnet-4",
        system: "Answer questions",
        budget: B
    }
    harness M {
        provider: "anthropic",
        model: "claude-sonnet-4",
        budget: B,
        sub_agents: { main: Main }
    }
  )NEAM");
}

TEST(V145Harness, HarnessWithAllCoreFields) {
  assert_compiles(R"NEAM(
    budget B { cost: 500.00, tokens: 10000000 }
    agent Worker { provider: "anthropic", model: "claude-opus-4", system: "work", budget: B }
    harness App {
        provider: "anthropic",
        model: "claude-opus-4",
        temperature: 0.2,
        budget: B,
        sub_agents: { main: Worker },
        context: {
            compaction: "auto",
            max_window_tokens: 200000,
            preserve_across_reset: ["goal", "progress"]
        },
        observability: {
            metrics: ["task_success_rate", "tokens_per_task"],
            audit: true
        }
    }
  )NEAM");
}

TEST(V145Harness, MinimalHarnessNoAgents) {
  // Phase 0 accepts this; Phase 1 H-001 validation will reject empty sub_agents.
  // This test documents the current boundary.
  assert_compiles(R"NEAM(
    harness Empty {
        provider: "anthropic",
        model: "claude-sonnet-4"
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 2: Interaction with v1.4 and Prior Constructs
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, HarnessCoexistsWithWiki) {
  assert_compiles(R"NEAM(
    wiki KnowledgeBase {
        topic: "engineering",
        raw_path: "./wiki/raw",
        wiki_path: "./wiki/topics"
    }
    budget B { cost: 50.00, tokens: 500000 }
    agent Research { provider: "anthropic", model: "claude-opus-4", system: "research", budget: B }
    harness Pipeline {
        provider: "anthropic",
        model: "claude-opus-4",
        budget: B,
        sub_agents: { research: Research }
    }
  )NEAM");
}

TEST(V145Harness, MultipleHarnessesInOneProgram) {
  assert_compiles(R"NEAM(
    budget B1 { cost: 10.00, tokens: 100000 }
    budget B2 { cost: 50.00, tokens: 500000 }
    agent A { provider: "anthropic", model: "claude-sonnet-4", system: "a", budget: B1 }
    agent BAgent { provider: "anthropic", model: "claude-opus-4", system: "b", budget: B2 }
    harness H1 {
        provider: "anthropic", model: "claude-sonnet-4",
        budget: B1,
        sub_agents: { main: A }
    }
    harness H2 {
        provider: "anthropic", model: "claude-opus-4",
        budget: B2,
        sub_agents: { main: BAgent }
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 3: Backward Compatibility (v0.6–v1.4 programs unaffected)
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, BackwardCompatPlainAgent) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    agent Plain { provider: "anthropic", model: "claude-sonnet-4", system: "hi", budget: B }
  )NEAM");
}

TEST(V145Harness, BackwardCompatWikiOnly) {
  assert_compiles(R"NEAM(
    wiki W { topic: "t", raw_path: "./r", wiki_path: "./w" }
  )NEAM");
}

}  // namespace
