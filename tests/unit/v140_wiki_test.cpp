//
// Neam v1.4 — NeamWiki Compiled LLM Wiki Unit Tests
// Tests both new v1.4 keywords (wiki, wiki agent), 8 capability areas,
// and complete backward compatibility with v0.6–v1.3.
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
// PART 1: Wiki Declaration
// ═══════════════════════════════════════════════════════════════

TEST(V140Wiki, BasicWiki) {
  assert_compiles(R"NEAM(
    wiki NutritionWiki {
        topic: "nutrition",
        raw_path: "./wiki/raw/nutrition",
        wiki_path: "./wiki/topics/nutrition"
    }
  )NEAM");
}

TEST(V140Wiki, FullWiki) {
  assert_compiles(R"NEAM(
    wiki MLResearch {
        topic: "machine_learning",
        raw_path: "./wiki/raw/ml",
        wiki_path: "./wiki/topics/ml",
        description: "ML research wiki",
        hub: "~/wiki",
        inbox_path: "./wiki/inbox/ml",
        output_path: "./wiki/output/ml",
        page_types: ["source", "entity", "concept", "synthesis"],
        naming_convention: "kebab_case",
        frontmatter_format: "yaml",
        auto_compile: true,
        contradiction_detection: true,
        auto_link: true,
        auto_extract_entities: true,
        obsidian_compat: true,
        vector_store: "usearch",
        embedding_model: "nomic-embed-text",
        chunk_strategy: "markdown_section"
    }
  )NEAM");
}

TEST(V140Wiki, MultipleWikisWithSiblings) {
  assert_compiles(R"NEAM(
    wiki MathWiki {
        topic: "math",
        raw_path: "./raw/math",
        wiki_path: "./wiki/math"
    }
    wiki MLWiki {
        topic: "ml",
        raw_path: "./raw/ml",
        wiki_path: "./wiki/ml",
        sibling_wikis: ["MathWiki"]
    }
  )NEAM");
}

TEST(V140Wiki, ObsidianCompatible) {
  assert_compiles(R"NEAM(
    wiki ObsidianStyleWiki {
        topic: "personal_kb",
        raw_path: "./obsidian/raw",
        wiki_path: "./obsidian/vault",
        obsidian_compat: true,
        frontmatter_format: "yaml"
    }
  )NEAM");
}

