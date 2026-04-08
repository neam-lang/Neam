//
// Neam v1.3 — NeamLab Auto Research Agent Unit Tests
// Tests ALL 4 new v1.3 constructs + research agent + backward compatibility
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

// ═══════════════════════════════════════════════════════════════
// PART 1: Program Declaration
// ═══════════════════════════════════════════════════════════════

TEST(V130Program, BasicProgram) {
  assert_compiles(R"NEAM(
    program ChurnResearch {
        mission: "Optimize churn prediction model val_auc",
        success_criterion: "ChurnAUC",
        constraints: ["Only modify ./feature_pipeline.py", "Maximum 50 features"],
        process: ["Establish baseline", "Propose hypothesis", "Run experiment"]
    }
  )NEAM");
}

TEST(V130Program, FullProgram) {
  assert_compiles(R"NEAM(
    program FeatureDiscovery {
        mission: "Discover smallest set of features that predict churn at 90 percent AUC",
        success_criterion: "ChurnAUC",
        constraints: [
            "Only modify ./feature_pipeline.py",
            "Never modify ./eval_dataset.csv",
            "Maximum 50 features",
            "No PII fields"
        ],
        process: [
            "Establish baseline AUC",
            "Propose ONE feature change with hypothesis",
            "Run pipeline + train + evaluate",
            "Record outcome",
            "Keep if improved"
        ],
        autonomy: "full",
        halt_conditions: [
            "AUC reaches 0.95",
            "10 consecutive failed improvements",
            "Cost budget exhausted"
        ],
        style_preferences: [
            "Prefer interpretable features",
            "Prefer fewer features when results similar"
        ],
        version: "1.0.0"
    }
  )NEAM");
}

