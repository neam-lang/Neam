//
// Neam v1.0 — OWASP Security Runtime Types
// ASI01-ASI10 + MCP01-MCP10 compliance constructs
//

#pragma once

#include "neamc/vm/object.hpp"
#include <string>
#include <unordered_map>
#include <mutex>

namespace neamc::vm {

// ═══ ASI01: Goal Integrity (Goal Hijack Defense) ═══
struct ObjGoalIntegrity : Obj {
    std::string name;
    std::string objectives_json;          // declared objectives array
    std::string verification_json;        // {method, threshold, check_frequency, on_drift}
    std::string input_guard_ref;
    std::string output_guard_ref;
    bool audit = true;
    // Runtime state
    mutable double last_drift_score = 0.0;
    mutable std::string last_check_result = "unchecked";
};

// ═══ ASI02: Tool Validator (Tool Misuse Defense) ═══
struct ObjToolValidator : Obj {
    std::string name;
    std::string schema_enforcement;       // "strict" | "permissive"
    bool additional_properties = false;
    std::string rate_limits_json;         // {per_agent, per_tool, per_phase}
    int max_call_depth = 5;
    bool detect_cycles = true;
    std::string budget_per_call_json;
    // Runtime state
    mutable int calls_this_phase = 0;
    mutable std::unordered_map<std::string, int> per_tool_calls;
    mutable std::mutex state_mutex;
};

// ═══ ASI03: Agent Identity (Privilege Abuse Defense) ═══
struct ObjAgentIdentity : Obj {
    std::string name;
    std::string credential_mode;          // "ephemeral" | "persistent"
    std::string ttl;                      // "15m", "1h"
    std::string rotation;                 // "per_phase" | "per_invocation"
    std::string scope_json;               // {agent: [permissions]}
    bool session_binding = true;
    bool cross_agent_sharing = false;
    bool attest_on_startup = true;
    // Runtime state
    mutable std::string current_token;
    mutable std::string token_expiry;
};

// ═══ ASI04: Supply Chain Policy ═══
struct ObjSupplyChainPolicy : Obj {
    std::string name;
    std::string agent_md_signing_json;
    std::string tool_pinning_json;
    std::string mcp_verification_json;
    std::string aibom_json;
};

// ═══ ASI05: Code Sandbox ═══
struct ObjCodeSandbox : Obj {
    std::string name;
    std::string runtime;                  // "container" | "wasm" | "gvisor"
    std::string filesystem_json;
    std::string network_json;
    std::string resources_json;
    std::string pre_execution_review_json;
    bool log_all_executions = true;
};

// ═══ ASI06: Memory Integrity ═══
struct ObjMemoryIntegrity : Obj {
    std::string name;
    std::string hash_algorithm;           // "sha256" | "blake3"
    bool verify_on_read = true;
    bool verify_on_write = true;
    std::string provenance_json;
    std::string access_guard_json;
    std::string integrity_scan_json;
};

// ═══ ASI07: Message Security ═══
struct ObjMessageSecurity : Obj {
    std::string name;
    std::string signing_json;
    std::string encryption_json;
    std::string authentication_json;
    bool log_all_messages = true;
};

// ═══ ASI08: Circuit Breaker ═══
struct ObjCircuitBreakerDecl : Obj {
    std::string name;
    int failure_threshold = 3;
    int success_threshold = 5;
    std::string half_open_timeout;
    std::string isolation_json;
    // Runtime state
    mutable int consecutive_failures = 0;
    mutable int consecutive_successes = 0;
    mutable std::string state = "closed";  // "closed" | "open" | "half_open"
    mutable std::mutex state_mutex;
};

// ═══ ASI09: Human Gate ═══
struct ObjHumanGate : Obj {
    std::string name;
    std::string approve_before_json;
    std::string confidence_escalation_json;
    std::string workflow_json;
    bool log_all_decisions = true;
};

// ═══ ASI10: Agent Attestation ═══
struct ObjAgentAttestation : Obj {
    std::string name;
    std::string attest_interval;
    std::string baseline_json;
    std::string kill_switch_json;
    std::string collusion_detection_json;
    // Runtime state
    mutable double last_anomaly_score = 0.0;
    mutable std::string last_attestation;
};

// ═══ MCP Security ═══
struct ObjMCPAllowlist : Obj {
    std::string name;
    std::string servers_json;
    bool block_unlisted = true;
    bool alert_on_new = true;
};

struct ObjToolPinning : Obj {
    std::string name;
    std::string method;                   // "sha256"
    bool pin_descriptions = true;
    bool block_on_change = true;
    mutable std::unordered_map<std::string, std::string> tool_hashes;
};

struct ObjContextGuard : Obj {
    std::string name;
    bool compartmentalize = true;
    std::string cross_task_sharing;
    std::string max_context_age;
    bool purge_on_completion = true;
};

// ═══ AIBOM ═══
struct ObjAIBOMConfig : Obj {
    std::string name;
    std::string format;                   // "cyclonedx"
    std::string version;                  // "1.6"
    std::string components_json;
    std::string provenance_json;
    bool auto_generate = true;
    std::string trigger;
    std::string output_path;
    std::string eu_ai_act_json;
};

// ═══ Cloud Stack ═══
struct ObjGateway : Obj {
    std::string name;
    std::string auth_json;
    std::string rate_limit_json;
    std::string routes_json;
    std::string cors_json;
    std::string observability_json;
    mutable int requests_served = 0;
    mutable bool running = false;
};

struct ObjModelRouter : Obj {
    std::string name;
    std::string strategy;
    std::string routes_json;
    std::string fallback_chain_json;
    std::string budget_json;
    mutable double daily_cost = 0.0;
};

struct ObjMarketplaceDecl : Obj {
    std::string name;
    std::string package_format_json;
    std::string publish_requires_json;
    std::string install_policy_json;
};

// ═══ Evaluation ═══
struct ObjGymEvaluator : Obj {
    std::string name;
    std::string mode;
    std::string agent_path;
    std::string dataset_path;
    std::string graders_json;
    std::string thresholds_json;
    std::string reproducibility_json;
    std::string metrics_json;
    std::string output_path;
    // Runtime
    mutable int total_runs = 0;
    mutable int passed_runs = 0;
    mutable double avg_score = 0.0;
    mutable double total_cost_usd = 0.0;
};

// ═══ Special Agents ═══
struct ObjSecuritySentinelAgent : Obj {
    std::string name;
    std::string provider;
    std::string llm_model;
    double temperature = 0.2;
    std::string budget_ref;
    std::string monitors_json;
    std::string actions_json;
    std::string reporting_json;
    mutable std::string status = "initialized";
    mutable int anomalies_detected = 0;
    mutable int kills_triggered = 0;
};

struct ObjProtocolBridgeAgent : Obj {
    std::string name;
    std::string provider;
    std::string llm_model;
    double temperature = 0.2;
    std::string budget_ref;
    std::string protocols_json;
    std::string firewall_json;
    mutable std::string status = "initialized";
};

struct ObjCostGuardianAgent : Obj {
    std::string name;
    std::string provider;
    std::string llm_model;
    double temperature = 0.2;
    std::string budget_ref;
    std::string tracking_json;
    std::string optimization_json;
    std::string alerts_json;
    mutable std::string status = "initialized";
    mutable double tracked_cost = 0.0;
};

} // namespace neamc::vm
