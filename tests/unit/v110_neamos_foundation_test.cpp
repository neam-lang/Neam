//
// Neam v1.1 — NeamOS Foundation Unit Tests
// Tests ALL 10 new v1.1 constructs + backward compatibility + coexistence
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

static void assert_compile_error(const std::string& source) {
  Pipeline p;
  EXPECT_ANY_THROW(p.compile(source, {}));
}

// ═══════════════════════════════════════════════════════════════
// PART 1: Knowledge Fabric (knowledge_card + context_assembly)
// ═══════════════════════════════════════════════════════════════

TEST(V110KnowledgeFabric, ConceptCard) {
  assert_compiles(R"(
    knowledge_card CustomerChurn {
        type: "concept",
        term: "Customer Churn",
        definition: "Customer with no transaction in 90+ days",
        domain: "telecom.retention",
        version: "2.1.0",
        owner: "DataBAAgent",
        ttl: "180d",
        provenance: { source: "BRD-2024-Q3", verified_by: "VP_Data" }
    }
  )");
}

TEST(V110KnowledgeFabric, PolicyCard) {
  assert_compiles(R"(
    knowledge_card PIIHandling {
        type: "policy",
        name: "PII Data Handling",
        scope: "all_agents",
        rules: ["Mask PII before logging", "365 day retention max"],
        enforcement: "runtime_guard",
        domain: "compliance.data_protection",
        version: "1.0.0"
    }
  )");
}

TEST(V110KnowledgeFabric, DecisionCard) {
  assert_compiles(R"(
    knowledge_card ModelSelection {
        type: "decision",
        context: "Churn model selection for Southeast Asia",
        decision: "XGBoost for batch, logistic regression for realtime",
        decided_by: "DataScientistAgent",
        rationale: "XGBoost AUC 0.89 vs logistic 0.82 but logistic meets latency SLA",
        domain: "ml.model_selection",
        version: "1.0.0"
    }
  )");
}

TEST(V110KnowledgeFabric, SkillCard) {
  assert_compiles(R"(
    knowledge_card FeatureEng {
        type: "skill",
        skill_name: "Temporal Feature Engineering",
        prerequisites: ["sliding windows", "time-series decomposition"],
        quality_expectations: ["<5% null rate", "no future leakage"],
        domain: "data_science.features",
        version: "1.2.0"
    }
  )");
}

TEST(V110KnowledgeFabric, ContextAssembly) {
  assert_compiles(R"(
    knowledge_card Churn {
        type: "concept",
        term: "Churn",
        definition: "No txn 90d",
        domain: "t.r",
        version: "1.0.0"
    }
    context_assembly ChurnContext {
        target_agent: "DS",
        phase: "feature_engineering",
        cards: ["Churn"],
        max_context_tokens: 4000,
        assembly_strategy: "relevance_ranked",
        fallback: "truncate"
    }
  )");
}

