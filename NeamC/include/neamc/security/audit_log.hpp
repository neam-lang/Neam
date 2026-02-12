//
// Neam v0.6.9 — Structured Audit Logging (D1)
//
// Foundation module for OWASP-aligned security monitoring.
// All security domains (D2-D10) emit events through this logger.
//

#pragma once

#include <chrono>
#include <fstream>
#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace neamc::security {

// Event types covering all 10 OWASP Agentic risks
enum class EventType {
  AuthSuccess,        // D7: credential lifecycle
  AuthFailure,        // D7: brute-force detection
  AgentInvoke,        // D9: behavioral baseline
  ToolCall,           // D2: permission enforcement
  ToolResult,         // D2: result validation
  GuardBlock,         // D3: guard blocked input/output
  GuardPass,          // D3: guard allowed input/output
  BudgetConsume,      // D5: rate/cost tracking
  BudgetExhausted,    // D5: limit enforcement
  PolicyDeny,         // D2: permission denial
  PolicyConfirm,      // D10: human-in-the-loop
  InjectionDetected,  // D3: prompt injection
  SSRFBlocked,        // D4: network protection
  RateLimitHit,       // D5: throttling
  MCPRequest,         // D6: supply chain audit
  MCPResponse,        // D6: supply chain audit
  AnomalyDetected,    // D9: behavioral monitoring
  AgentDisabled,      // D9: kill switch
  ConfirmRequest,     // D10: HITL
  ConfirmResult,      // D10: HITL
  Error               // catch-all
};

// Audit output destination
enum class AuditSink {
  Stderr,      // default: human-readable to stderr
  File,        // append to file (JSONL)
  JsonStdout   // structured JSON to stdout (for log aggregators)
};

struct AuditEvent {
  std::string timestamp;    // ISO 8601
  std::string trace_id;     // propagated from HTTP request
  EventType event_type;
  std::string agent;        // agent name (if applicable)
  std::string tool;         // tool name (if applicable)
  std::string detail;       // human-readable summary
  nlohmann::json metadata;  // arbitrary k/v pairs
};

class AuditLogger {
public:
  static AuditLogger& instance();

  void configure(AuditSink sink, const std::string& file_path = "");
  void log(const AuditEvent& event);

  // Convenience methods for common security events
  void log_auth(const std::string& trace_id, bool success,
                const std::string& source_ip, const std::string& key_fingerprint);
  void log_tool_call(const std::string& trace_id, const std::string& agent,
                     const std::string& tool, const std::string& params_summary,
                     int duration_ms, const std::string& status);
  void log_guard(const std::string& trace_id, const std::string& guard,
                 const std::string& decision, const std::string& input_hash);
  void log_policy_deny(const std::string& trace_id, const std::string& agent,
                       const std::string& tool, const std::string& reason);
  void log_injection(const std::string& trace_id, double score,
                     const std::string& patterns_found);
  void log_ssrf_block(const std::string& trace_id, const std::string& url,
                      const std::string& reason);
  void log_rate_limit(const std::string& trace_id, const std::string& key,
                      const std::string& scope);
  void log_budget(const std::string& trace_id, const std::string& budget_name,
                  const std::string& resource, double used, double limit, bool exhausted);

private:
  AuditLogger() = default;

  void emit(const nlohmann::json& event_json);
  static std::string event_type_string(EventType type);
  static std::string now_iso8601();

  std::mutex mutex_;
  AuditSink sink_ = AuditSink::Stderr;
  std::ofstream file_stream_;
};

// UUID v4 trace ID generation
std::string generate_trace_id();

// Thread-local trace context propagation
class TraceContext {
public:
  explicit TraceContext(std::string trace_id);
  ~TraceContext();

  static std::string current();

  TraceContext(const TraceContext&) = delete;
  TraceContext& operator=(const TraceContext&) = delete;

private:
  std::string previous_;
};

}  // namespace neamc::security
