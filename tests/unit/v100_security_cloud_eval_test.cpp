//
// Neam v1.0 — Security, Cloud, RAG, Evaluation & Special Agents Unit Tests
// Tests ALL 21 new v1.0 constructs + backward compatibility + performance
//

#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include <string>
#include <chrono>

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
// PART 1: OWASP Security Constructs (ASI01-ASI10)
// ═══════════════════════════════════════════════════════════════

TEST(V100Security, GoalIntegrity) {
  assert_compiles(R"(
    goal_integrity TestGoal {
        declared_objectives: ["predict churn", "identify drivers"],
        verification: { method: "semantic_similarity", threshold: 0.75 },
        on_drift: "halt_and_escalate",
        audit: true
    }
  )");
}

TEST(V100Security, ToolValidator) {
  assert_compiles(R"(
    tool_validator StrictTV {
        schema_enforcement: "strict",
        additional_properties: false,
        rate_limits: { per_agent: 100, per_tool: { deploy: 3 }, per_phase: 50 },
        max_call_depth: 5,
        detect_cycles: true
    }
  )");
}

TEST(V100Security, AgentIdentity) {
  assert_compiles(R"(
    agent_identity EphID {
        credential_mode: "ephemeral",
        ttl: "15m",
        rotation: "per_phase",
        scope: { datascientist: ["read:ml_features"], mlops: ["write:deployments"] },
        session_binding: true
    }
  )");
}

TEST(V100Security, SupplyChainPolicy) {
  assert_compiles(R"(
    supply_chain_policy SecChain {
        agent_md_signing: { algorithm: "ed25519", verify_on_load: true },
        tool_pinning: { method: "sha256", pin_descriptions: true },
        mcp_verification: { require_signed_cards: true },
        aibom: { format: "cyclonedx", auto_generate: true }
    }
  )");
}

TEST(V100Security, CodeSandbox) {
  assert_compiles(R"(
    code_sandbox Sandbox {
        runtime: "container",
        filesystem: { read_only: ["/data"], writable: ["/tmp"] },
        network: { allow_outbound: false },
        resources: { max_cpu: "1 core", max_memory: "512MB" }
    }
  )");
}

TEST(V100Security, MemoryIntegrity) {
  assert_compiles(R"(
    memory_integrity MemGuard {
        hash_algorithm: "sha256",
        verify_on_read: true,
        verify_on_write: true,
        provenance: { track_author: true, immutable_after: "24h" }
    }
  )");
}

TEST(V100Security, MessageSecurity) {
  assert_compiles(R"(
    message_security MsgSec {
        signing: { algorithm: "ecdsa_p256", sign_all_messages: true, include_nonce: true },
        encryption: { algorithm: "aes_256_gcm", key_exchange: "ecdh" },
        authentication: { verify_sender_identity: true }
    }
  )");
}

TEST(V100Security, CircuitBreaker) {
  assert_compiles(R"(
    circuit_breaker CB {
        failure_threshold: 3,
        success_threshold: 5,
        half_open_timeout: "30s",
        isolation: { scope: "per_agent", propagation: "block" }
    }
  )");
}

TEST(V100Security, HumanGate) {
  assert_compiles(R"(
    human_gate HG {
        approve_before: ["deploy", "delete", "transfer_data"],
        confidence_escalation: { below_threshold: 0.7 },
        workflow: { channel: "slack", timeout: "15m", default_on_timeout: "deny" }
    }
  )");
}

TEST(V100Security, AgentAttestation) {
  assert_compiles(R"(
    agent_attestation AA {
        attest_interval: "5m",
        baseline: { method: "welford_online", sigma_threshold: 3.0 },
        kill_switch: { api_enabled: true, auto_kill_on: ["budget_exhausted"] },
        collusion_detection: { max_mutual_reinforcement: 3 }
    }
  )");
}