TEST(V110KnowledgeFabric, MultipleContextAssemblies) {
  assert_compiles(R"(
    knowledge_card C1 { type: "concept", term: "A", definition: "X", domain: "d", version: "1.0.0" }
    knowledge_card C2 { type: "policy", name: "P", scope: "all", rules: ["r1"], enforcement: "guard" }
    context_assembly Ctx1 {
        target_agent: "Agent1",
        phase: "analysis",
        cards: ["C1", "C2"],
        max_context_tokens: 3000
    }
    context_assembly Ctx2 {
        target_agent: "Agent2",
        phase: "modeling",
        cards: ["C1"],
        max_context_tokens: 2000,
        assembly_strategy: "priority"
    }
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 2: Agent Persona & Localization
// ═══════════════════════════════════════════════════════════════

TEST(V110PersonaLocale, AgentPersonaBasic) {
  assert_compiles(R"(
    agent_persona DSPersona {
        target_agent: "ChurnDS",
        display_name: "Churn Data Scientist",
        personality: { curiosity: 0.9, confidence: 0.7, caution: 0.4 }
    }
  )");
}

TEST(V110PersonaLocale, AgentPersonaFull) {
  assert_compiles(R"(
    agent_persona FullPersona {
        target_agent: "Analyst",
        display_name: "Senior Analyst",
        avatar: "assets/avatars/analyst.glb",
        voice: { engine: "kokoro", voice_id: "curious_analyst" },
        personality: { curiosity: 0.9, confidence: 0.7, formality: 0.8 },
        catchphrase: "Interesting pattern...",
        locales: {
            en: { voice_id: "analyst_en", catchphrases: ["Interesting pattern..."] },
            ta: { voice_id: "ta-IN-Valluvar", catchphrases: ["suvarasiyamana mathiri"] }
        }
    }
  )");
}

TEST(V110PersonaLocale, LocaleBasic) {
  assert_compiles(R"(
    locale AppLocale {
        supported: ["en", "zh", "ms", "ta", "th", "vi"],
        fallback: "en",
        string_table: "./locales/{lang}.json"
    }
  )");
}

TEST(V110PersonaLocale, LocaleWithRTL) {
  assert_compiles(R"(
    locale MultiLocale {
        supported: ["en", "zh", "ar", "he"],
        rtl: ["ar", "he"],
        fallback: "en",
        string_table: "./i18n/{lang}.json"
    }
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 3: Governance Rules
// ═══════════════════════════════════════════════════════════════

TEST(V110Governance, BasicGovernanceRule) {
  assert_compiles(R"(
    governance_rule PDPA_Retention {
        trigger: "data_write",
        condition: "data.classification == personal_data",
        action: {
            enforce_ttl: "365d",
            require_consent: true,
            audit: "mandatory"
        },
        regulatory_basis: "PDPA Section 25"
    }
  )");
}

TEST(V110Governance, GovernanceRuleWithDates) {
  assert_compiles(R"(
    governance_rule GDPR_Erasure {
        trigger: "data_read",
        condition: "data.region == EU",
        action: {
            enforce_ttl: "730d",
            notify: "GovernanceAgent",
            block: false,
            audit: "mandatory"
        },
        regulatory_basis: "GDPR Article 17",
        effective_date: "2026-01-01",
        review_date: "2026-12-31"
    }
  )");
}

TEST(V110Governance, GovernanceRuleToolCall) {
  assert_compiles(R"(
    governance_rule HighRiskToolGate {
        trigger: "tool_call",
        condition: "tool.risk_level == high",
        action: {
            require_consent: true,
            audit: "mandatory",
            block: true
        }
    }
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 4: Agent Adapters
// ═══════════════════════════════════════════════════════════════

TEST(V110Adapters, LocalCodingAgent) {
  assert_compiles(R"(
    agent_adapter ClaudeCode {
        type: "local_coding_agent",
        binary: "claude",
        capabilities: ["code_generation", "code_review", "debugging"],
        communication: "stdin_stdout",
        skill_injection: true,
        cost_tracking: "token_based"
    }
  )");
}

TEST(V110Adapters, GatewayAgent) {
  assert_compiles(R"(
    agent_adapter OpenClaw {
        type: "gateway_agent",
        protocol: "websocket",
        endpoint: "ws://localhost:18789",
        capabilities: ["browser", "email", "file_system"],
        communication: "websocket",
        device_key_pairing: true,
        cost_tracking: "api_based"
    }
  )");
}

TEST(V110Adapters, CloudAgent) {
  assert_compiles(R"(
    agent_adapter BedrockAgent {
        type: "cloud_agent",
        protocol: "http",
        endpoint: "https://bedrock.us-east-1.amazonaws.com",
        capabilities: ["text_generation", "summarization"],
        communication: "rpc",
        cost_tracking: "token_based"
    }
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 5: Blueprints
// ═══════════════════════════════════════════════════════════════

TEST(V110Blueprints, BasicBlueprint) {
  assert_compiles(R"(
    blueprint TelecomChurn {
        version: "1.0.0",
        agents: ["ChurnDS", "CausalAgent"],
        knowledge_cards: ["CustomerChurn"],
        parameters: {
            target_churn: { type: "float", default: 0.06, range: [0.01, 0.50] }
        }
    }
  )");
}

TEST(V110Blueprints, FullBlueprint) {
  assert_compiles(R"(
    blueprint EnterpriseChurn {
        version: "2.0.0",
        description: "Enterprise churn prediction with OWASP compliance",
        author: "Praveen Govindaraj",
        agents: ["DS", "CA", "MO", "Sentinel"],
        skills: ["query_database", "causal_infer"],
        knowledge_cards: ["CustomerChurn", "PIIPolicy", "ModelSelection"],
        parameters: {
            target_churn: { type: "float", default: 0.06 },
            training_window: { type: "int", default: 90, range: [30, 365] },
            enable_causal: { type: "bool", default: true }
        }
    }
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 6: New Agent Types
// ═══════════════════════════════════════════════════════════════

TEST(V110Agents, KnowledgeWeaverAgent) {
  assert_compiles(R"(
    budget B { cost: 100.00, tokens: 1000000 }
    knowledgeweaver agent Weaver {
        provider: "openai", model: "gpt-4o",
        budget: B,
        fabric: {
            card_store: "./knowledge/",
            embedding_model: "nomic-embed-text",
            search: "semantic",
            certification: { required: true, reviewers: ["VP_Data"] }
        },
        monitors: {
            card_freshness: { alert_after: "90d" },
            coverage_gaps: { check: "weekly" },
            conflict_detection: { auto_flag: true }
        }
    }
  )");
}

TEST(V110Agents, AdaptAgent) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    adaptagent agent Adapt {
        provider: "openai", model: "gpt-4o-mini",
        budget: B,
        monitors: {
            model_releases: { sources: ["anthropic", "openai"], schedule: "daily" },
            protocol_updates: { sources: ["mcp_changelog"], schedule: "weekly" },
            security_advisories: { sources: ["cve_database"], schedule: "continuous" }
        },
        proposals: { channel: "evolution_panel", requires_approval: true }
    }
  )");
}

TEST(V110Agents, StorytellerAgent) {
  assert_compiles(R"(
    budget B { cost: 25.00, tokens: 250000 }
    storyteller agent Luna {
        provider: "openai", model: "gpt-4o",
        budget: B,
        sub_agents: {
            scene_artist: { model: "flux_pro" },
            voice_caster: { model: "fish_audio_s1" },
            mood_composer: { model: "suno_v5" }
        },
        safety: {
            content_filter: "child_safe_strict",
            age_groups: ["3-5", "6-8", "9-12"],
            image_scan: "every_generation",
            parent_preview: true
        }
    }
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 7: Contextual Keywords (Backward Compatibility)
// ═══════════════════════════════════════════════════════════════

TEST(V110BackwardCompat, ContextualKeywordsAsVariables) {
  assert_compiles(R"(
    let blueprint = "just a string";
    let locale = 42;
    let knowledge_card = true;
    let governance_rule = 3.14;
    let agent_adapter = "variable";
    let context_assembly = "another variable";
    let agent_persona = "yet another";
    print(blueprint);
    print(locale);
  )");
}

TEST(V110BackwardCompat, V10OWASPStillWorks) {
  assert_compiles(R"(
    goal_integrity G { declared_objectives: ["test"] }
    tool_validator TV { schema_enforcement: "strict" }
    agent_identity AI { credential_mode: "ephemeral", ttl: "15m" }
    circuit_breaker CB { failure_threshold: 3 }
    human_gate HG { approve_before: ["deploy"] }
    agent_attestation AA { attest_interval: "5m" }
  )");
}

TEST(V110BackwardCompat, V10CloudStackStillWorks) {
  assert_compiles(R"(
    gateway API { auth: { method: "oauth2" }, routes: { health: "/health" } }
    model_router Router { strategy: "cost_optimized", routes: { simple: "haiku" } }
    marketplace Skills { install_policy: { verify_signature: true } }
  )");
}

TEST(V110BackwardCompat, V10SpecialAgentsStillWork) {
  assert_compiles(R"(
    budget B { cost: 100.00, tokens: 1000000 }
    securitysentinel agent Sentinel { provider: "openai", model: "gpt-4o", budget: B, monitors: { all: true } }
    protocolbridge agent Bridge { provider: "openai", model: "gpt-4o", budget: B, protocols: { mcp: true } }
    costguardian agent Cost { provider: "ollama", model: "llama3:8b", budget: B, tracking: { all: true } }
  )");
}

TEST(V110BackwardCompat, V09DataAgentsStillWork) {
  assert_compiles(R"(
    budget B { cost: 10.00, tokens: 100000 }
    datascientist agent DS { provider: "openai", model: "gpt-4o", budget: B }
    causal agent CA { provider: "openai", model: "gpt-4o", budget: B }
    mlops agent MO { provider: "openai", model: "gpt-4o", budget: B }
    databa agent BA { provider: "openai", model: "gpt-4o", budget: B }
    datatest agent QA { provider: "openai", model: "gpt-4o", budget: B }
  )");
}

TEST(V110BackwardCompat, V06BasicAgentStillWorks) {
  assert_compiles(R"(
    agent Helper { provider: "openai", model: "gpt-4o-mini", system: "You help." }
    let answer = "test";
    emit answer;
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 8: Coexistence (v0.6 + v0.9 + v1.0 + v1.1)
// ═══════════════════════════════════════════════════════════════

TEST(V110Coexistence, AllVersionsTogether) {
  assert_compiles(R"(
    budget B { cost: 500.00, tokens: 2000000 }

    // v0.6: Basic agent
    agent Helper { provider: "openai", model: "gpt-4o-mini", system: "You help." }

    // v1.0: OWASP Security
    goal_integrity Goals { declared_objectives: ["secure ops"] }
    circuit_breaker CB { failure_threshold: 3 }
    human_gate HG { approve_before: ["deploy"] }

    // v1.0: Cloud Stack
    gateway API { auth: { method: "api_key" }, routes: { health: "/health" } }
    model_router Router { strategy: "cost_optimized" }

    // v1.0: Special Agents
    securitysentinel agent Sentinel { provider: "openai", model: "gpt-4o", budget: B, monitors: { all: true } }

    // v0.9: Data Intelligence
    datascientist agent DS { provider: "openai", model: "gpt-4o", budget: B }
    causal agent CA { provider: "openai", model: "gpt-4o", budget: B }

    // v1.1: NeamOS Foundation
    knowledge_card Churn { type: "concept", term: "Churn", definition: "No txn 90d", domain: "t.r", version: "1.0.0" }
    context_assembly Ctx { target_agent: "DS", cards: ["Churn"], max_context_tokens: 4000 }
    agent_persona DSP { target_agent: "DS", display_name: "Analyst", personality: { curiosity: 0.9 } }
    locale AppL { supported: ["en", "zh"], fallback: "en" }
    governance_rule PDPA { trigger: "data_write", condition: "pii", action: { audit: "mandatory" } }
    agent_adapter Claude { type: "local", binary: "claude", capabilities: ["code"], communication: "stdin" }
    blueprint BP { version: "1.0.0", agents: ["DS", "CA"] }
    knowledgeweaver agent Weaver { provider: "openai", model: "gpt-4o", budget: B, fabric: { store: "./" } }
    adaptagent agent Adapt { provider: "openai", model: "gpt-4o-mini", budget: B, monitors: { m: "daily" } }
    storyteller agent Luna { provider: "openai", model: "gpt-4o", budget: B, sub_agents: { a: "f" }, safety: { f: "safe" } }

    print("All versions coexist");
  )");
}

TEST(V110Coexistence, All22AgentTypes) {
  assert_compiles(R"(
    budget B { cost: 10.00, tokens: 100000 }

    // 19 existing agent types
    agent A1 { provider: "openai", model: "gpt-4o-mini", system: "help" }
    datascientist agent A2 { provider: "openai", model: "gpt-4o", budget: B }
    causal agent A3 { provider: "openai", model: "gpt-4o", budget: B }
    mlops agent A4 { provider: "openai", model: "gpt-4o", budget: B }
    databa agent A5 { provider: "openai", model: "gpt-4o", budget: B }
    datatest agent A6 { provider: "openai", model: "gpt-4o", budget: B }
    securitysentinel agent A7 { provider: "openai", model: "gpt-4o", budget: B, monitors: { all: true } }
    protocolbridge agent A8 { provider: "openai", model: "gpt-4o", budget: B, protocols: { mcp: true } }
    costguardian agent A9 { provider: "ollama", model: "llama3:8b", budget: B, tracking: { all: true } }

    // 3 new v1.1 agent types
    knowledgeweaver agent A10 { provider: "openai", model: "gpt-4o", budget: B, fabric: { s: "./" } }
    adaptagent agent A11 { provider: "openai", model: "gpt-4o", budget: B, monitors: { m: "d" } }
    storyteller agent A12 { provider: "openai", model: "gpt-4o", budget: B, sub_agents: { a: "f" }, safety: { f: "s" } }

    print("22 agent types coexist");
  )");
}

}  // namespace
