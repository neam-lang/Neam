//
// Neam v0.6.9 — Structured Audit Logging Implementation (D1)
//

#include "neamc/security/audit_log.hpp"
#include "neamc/security/credential_manager.hpp"

#include <ctime>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace neamc::security {

// --- Thread-local trace ID ---

static thread_local std::string tl_trace_id;

std::string generate_trace_id()
{
  static thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<uint64_t> dist;

  uint64_t hi = dist(rng);
  uint64_t lo = dist(rng);

  // UUID v4: set version bits (0100) and variant bits (10xx)
  hi = (hi & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
  lo = (lo & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

  char buf[37];
  std::snprintf(buf, sizeof(buf),
                "%08x-%04x-%04x-%04x-%012llx",
                static_cast<uint32_t>(hi >> 32),
                static_cast<uint16_t>((hi >> 16) & 0xFFFF),
                static_cast<uint16_t>(hi & 0xFFFF),
                static_cast<uint16_t>(lo >> 48),
                static_cast<unsigned long long>(lo & 0x0000FFFFFFFFFFFFULL));
  return std::string(buf);
}

TraceContext::TraceContext(std::string trace_id)
    : previous_(tl_trace_id)
{
  tl_trace_id = std::move(trace_id);
}

TraceContext::~TraceContext()
{
  tl_trace_id = std::move(previous_);
}

std::string TraceContext::current()
{
  return tl_trace_id;
}

// --- AuditLogger ---

AuditLogger& AuditLogger::instance()
{
  static AuditLogger logger;
  return logger;
}

void AuditLogger::configure(AuditSink sink, const std::string& file_path)
{
  std::lock_guard<std::mutex> lock(mutex_);
  sink_ = sink;

  if (sink == AuditSink::File && !file_path.empty())
  {
    if (file_stream_.is_open())
    {
      file_stream_.close();
    }
    file_stream_.open(file_path, std::ios::app);
    if (!file_stream_.is_open())
    {
      std::cerr << "[audit] WARNING: Cannot open audit log file: " << file_path
                << ", falling back to stderr\n";
      sink_ = AuditSink::Stderr;
    }
  }
}

std::string AuditLogger::now_iso8601()
{
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) % 1000;

  struct tm tm_buf{};
#if defined(_WIN32)
  gmtime_s(&tm_buf, &time_t_now);
#else
  gmtime_r(&time_t_now, &tm_buf);
#endif

  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);

  std::ostringstream oss;
  oss << buf << "." << std::setfill('0') << std::setw(3) << ms.count() << "Z";
  return oss.str();
}

std::string AuditLogger::event_type_string(EventType type)
{
  switch (type)
  {
    case EventType::AuthSuccess:       return "auth.success";
    case EventType::AuthFailure:       return "auth.failure";
    case EventType::AgentInvoke:       return "agent.invoke";
    case EventType::ToolCall:          return "tool.call";
    case EventType::ToolResult:        return "tool.result";
    case EventType::GuardBlock:        return "guard.block";
    case EventType::GuardPass:         return "guard.pass";
    case EventType::BudgetConsume:     return "budget.consume";
    case EventType::BudgetExhausted:   return "budget.exhausted";
    case EventType::PolicyDeny:        return "policy.deny";
    case EventType::PolicyConfirm:     return "policy.confirm";
    case EventType::InjectionDetected: return "injection.detected";
    case EventType::SSRFBlocked:       return "ssrf.blocked";
    case EventType::RateLimitHit:      return "rate_limit.hit";
    case EventType::MCPRequest:        return "mcp.request";
    case EventType::MCPResponse:       return "mcp.response";
    case EventType::AnomalyDetected:   return "anomaly.detected";
    case EventType::AgentDisabled:     return "agent.disabled";
    case EventType::ConfirmRequest:    return "confirm.request";
    case EventType::ConfirmResult:     return "confirm.result";
    case EventType::Error:             return "error";
  }
  return "unknown";
}

void AuditLogger::emit(const nlohmann::json& event_json)
{
  std::lock_guard<std::mutex> lock(mutex_);

  // v0.6.9 D7: Redact credentials before writing to any sink
  auto& cred_mgr = CredentialManager::instance();

  switch (sink_)
  {
    case AuditSink::Stderr:
    {
      // Human-readable format for development
      std::cerr << "[audit] "
                << event_json.value("timestamp", "")
                << " " << event_json.value("event", "")
                << " trace=" << event_json.value("trace_id", "")
                << " " << cred_mgr.redact(event_json.value("detail", ""));
      if (event_json.contains("agent") && !event_json["agent"].get<std::string>().empty())
      {
        std::cerr << " agent=" << event_json["agent"].get<std::string>();
      }
      if (event_json.contains("tool") && !event_json["tool"].get<std::string>().empty())
      {
        std::cerr << " tool=" << event_json["tool"].get<std::string>();
      }
      std::cerr << "\n";
      break;
    }
    case AuditSink::File:
    {
      if (file_stream_.is_open())
      {
        file_stream_ << cred_mgr.redact(event_json.dump()) << "\n";
        file_stream_.flush();
      }
      break;
    }
    case AuditSink::JsonStdout:
    {
      std::cout << cred_mgr.redact(event_json.dump()) << "\n";
      std::cout.flush();
      break;
    }
  }
}