TEST(V140Wiki, NoContradictionDetection) {
  assert_compiles(R"NEAM(
    wiki SimpleWiki {
        topic: "simple",
        raw_path: "./r",
        wiki_path: "./w",
        contradiction_detection: false,
        auto_extract_entities: false
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 2: Wiki Agent
// ═══════════════════════════════════════════════════════════════

TEST(V140WikiAgent, BasicWikiAgent) {
  assert_compiles(R"NEAM(
    budget B { cost: 100.00, tokens: 1000000 }
    wiki MyWiki {
        topic: "test",
        raw_path: "./raw",
        wiki_path: "./wiki"
    }
    wiki agent Curator {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        wikis: [MyWiki]
    }
  )NEAM");
}

TEST(V140WikiAgent, FullOperations) {
  assert_compiles(R"NEAM(
    budget B { cost: 500.00, tokens: 5000000 }
    wiki W { topic: "x", raw_path: "./r", wiki_path: "./w" }
    wiki agent Full {
        provider: "anthropic",
        model: "claude-opus-4",
        budget: B,
        wikis: [W],
        operations: ["ingest", "query", "lint", "graph", "research", "compile", "output", "thesis", "assess", "retract"],
        research_config: {
            parallel_agents: 5,
            max_rounds: 3,
            max_time_per_topic: "1h"
        },
        lint_policies: {
            flag_orphans: true,
            flag_broken_links: true,
            flag_contradictions: true,
            auto_fix: false
        },
        graph_config: {
            deterministic_pass: true,
            semantic_pass: true,
            community_detection: "louvain",
            output_format: "html"
        },
        output_formats: ["report", "slides", "glossary", "timeline", "comparison"]
    }
  )NEAM");
}

TEST(V140WikiAgent, MultiWiki) {
  assert_compiles(R"NEAM(
    budget B { cost: 50.00, tokens: 500000 }
    wiki A { topic: "a", raw_path: "./ra", wiki_path: "./wa" }
    wiki B2 { topic: "b", raw_path: "./rb", wiki_path: "./wb" }
    wiki C { topic: "c", raw_path: "./rc", wiki_path: "./wc" }
    wiki agent Multi {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        wikis: [A, B2, C]
    }
  )NEAM");
}

TEST(V140WikiAgent, ReadOnlyAgent) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    wiki W { topic: "x", raw_path: "./r", wiki_path: "./w" }
    wiki agent ReadOnly {
        provider: "openai",
        model: "gpt-4o-mini",
        budget: B,
        wikis: [W],
        operations: ["query", "lint"]
    }
  )NEAM");
}

TEST(V140WikiAgent, GovernanceIntegration) {
  assert_compiles(R"NEAM(
    budget B { cost: 50.00, tokens: 500000 }
    governance_rule PDPA { trigger: "data_write", condition: "pii", action: { audit: "yes" } }
    wiki W { topic: "regulated", raw_path: "./r", wiki_path: "./w" }
    wiki agent SecureCurator {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        wikis: [W],
        governance: [PDPA]
    }
  )NEAM");
}

TEST(V140WikiAgent, KnowledgeCardBridge) {
  assert_compiles(R"NEAM(
    budget B { cost: 50.00, tokens: 500000 }
    knowledge_card Churn { type: "concept", term: "Churn", definition: "No txn 90d", domain: "t.r", version: "1.0.0" }
    wiki W { topic: "churn_wiki", raw_path: "./r", wiki_path: "./w" }
    wiki agent BridgedAgent {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        wikis: [W],
        knowledge_cards: [Churn]
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 3: Reproductions — Karpathy / SamurAIGPT / skillsllm.com
// ═══════════════════════════════════════════════════════════════

TEST(V140Reproduction, KarpathyLlmWikiEquivalent) {
  assert_compiles(R"NEAM(
    budget B { cost: 50.00, tokens: 5000000 }
    wiki PersonalWiki {
        topic: "personal_research",
        raw_path: "./wiki/raw",
        wiki_path: "./wiki/pages",
        page_types: ["source", "entity", "concept", "synthesis"],
        contradiction_detection: true,
        auto_link: true,
        auto_extract_entities: true
    }
    wiki agent KarpathyAgent {
        provider: "anthropic",
        model: "claude-opus-4",
        budget: B,
        wikis: [PersonalWiki],
        operations: ["ingest", "query", "lint"]
    }
    print("Karpathy llm-wiki paradigm in Neam v1.4");
  )NEAM");
}

TEST(V140Reproduction, SamurAIGPTFourOpFlow) {
  assert_compiles(R"NEAM(
    budget B { cost: 100.00, tokens: 10000000 }
    wiki SamurAIWiki {
        topic: "ml_papers",
        raw_path: "./wiki/raw",
        wiki_path: "./wiki/pages",
        obsidian_compat: true
    }
    wiki agent SamurAIAgent {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        wikis: [SamurAIWiki],
        operations: ["ingest", "query", "lint", "graph"],
        graph_config: {
            deterministic_pass: true,
            semantic_pass: true,
            community_detection: "louvain",
            output_format: "html"
        }
    }
    print("SamurAIGPT 4-op flow in Neam v1.4");
  )NEAM");
}

TEST(V140Reproduction, SkillsllmHubAndSpoke) {
  assert_compiles(R"NEAM(
    budget B { cost: 200.00, tokens: 20000000 }
    wiki MLResearch {
        topic: "ml_research",
        raw_path: "./hub/topics/ml/raw",
        wiki_path: "./hub/topics/ml",
        hub: "./hub"
    }
    wiki HealthWiki {
        topic: "personal_health",
        raw_path: "./hub/topics/health/raw",
        wiki_path: "./hub/topics/health",
        hub: "./hub"
    }
    wiki InvestmentWiki {
        topic: "investment_thesis",
        raw_path: "./hub/topics/investment/raw",
        wiki_path: "./hub/topics/investment",
        hub: "./hub"
    }
    wiki agent HubAgent {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        wikis: [MLResearch, HealthWiki, InvestmentWiki],
        operations: ["ingest", "query", "lint", "graph", "research", "thesis", "output"],
        research_config: {
            parallel_agents: 5,
            max_rounds: 3,
            max_time_per_topic: "1h"
        }
    }
    print("skillsllm.com hub-and-spoke in Neam v1.4");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 4: Coexistence with v1.3 Research Agent
// ═══════════════════════════════════════════════════════════════

TEST(V140Coexistence, ResearchAgentFeedingWiki) {
  assert_compiles(R"NEAM(
    budget B { cost: 500.00, tokens: 5000000 }
    program OptProgram {
        mission: "Optimize search",
        success_criterion: "RecallMetric",
        constraints: ["only configs"],
        process: ["iterate"]
    }
    metric_extractor RecallMetric {
        direction: "higher_is_better",
        method: "regex",
        pattern: "recall pattern"
    }
    session_service Sessions { backend: "inmemory", ttl: "7d" }
    research agent Optimizer {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        program: OptProgram,
        metric: RecallMetric,
        experiment_log: Sessions,
        mutable_artifacts: ["./config.toml"]
    }
    wiki ResearchWiki {
        topic: "search_research",
        raw_path: "./wiki/raw",
        wiki_path: "./wiki/pages"
    }
    wiki agent Curator {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        wikis: [ResearchWiki]
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 5: Backward Compatibility — Contextual Keywords
// ═══════════════════════════════════════════════════════════════

TEST(V140BackwardCompat, ContextualKeyword_wiki) {
  assert_compiles(R"NEAM(
    let wiki = "just a string";
    print(wiki);
  )NEAM");
}

TEST(V140BackwardCompat, V13StillWorks) {
  assert_compiles(R"NEAM(
    budget B { cost: 100.00, tokens: 1000000 }
    program P {
        mission: "test",
        success_criterion: "M",
        constraints: ["c"],
        process: ["i"]
    }
    metric_extractor M { direction: "higher_is_better", method: "regex", pattern: "x" }
    session_service S { backend: "inmemory", ttl: "1d" }
    research agent R {
        provider: "openai",
        model: "gpt-4o",
        budget: B,
        program: P,
        metric: M,
        experiment_log: S,
        mutable_artifacts: ["./x.py"]
    }
  )NEAM");
}

TEST(V140BackwardCompat, V12StillWorks) {
  assert_compiles(R"NEAM(
    plugin Logger { hooks: { before_agent: "observe" }, priority: 1 }
    session_service S { backend: "inmemory", ttl: "1h" }
    eval_test E { agent: "DS", input: "t", criteria: ["safety"], threshold: 0.9 }
    artifact_store A { backend: "filesystem", path: "./out" }
    stream_config SC { mode: "sse" }
    a2a_config AC { expose_as_server: true }
  )NEAM");
}

TEST(V140BackwardCompat, V11StillWorks) {
  assert_compiles(R"NEAM(
    knowledge_card K { type: "concept", term: "Test", definition: "A test", domain: "t.d", version: "1.0.0" }
    context_assembly C { target_agent: "A", cards: ["K"], max_context_tokens: 2000 }
    governance_rule G { trigger: "data_write", condition: "pii", action: { audit: "yes" } }
    blueprint BP { version: "1.0.0", agents: ["A"] }
  )NEAM");
}

TEST(V140BackwardCompat, V10OWASPStillWorks) {
  assert_compiles(R"NEAM(
    goal_integrity G { declared_objectives: ["test"] }
    circuit_breaker CB { failure_threshold: 3 }
    human_gate HG { approve_before: ["deploy"] }
    gateway API { auth: { method: "api_key" } }
  )NEAM");
}

TEST(V140BackwardCompat, V09DataAgentsStillWork) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    datascientist agent DS { provider: "openai", model: "gpt-4o", budget: B }
    causal agent CA { provider: "openai", model: "gpt-4o", budget: B }
  )NEAM");
}

TEST(V140BackwardCompat, ForgeAgentStillWorks) {
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
// PART 6: All-Versions Coexistence
// ═══════════════════════════════════════════════════════════════

TEST(V140Coexistence, AllVersionsTogether) {
  assert_compiles(R"NEAM(
    budget B { cost: 1000.00, tokens: 10000000 }

    // v0.6: Basic agent
    agent Helper { provider: "openai", model: "gpt-4o-mini", system: "You help." }

    // v1.0: OWASP
    goal_integrity Goals { declared_objectives: ["secure ops"] }
    circuit_breaker CB { failure_threshold: 3 }

    // v1.1: NeamOS Foundation
    knowledge_card Churn { type: "concept", term: "Churn", definition: "No txn 90d", domain: "t.r", version: "1.0.0" }
    governance_rule PDPA { trigger: "data_write", condition: "pii", action: { audit: "yes" } }
    blueprint BP { version: "1.0.0", agents: ["DS"] }

    // v1.2: NeamProd
    plugin Logger { hooks: { before_agent: "observe" }, priority: 1 }
    session_service Sessions { backend: "inmemory", ttl: "24h" }
    artifact_store Artifacts { backend: "filesystem", path: "./out" }
    stream_config Stream { mode: "sse" }

    // v1.3: NeamLab Research Agent
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

    // v1.4: NeamWiki
    wiki PlatformWiki {
        topic: "platform_research",
        raw_path: "./wiki/raw",
        wiki_path: "./wiki/pages",
        page_types: ["source", "entity", "concept", "synthesis"],
        contradiction_detection: true,
        auto_extract_entities: true
    }
    wiki agent PlatformCurator {
        provider: "anthropic",
        model: "claude-opus-4",
        budget: B,
        wikis: [PlatformWiki],
        operations: ["ingest", "query", "lint", "graph"],
        knowledge_cards: [Churn],
        governance: [PDPA],
        plugin_hooks: [Logger]
    }

    // v0.9 Data Intelligence
    datascientist agent DS { provider: "openai", model: "gpt-4o", budget: B }

    // v1.0 Special Agents
    securitysentinel agent Sentinel { provider: "openai", model: "gpt-4o", budget: B, monitors: { all: true } }

    print("v0.6 + v0.9 + v1.0 + v1.1 + v1.2 + v1.3 + v1.4 all coexist");
  )NEAM");
}

}  // namespace