TEST(V130Program, ProgramWithMetadata) {
  assert_compiles(R"NEAM(
    program SecureChurnResearch {
        mission: "Optimize churn model with PDPA compliance",
        success_criterion: "ChurnAUC",
        constraints: ["No PII"],
        process: ["Iterate"],
        autonomy: "human_in_loop",
        version: "2.0.0",
        metadata: { owner: "data_team", team: "ml_research", classification: "internal" }
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 2: Metric Extractor
// ═══════════════════════════════════════════════════════════════

TEST(V130MetricExtractor, GrepMethod) {
  assert_compiles(R"NEAM(
    metric_extractor ValBpb {
        direction: "lower_is_better",
        method: "grep",
        pattern: "val_bpb:"
    }
  )NEAM");
}

TEST(V130MetricExtractor, RegexMethod) {
  assert_compiles(R"NEAM(
    metric_extractor ChurnAUC {
        direction: "higher_is_better",
        method: "regex",
        pattern: "AUC: numeric value",
        regex_group: 1,
        tolerance: 0.005
    }
  )NEAM");
}

TEST(V130MetricExtractor, JsonPathMethod) {
  assert_compiles(R"NEAM(
    metric_extractor ModelAccuracy {
        direction: "higher_is_better",
        method: "json_path",
        path: "$.metrics.accuracy",
        tolerance: 0.001
    }
  )NEAM");
}

TEST(V130MetricExtractor, FunctionMethod) {
  assert_compiles(R"NEAM(
    metric_extractor HarmonicMean {
        direction: "higher_is_better",
        method: "function",
        function_ref: "compute_harmonic_mean"
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 3: Research Agent
// ═══════════════════════════════════════════════════════════════

TEST(V130ResearchAgent, BasicResearchAgent) {
  assert_compiles(R"NEAM(
    budget B { cost: 100.00, tokens: 1000000 }
    program ChurnResearch {
        mission: "Optimize churn AUC",
        success_criterion: "ChurnAUC",
        constraints: ["Only modify pipeline"],
        process: ["Iterate"]
    }
    metric_extractor ChurnAUC {
        direction: "higher_is_better",
        method: "regex",
        pattern: "AUC pattern"
    }
    session_service ResearchSessions {
        backend: "inmemory",
        ttl: "7d"
    }
    research agent ChurnOptimizer {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        program: ChurnResearch,
        metric: ChurnAUC,
        experiment_log: ResearchSessions,
        mutable_artifacts: ["./feature_pipeline.py"],
        immutable_artifacts: ["./eval_dataset.csv"],
        iteration_budget: { time_per_run: "5m", max_runs: 50 },
        on_improvement: "keep",
        on_failure: "revert"
    }
  )NEAM");
}

TEST(V130ResearchAgent, ResearchAgentWithHypothesis) {
  assert_compiles(R"NEAM(
    budget B { cost: 50.00, tokens: 500000 }
    program PromptResearch {
        mission: "Optimize system prompt for accuracy",
        success_criterion: "PromptAcc",
        constraints: ["Only modify prompt.txt"],
        process: ["Iterate"]
    }
    metric_extractor PromptAcc {
        direction: "higher_is_better",
        method: "json_path",
        path: "$.accuracy"
    }
    session_service Sessions {
        backend: "inmemory",
        ttl: "1d"
    }
    research agent PromptOptimizer {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        program: PromptResearch,
        metric: PromptAcc,
        experiment_log: Sessions,
        mutable_artifacts: ["./prompt.txt"],
        iteration_budget: { time_per_run: "30s", max_runs: 100 },
        hypothesis_required: true,
        target_metric: 0.95
    }
  )NEAM");
}

TEST(V130ResearchAgent, ResearchAgentUntilInterrupted) {
  assert_compiles(R"NEAM(
    budget B { cost: 1000.00, tokens: 10000000 }
    program LongRunResearch {
        mission: "Run forever optimizing",
        success_criterion: "ValBpb",
        constraints: ["Only modify train.py"],
        process: ["Iterate forever"]
    }
    metric_extractor ValBpb {
        direction: "lower_is_better",
        method: "grep",
        pattern: "val_bpb:"
    }
    session_service Sessions {
        backend: "postgresql",
        ttl: "30d"
    }
    research agent ContinuousOptimizer {
        provider: "anthropic",
        model: "claude-opus-4",
        budget: B,
        program: LongRunResearch,
        metric: ValBpb,
        experiment_log: Sessions,
        mutable_artifacts: ["./train.py"],
        immutable_artifacts: ["./prepare.py", "./eval.csv"],
        iteration_budget: { time_per_run: "5m" },
        until_interrupted: true,
        plugin_throttle: "every_10_iterations"
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 4: Experiment Loop
// ═══════════════════════════════════════════════════════════════

TEST(V130ExperimentLoop, BasicLoop) {
  assert_compiles(R"NEAM(
    experiment_loop ChurnLoop {
        research_agent: "ChurnOptimizer",
        until: { time: "8h", iterations: 100 }
    }
  )NEAM");
}

TEST(V130ExperimentLoop, LoopWithHooks) {
  assert_compiles(R"NEAM(
    experiment_loop ChurnLoopWithNotify {
        research_agent: "ChurnOptimizer",
        until: { time: "8h", target_metric: 0.95 },
        on_improvement: { notify: "slack_channel", threshold: 0.05 },
        on_failure: { log: "verbose", alert_after: 5 },
        notify: "ops_channel"
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 5: Karpathy autoresearch Reproduction
// ═══════════════════════════════════════════════════════════════

TEST(V130Reproduction, KarpathyAutoresearchEquivalent) {
  assert_compiles(R"NEAM(
    budget B { cost: 50.00, tokens: 5000000, time: 28800000 }

    program AutoresearchProgram {
        mission: "Minimize val_bpb within 5-minute training budget",
        success_criterion: "ValBpb",
        constraints: [
            "Only modify ./train.py",
            "Off-limits: ./prepare.py, dependencies, evaluation harness",
            "Some VRAM increase acceptable for meaningful val_bpb gains"
        ],
        process: [
            "Establish baseline on first run",
            "Propose modifications with specific hypotheses",
            "Execute training",
            "Extract val_bpb from logs",
            "Record outcome in experiment log",
            "Keep improvements; revert failures",
            "Continue indefinitely until manually stopped"
        ],
        autonomy: "full",
        style_preferences: ["A small improvement that adds ugly complexity is not worth it"]
    }

    metric_extractor ValBpb {
        direction: "lower_is_better",
        method: "grep",
        pattern: "val_bpb:",
        tolerance: 0.001
    }

    session_service ExperimentLog {
        backend: "sqlite",
        ttl: "365d"
    }

    research agent AutoresearchAgent {
        provider: "anthropic",
        model: "claude-opus-4",
        budget: B,
        program: AutoresearchProgram,
        metric: ValBpb,
        experiment_log: ExperimentLog,
        mutable_artifacts: ["./train.py"],
        immutable_artifacts: ["./prepare.py", "./pyproject.toml"],
        iteration_budget: { time_per_run: "5m" },
        until_interrupted: true,
        on_improvement: "keep",
        on_failure: "revert",
        hypothesis_required: true
    }

    experiment_loop OvernightRun {
        research_agent: "AutoresearchAgent",
        until: { time: "8h", interrupt: true }
    }

    print("Karpathy autoresearch paradigm in Neam v1.3");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 6: Backward Compatibility
// ═══════════════════════════════════════════════════════════════

TEST(V130BackwardCompat, ContextualKeywords) {
  assert_compiles(R"NEAM(
    let program = "just a string";
    let experiment_loop = 42;
    let metric_extractor = true;
    let research = "variable";
    print(program);
  )NEAM");
}

TEST(V130BackwardCompat, V12StillWorks) {
  assert_compiles(R"NEAM(
    plugin Logger { hooks: { before_agent: "observe" }, priority: 1 }
    session_service S { backend: "inmemory", ttl: "1h" }
    eval_test E { agent: "DS", input: "test", criteria: ["safety"], threshold: 0.9 }
    eval_set ES { tests: ["E"], judge: { model: "gpt-4o-mini" } }
    artifact_store A { backend: "filesystem", path: "./out" }
    stream_config SC { mode: "sse" }
    a2a_config AC { expose_as_server: true }
  )NEAM");
}

TEST(V130BackwardCompat, V11StillWorks) {
  assert_compiles(R"NEAM(
    knowledge_card K { type: "concept", term: "Test", definition: "A test", domain: "t.d", version: "1.0.0" }
    context_assembly C { target_agent: "A", cards: ["K"], max_context_tokens: 2000 }
    governance_rule G { trigger: "data_write", condition: "pii", action: { audit: "yes" } }
    blueprint BP { version: "1.0.0", agents: ["A"] }
  )NEAM");
}

TEST(V130BackwardCompat, V10OWASPStillWorks) {
  assert_compiles(R"NEAM(
    goal_integrity G { declared_objectives: ["test"] }
    circuit_breaker CB { failure_threshold: 3 }
    human_gate HG { approve_before: ["deploy"] }
    gateway API { auth: { method: "api_key" } }
  )NEAM");
}

TEST(V130BackwardCompat, V09DataAgentsStillWork) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    datascientist agent DS { provider: "openai", model: "gpt-4o", budget: B }
    causal agent CA { provider: "openai", model: "gpt-4o", budget: B }
  )NEAM");
}

TEST(V130BackwardCompat, ForgeAgentStillWorks) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    forge agent F {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        verify: { criteria: "passes_tests" }
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 7: Coexistence — All Versions Together
// ═══════════════════════════════════════════════════════════════

TEST(V130Coexistence, AllVersionsTogether) {
  assert_compiles(R"NEAM(
    budget B { cost: 500.00, tokens: 2000000 }

    // v0.6: Basic agent
    agent Helper { provider: "openai", model: "gpt-4o-mini", system: "You help." }

    // v1.0: OWASP Security
    goal_integrity Goals { declared_objectives: ["secure ops"] }
    circuit_breaker CB { failure_threshold: 3 }

    // v1.1: NeamOS Foundation
    knowledge_card Churn { type: "concept", term: "Churn", definition: "No txn 90d", domain: "t.r", version: "1.0.0" }
    governance_rule PDPA { trigger: "data_write", condition: "pii", action: { audit: "yes" } }
    blueprint BP { version: "1.0.0", agents: ["DS"] }
    knowledgeweaver agent Weaver { provider: "openai", model: "gpt-4o", budget: B, fabric: { store: "./" } }

    // v1.2: NeamProd
    plugin Logger { hooks: { before_agent: "observe" }, priority: 1 }
    session_service Sessions { backend: "inmemory", ttl: "24h" }
    eval_test E { agent: "DS", input: "test", criteria: ["safety"], threshold: 0.9 }
    artifact_store Artifacts { backend: "filesystem", path: "./out" }
    stream_config Stream { mode: "sse" }

    // v1.3: NeamLab — Auto Research Agent
    program ResearchProgram {
        mission: "Optimize agent quality",
        success_criterion: "QualityMetric",
        constraints: ["Only modify configs"],
        process: ["Iterate"]
    }
    metric_extractor QualityMetric {
        direction: "higher_is_better",
        method: "regex",
        pattern: "score pattern"
    }
    research agent QualityResearcher {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        program: ResearchProgram,
        metric: QualityMetric,
        experiment_log: Sessions,
        mutable_artifacts: ["./config.toml"]
    }
    experiment_loop OvernightRun {
        research_agent: "QualityResearcher",
        until: { time: "8h" }
    }

    // v0.9 Data Intelligence
    datascientist agent DS { provider: "openai", model: "gpt-4o", budget: B }

    // v1.0 Special Agents
    securitysentinel agent Sentinel { provider: "openai", model: "gpt-4o", budget: B, monitors: { all: true } }

    print("v0.6 + v0.9 + v1.0 + v1.1 + v1.2 + v1.3 all coexist");
  )NEAM");
}

}  // namespace