TEST(V100Security, AllSecurityConstructsCoexist) {
  assert_compiles(R"(
    goal_integrity G { objectives: ["test"] }
    tool_validator TV { schema_enforcement: "strict" }
    agent_identity AI { credential_mode: "ephemeral", ttl: "15m" }
    supply_chain_policy SC { agent_md_signing: { verify: true } }
    code_sandbox CS { runtime: "container" }
    memory_integrity MI { hash_algorithm: "sha256" }
    message_security MS { signing: { algorithm: "ecdsa_p256" } }
    circuit_breaker CB { failure_threshold: 3 }
    human_gate HG { approve_before: ["deploy"] }
    agent_attestation AA { attest_interval: "5m" }
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 2: MCP Security
// ═══════════════════════════════════════════════════════════════

TEST(V100MCP, MCPAllowlist) {
  assert_compiles(R"(
    mcp_allowlist Servers {
        servers: [{ url: "https://fs.example.com", fingerprint: "sha256:abc" }],
        block_unlisted: true,
        alert_on_new: true
    }
  )");
}

TEST(V100MCP, ToolPinning) {
  assert_compiles(R"(
    tool_pinning Pins {
        method: "sha256",
        pin_descriptions: true,
        block_on_change: true
    }
  )");
}

TEST(V100MCP, ContextGuard) {
  assert_compiles(R"(
    context_guard CG {
        compartmentalize: true,
        cross_task_sharing: "none",
        max_context_age: "1h",
        purge_on_completion: true
    }
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 3: AIBOM, Evaluation, Cloud
// ═══════════════════════════════════════════════════════════════

TEST(V100AIBOM, AIBOMConfig) {
  assert_compiles(R"(
    aibom_config BOM {
        format: "cyclonedx",
        version: "1.6",
        components: { models: true, agents: true },
        auto_generate: true,
        trigger: "on_build"
    }
  )");
}

TEST(V100Eval, GymEvaluator) {
  assert_compiles(R"(
    gym_evaluator GE {
        mode: "unit",
        agent: "./test.neamb",
        dataset: "./test.jsonl",
        graders: { primary: "llm_judge", judge: { provider: "openai", model: "gpt-4o-mini" } },
        thresholds: { pass_rate: 0.85, exit_on_fail: true },
        reproducibility: { repetitions: 5, seed: 42 }
    }
  )");
}

TEST(V100Cloud, Gateway) {
  assert_compiles(R"(
    gateway GW {
        auth: { method: "oauth2", issuer: "https://auth.example.com" },
        rate_limit: { per_api_key: 1000, per_ip: 100 },
        routes: { tasks: "/tasks", health: "/health" },
        observability: { metrics: { format: "prometheus" } }
    }
  )");
}

TEST(V100Cloud, ModelRouter) {
  assert_compiles(R"(
    model_router MR {
        strategy: "cost_optimized",
        routes: { simple: "haiku", reasoning: "sonnet", complex: "opus" },
        fallback_chain: ["anthropic", "openai", "ollama"],
        budget: { per_request_max: 0.50, daily_max: 100.00 }
    }
  )");
}

TEST(V100Cloud, Marketplace) {
  assert_compiles(R"(
    marketplace MP {
        package_format: { manifest: "skill.neam.json", signature: "ed25519" },
        publish_requires: ["signature", "tests_passing"],
        install_policy: { verify_signature: true }
    }
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 4: Special Agents (3 new types)
// ═══════════════════════════════════════════════════════════════

TEST(V100Agents, SecuritySentinel) {
  assert_compiles(R"(
    budget B { cost: 100.00, tokens: 1000000 }
    securitysentinel agent Sentinel {
        provider: "openai", model: "gpt-4o", budget: B,
        monitors: { goal_integrity: { check: "per_phase" }, behavioral_anomaly: { sigma: 3.0 } },
        actions: { on_anomaly: "alert", on_critical: "kill_switch" }
    }
  )");
}

TEST(V100Agents, ProtocolBridge) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    protocolbridge agent Bridge {
        provider: "openai", model: "gpt-4o", budget: B,
        protocols: { mcp: { servers: ["filesystem"] }, a2a: { peers: ["external"] } },
        firewall: { allowed_actions: ["read"], blocked_actions: ["delete"] }
    }
  )");
}

TEST(V100Agents, CostGuardian) {
  assert_compiles(R"(
    budget B { cost: 20.00, tokens: 200000 }
    costguardian agent CostOps {
        provider: "ollama", model: "llama3:8b", budget: B,
        tracking: { per_agent: true, per_phase: true },
        optimization: { model_downgrade: { enabled: true } },
        alerts: { budget_warning: 0.75, budget_critical: 0.90 }
    }
  )");
}

TEST(V100Agents, AllSpecialAgentsCoexist) {
  assert_compiles(R"(
    budget B { cost: 100.00, tokens: 1000000 }
    securitysentinel agent S { provider: "openai", model: "gpt-4o", budget: B, monitors: { all: true } }
    protocolbridge agent P { provider: "openai", model: "gpt-4o", budget: B, protocols: { mcp: true } }
    costguardian agent C { provider: "ollama", model: "llama3:8b", budget: B, tracking: { all: true } }
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 5: Contextual Keywords as Variables
// ═══════════════════════════════════════════════════════════════

TEST(V100Keywords, AllV10KeywordsAsVariables) {
  assert_compiles(R"(
    let goal_integrity = "variable";
    let tool_validator = 42;
    let agent_identity = true;
    let supply_chain_policy = "var";
    let code_sandbox = 3.14;
    let memory_integrity = "var";
    let message_security = false;
    let circuit_breaker = 99;
    let human_gate = "var";
    let agent_attestation = "var";
    let mcp_allowlist = 0;
    let tool_pinning = "var";
    let context_guard = 1;
    let aibom_config = "var";
    let gym_evaluator = "var";
    let gateway = "var";
    let model_router = "var";
    let marketplace = "var";
    let securitysentinel = "not agent";
    let protocolbridge = "not agent";
    let costguardian = "not agent";
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 6: Cross-Version Compatibility
// ═══════════════════════════════════════════════════════════════

TEST(V100Compat, V098AgentsWithV10Security) {
  assert_compiles(R"(
    budget B { cost: 50.00, tokens: 500000 }
    goal_integrity G { objectives: ["secure prediction"] }
    circuit_breaker CB { failure_threshold: 3 }
    human_gate HG { approve_before: ["deploy"] }
    datascientist agent DS { provider: "openai", model: "gpt-4o", budget: B }
    causal agent CA { provider: "openai", model: "o3-mini", budget: B }
    mlops agent MO { provider: "openai", model: "gpt-4o", budget: B }
  )");
}

TEST(V100Compat, V099DIOWithV10Constructs) {
  assert_compiles(R"(
    budget B { cost: 500.00, tokens: 2000000 }
    goal_integrity G { objectives: ["full lifecycle"] }
    tool_validator TV { schema_enforcement: "strict" }
    agent_attestation AA { attest_interval: "5m" }
    gateway GW { routes: { health: "/health" } }
    model_router MR { strategy: "cost_optimized" }
    securitysentinel agent Sentinel { provider: "openai", model: "gpt-4o", budget: B, monitors: { all: true } }
    infrastructure_profile Infra { data_warehouse: { platform: "postgres" } }
    dio agent DIO { mode: "config", task: "secure orchestration", infrastructure: Infra, provider: "openai", model: "gpt-4o", budget: B }
  )");
}

TEST(V100Compat, AllAgentTypesCoexist) {
  assert_compiles(R"(
    budget B { cost: 10.00, tokens: 100000 }
    agent Legacy { provider: "openai", model: "gpt-4o-mini", system: "helper", budget: B }
    source S { type: "postgres", connection: "pg://localhost" }
    sink K { type: "s3", connection: "s3://bucket/" }
    data agent DA { provider: "openai", model: "gpt-4o-mini", sources: [S], sinks: [K] }
    compute WH { engine: "snowflake" }
    etl agent EA { provider: "openai", model: "gpt-4o", sources: [S], warehouse: WH }
    sql_connection DB { platform: "postgres", connection: "pg://localhost/test", database: "test" }
    analyst agent AA { provider: "openai", model: "gpt-4o-mini", connections: [DB], budget: B }
    datascientist agent DS { provider: "openai", model: "gpt-4o", budget: B }
    causal agent CA { provider: "openai", model: "o3-mini", budget: B }
    mlops agent MO { provider: "openai", model: "gpt-4o", budget: B }
    databa agent BA { provider: "openai", model: "gpt-4o", budget: B }
    datatest agent QA { provider: "openai", model: "gpt-4o", budget: B }
    dio agent DIO { mode: "auto", task: "test", provider: "openai", model: "gpt-4o", budget: B }
    securitysentinel agent Sent { provider: "openai", model: "gpt-4o", budget: B, monitors: { all: true } }
    protocolbridge agent Br { provider: "openai", model: "gpt-4o", budget: B, protocols: { mcp: true } }
    costguardian agent CG { provider: "ollama", model: "llama3:8b", budget: B, tracking: { all: true } }
  )");
}

// ═══════════════════════════════════════════════════════════════
// PART 7: Performance
// ═══════════════════════════════════════════════════════════════

TEST(V100Performance, CompileAllConstructsUnder50ms) {
  auto start = std::chrono::high_resolution_clock::now();
  assert_compiles(R"(
    budget B { cost: 500.00, tokens: 2000000 }
    goal_integrity G { objectives: ["test"] }
    tool_validator TV { schema_enforcement: "strict" }
    agent_identity AI { credential_mode: "ephemeral" }
    supply_chain_policy SC { agent_md_signing: { verify: true } }
    code_sandbox CS { runtime: "container" }
    memory_integrity MI { hash_algorithm: "sha256" }
    message_security MS { signing: { algorithm: "ecdsa_p256" } }
    circuit_breaker CB { failure_threshold: 3 }
    human_gate HG { approve_before: ["deploy"] }
    agent_attestation AA { attest_interval: "5m" }
    mcp_allowlist MA { servers: ["https://fs.example.com"] }
    tool_pinning TP { method: "sha256" }
    context_guard CG { compartmentalize: true }
    aibom_config AC { format: "cyclonedx" }
    gym_evaluator GE { mode: "unit", agent: "./test.neamb" }
    gateway GW { routes: { health: "/health" } }
    model_router MR { strategy: "cost_optimized" }
    marketplace MP { package_format: { signature: "ed25519" } }
    securitysentinel agent S { provider: "openai", model: "gpt-4o", budget: B, monitors: { all: true } }
    protocolbridge agent P { provider: "openai", model: "gpt-4o", budget: B, protocols: { mcp: true } }
    costguardian agent C { provider: "ollama", model: "llama3:8b", budget: B, tracking: { all: true } }
    datascientist agent DS { provider: "openai", model: "gpt-4o", budget: B }
    causal agent CA { provider: "openai", model: "o3-mini", budget: B }
    infrastructure_profile I { data_warehouse: { platform: "postgres" } }
    dio agent DIO { mode: "config", task: "perf test", infrastructure: I, provider: "openai", model: "gpt-4o", budget: B }
  )");
  auto end = std::chrono::high_resolution_clock::now();
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  EXPECT_LT(ms, 50) << "Compilation took " << ms << "ms, expected < 50ms";
}

} // namespace