void AuditLogger::log(const AuditEvent& event)
{
  nlohmann::json j = {
      {"timestamp", event.timestamp.empty() ? now_iso8601() : event.timestamp},
      {"trace_id", event.trace_id.empty() ? TraceContext::current() : event.trace_id},
      {"event", event_type_string(event.event_type)},
      {"agent", event.agent},
      {"tool", event.tool},
      {"detail", event.detail}};

  if (!event.metadata.is_null() && !event.metadata.empty())
  {
    j["metadata"] = event.metadata;
  }

  emit(j);
}

void AuditLogger::log_auth(const std::string& trace_id, bool success,
                           const std::string& source_ip,
                           const std::string& key_fingerprint)
{
  AuditEvent event;
  event.trace_id = trace_id;
  event.event_type = success ? EventType::AuthSuccess : EventType::AuthFailure;
  event.detail = success ? "Authentication succeeded" : "Authentication failed";
  event.metadata = {
      {"source_ip", source_ip},
      {"key_fingerprint", key_fingerprint}};
  log(event);
}

void AuditLogger::log_tool_call(const std::string& trace_id, const std::string& agent,
                                const std::string& tool, const std::string& params_summary,
                                int duration_ms, const std::string& status)
{
  AuditEvent event;
  event.trace_id = trace_id;
  event.event_type = EventType::ToolCall;
  event.agent = agent;
  event.tool = tool;
  event.detail = "Tool invocation: " + status;
  event.metadata = {
      {"params_summary", params_summary},
      {"duration_ms", duration_ms},
      {"status", status}};
  log(event);
}

void AuditLogger::log_guard(const std::string& trace_id, const std::string& guard,
                            const std::string& decision, const std::string& input_hash)
{
  AuditEvent event;
  event.trace_id = trace_id;
  event.event_type = (decision == "block") ? EventType::GuardBlock : EventType::GuardPass;
  event.tool = guard;
  event.detail = "Guard " + decision;
  event.metadata = {
      {"decision", decision},
      {"input_hash", input_hash}};
  log(event);
}

void AuditLogger::log_policy_deny(const std::string& trace_id, const std::string& agent,
                                  const std::string& tool, const std::string& reason)
{
  AuditEvent event;
  event.trace_id = trace_id;
  event.event_type = EventType::PolicyDeny;
  event.agent = agent;
  event.tool = tool;
  event.detail = "Policy denied: " + reason;
  event.metadata = {{"reason", reason}};
  log(event);
}

void AuditLogger::log_injection(const std::string& trace_id, double score,
                                const std::string& patterns_found)
{
  AuditEvent event;
  event.trace_id = trace_id;
  event.event_type = EventType::InjectionDetected;
  event.detail = "Injection detected";
  event.metadata = {
      {"score", score},
      {"patterns", patterns_found}};
  log(event);
}

void AuditLogger::log_ssrf_block(const std::string& trace_id, const std::string& url,
                                 const std::string& reason)
{
  AuditEvent event;
  event.trace_id = trace_id;
  event.event_type = EventType::SSRFBlocked;
  event.detail = "SSRF blocked: " + reason;
  event.metadata = {
      {"url", url},
      {"reason", reason}};
  log(event);
}

void AuditLogger::log_rate_limit(const std::string& trace_id, const std::string& key,
                                 const std::string& scope)
{
  AuditEvent event;
  event.trace_id = trace_id;
  event.event_type = EventType::RateLimitHit;
  event.detail = "Rate limit hit: " + scope;
  event.metadata = {
      {"key", key},
      {"scope", scope}};
  log(event);
}

void AuditLogger::log_budget(const std::string& trace_id, const std::string& budget_name,
                             const std::string& resource, double used, double limit,
                             bool exhausted)
{
  AuditEvent event;
  event.trace_id = trace_id;
  event.event_type = exhausted ? EventType::BudgetExhausted : EventType::BudgetConsume;
  event.detail = exhausted ? "Budget exhausted: " + budget_name
                           : "Budget consume: " + budget_name;
  event.metadata = {
      {"budget", budget_name},
      {"resource", resource},
      {"used", used},
      {"limit", limit},
      {"utilization_pct", limit > 0 ? (used / limit * 100.0) : 0.0}};
  log(event);
}

}  // namespace neamc::security
