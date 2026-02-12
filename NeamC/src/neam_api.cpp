//
// Neam API - Native REST API Server for Neam Agents
//
// Exposes Neam agents as HTTP endpoints, enabling integration with any application.
//

#include "neamc/api/http_server.hpp"
#include "neamc/pipeline.hpp"
#include "neamc/security/audit_log.hpp"
#include "neamc/security/behavioral_monitor.hpp"
#include "neamc/security/credential_manager.hpp"
#include "neamc/security/human_in_the_loop.hpp"
#include "neamc/security/rate_limiter.hpp"
#include "neamc/version.hpp"
#include "neamc/vm/vm.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

// Forward declarations
std::string escape_string(const std::string& input);

// Extract source IP from request (X-Forwarded-For or X-Real-IP)
std::string get_source_ip(const neamc::api::HttpRequest& request)
{
  auto it = request.headers.find("X-Forwarded-For");
  if (it != request.headers.end() && !it->second.empty())
  {
    auto comma = it->second.find(',');
    return comma != std::string::npos ? it->second.substr(0, comma) : it->second;
  }
  it = request.headers.find("X-Real-IP");
  if (it != request.headers.end())
  {
    return it->second;
  }
  return "unknown";
}

// Fingerprint an API key (first 4 + last 4 chars)
std::string key_fingerprint(const std::string& key)
{
  if (key.size() <= 8)
  {
    return "****";
  }
  return key.substr(0, 4) + "..." + key.substr(key.size() - 4);
}

// Extract or generate trace ID from request
std::string get_or_create_trace_id(const neamc::api::HttpRequest& request)
{
  auto it = request.headers.find("X-Trace-ID");
  if (it != request.headers.end() && !it->second.empty())
  {
    return it->second;
  }
  it = request.headers.find("X-Request-ID");
  if (it != request.headers.end() && !it->second.empty())
  {
    return it->second;
  }
  return neamc::security::generate_trace_id();
}

// API key authentication (v0.6.5 security fix)
// When NEAM_API_KEY is set, all requests must include Authorization: Bearer <key>
std::string get_api_key()
{
  const char* key = std::getenv("NEAM_API_KEY");
  return key ? std::string(key) : "";
}

bool authenticate_request(const neamc::api::HttpRequest& request)
{
  const std::string key_env = get_api_key();
  if (key_env.empty())
  {
    return true;  // No key configured — open mode (dev only)
  }

  auto it = request.headers.find("Authorization");
  if (it == request.headers.end())
  {
    // Also check lowercase (some HTTP servers normalize headers)
    it = request.headers.find("authorization");
    if (it == request.headers.end())
    {
      return false;
    }
  }

  const std::string prefix = "Bearer ";
  if (it->second.size() < prefix.size() ||
      it->second.substr(0, prefix.size()) != prefix)
  {
    return false;
  }

  const std::string provided = it->second.substr(prefix.size());

  // v0.6.9 D7: Multi-key rotation — parse comma-separated keys
  auto& cred_mgr = neamc::security::CredentialManager::instance();
  auto keys = cred_mgr.parse_api_keys(key_env);

  for (const auto& key : keys)
  {
    if (neamc::security::CredentialManager::constant_time_compare(provided, key))
    {
      return true;
    }
  }
  return false;
}

neamc::api::HttpResponse unauthorized_response()
{
  nlohmann::json body = {
      {"error", "Unauthorized"},
      {"message", "Set NEAM_API_KEY env var and pass Authorization: Bearer <key> header"}};
  auto resp = neamc::api::HttpResponse::json(body, 401);
  resp.status_code = 401;
  resp.status_text = "Unauthorized";
  return resp;
}

// Agent configuration
struct AgentConfig
{
  std::string name;
  std::string provider;
  std::string model;
  std::string system_prompt;
  std::string knowledge_base;  // Optional path for RAG
};

// Available agents
const std::map<std::string, AgentConfig> kAgents = {
    {"assistant",
     {"Assistant", "openai", "gpt-4o-mini",
      "You are a helpful assistant. Provide clear, concise, and accurate responses.", ""}},
    {"coder",
     {"Coder", "openai", "gpt-4o-mini",
      "You are an expert programmer. Provide clean, efficient, and well-documented code solutions.", ""}},
    {"analyst",
     {"Analyst", "openai", "gpt-4o-mini",
      "You are a data analyst. Analyze information and provide insights with clear explanations.", ""}},
    {"writer",
     {"Writer", "openai", "gpt-4o-mini",
      "You are a creative writer. Produce engaging, well-structured content.", ""}},
    {"researcher",
     {"Researcher", "openai", "gpt-4o-mini",
      "You are a researcher. Use the provided knowledge context to answer questions accurately.",
      "./readme.md"}}};

// v0.6.8: Thread pool for concurrent request handling (replaces single g_vm_mutex)
class ThreadPool
{
public:
  explicit ThreadPool(size_t threads = 4)
  {
    for (size_t i = 0; i < threads; ++i)
    {
      workers_.emplace_back([this] {
        while (true)
        {
          std::function<void()> task;
          {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
            if (stop_ && tasks_.empty())
            {
              return;
            }
            task = std::move(tasks_.front());
            tasks_.pop();
          }
          task();
        }
      });
    }
  }

  ~ThreadPool()
  {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      stop_ = true;
    }
    condition_.notify_all();
    for (auto& worker : workers_)
    {
      if (worker.joinable())
      {
        worker.join();
      }
    }
  }

  template <typename F>
  void enqueue(F&& task)
  {
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      tasks_.push(std::forward<F>(task));
    }
    condition_.notify_one();
  }

private:
  std::vector<std::thread> workers_;
  std::queue<std::function<void()>> tasks_;
  std::mutex queue_mutex_;
  std::condition_variable condition_;
  bool stop_{false};
};

// Request timeout in milliseconds (default 60s, configurable via --timeout)
long g_request_timeout_ms = 60000;

// Generate Neam program for agent query
std::string generate_neam_program(const std::string& agent_id, const std::string& query,
                                  const AgentConfig& config)
{
  std::ostringstream program;

  // Add knowledge base if configured
  if (!config.knowledge_base.empty())
  {
    program << "knowledge AgentKB {\n";
    program << "  vector_store: \"usearch\"\n";
    program << "  embedding_model: \"nomic-embed-text\"\n";
    program << "  chunk_size: 200\n";
    program << "  chunk_overlap: 50\n";
    program << "  sources: [\n";
    program << "    { type: \"file\", path: \"" << config.knowledge_base << "\" }\n";
    program << "  ]\n";
    program << "  retrieval_strategy: \"basic\"\n";
    program << "  top_k: 3\n";
    program << "}\n\n";
  }

  // Define agent (Neam syntax doesn't use commas between fields)
  program << "agent QueryAgent {\n";
  program << "  provider: \"" << config.provider << "\"\n";
  program << "  model: \"" << config.model << "\"\n";
  program << "  system: \"" << config.system_prompt << "\"";

  if (!config.knowledge_base.empty())
  {
    program << "\n  connected_knowledge: [AgentKB]";
  }

  program << "\n}\n\n";

  // Execute query
  program << "{\n";
  program << "  let response = QueryAgent.ask(\"" << escape_string(query) << "\");\n";
  program << "  emit response;\n";
  program << "}\n";

  return program.str();
}

// Escape special characters in string for Neam code
std::string escape_string(const std::string& input)
{
  std::string result;
  result.reserve(input.size() * 2);

  for (char c : input)
  {
    switch (c)
    {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        result += c;
        break;
    }
  }

  return result;
}

// Convert Value to string representation
std::string value_to_string(const neamc::vm::Value& value)
{
  if (value.is_nil())
  {
    return "nil";
  }
  if (value.is_bool())
  {
    return value.as_bool() ? "true" : "false";
  }
  if (value.is_number())
  {
    std::ostringstream ss;
    ss << value.as_number();
    return ss.str();
  }
  if (value.is_string())
  {
    auto* str = neamc::vm::as_string(value);
    if (str && str->chars)
    {
      return std::string(str->chars, str->length);
    }
    return "";
  }
  return "<object>";
}

// v0.6.8: Execute Neam program with per-request VM (no global mutex).
// Each request gets its own Pipeline + VM for full concurrency.
std::string execute_neam_program(const std::string& source)
{
  try
  {
    neamc::Pipeline pipeline;
    auto unit = pipeline.compile(source, {});

    std::ostringstream output_stream;
    std::istringstream input_stream;

    neamc::vm::VirtualMachine vm;
    vm.set_io(&input_stream, &output_stream);
    vm.run(unit.chunk);

    const auto& emitted = vm.emitted();
    if (!emitted.empty())
    {
      return value_to_string(emitted.back());
    }

    return output_stream.str();
  }
  catch (const std::exception& ex)
  {
    return std::string("Error: ") + ex.what();
  }
}

// API Routes

// GET /api/v1/health
neamc::api::HttpResponse handle_health(const neamc::api::HttpRequest&)
{
  nlohmann::json response = {{"status", "healthy"}, {"version", NEAM_VERSION}, {"server", "neam-api"}};
  return neamc::api::HttpResponse::json(response);
}

// v0.6.9 D5: Rate limit helper — returns 429 response if limited
neamc::api::HttpResponse rate_limit_response()
{
  nlohmann::json body = {
      {"error", "Too Many Requests"},
      {"message", "Rate limit exceeded. Please wait before retrying."}};
  auto resp = neamc::api::HttpResponse::json(body, 429);
  resp.status_code = 429;
  resp.status_text = "Too Many Requests";
  return resp;
}

// GET /api/v1/agents
neamc::api::HttpResponse handle_list_agents(const neamc::api::HttpRequest& request)
{
  auto trace_id = get_or_create_trace_id(request);
  neamc::security::TraceContext trace_ctx(trace_id);
  auto& audit = neamc::security::AuditLogger::instance();

  // v0.6.9 D5: Rate limit by IP before auth
  std::string client_ip = get_source_ip(request);
  auto& limiter = neamc::security::RateLimiter::instance();
  if (!limiter.check_ip(client_ip))
  {
    audit.log_rate_limit(trace_id, client_ip, "ip");
    return rate_limit_response();
  }

  if (!authenticate_request(request))
  {
    audit.log_auth(trace_id, false, client_ip, "");
    return unauthorized_response();
  }
  audit.log_auth(trace_id, true, client_ip, key_fingerprint(get_api_key()));

  // v0.6.9 D5: Rate limit by API key after auth
  if (!limiter.check_api_key(get_api_key()))
  {
    audit.log_rate_limit(trace_id, key_fingerprint(get_api_key()), "api_key");
    return rate_limit_response();
  }

  nlohmann::json agents_json = nlohmann::json::object();

  for (const auto& [id, config] : kAgents)
  {
    // v0.6.9 D7: Omit system prompts from response to prevent leakage
    agents_json[id] = {{"name", config.name},
                       {"provider", config.provider},
                       {"model", config.model},
                       {"has_knowledge_base", !config.knowledge_base.empty()}};
  }

  return neamc::api::HttpResponse::json({{"agents", agents_json}});
}

// POST /api/v1/agent/ask
neamc::api::HttpResponse handle_agent_ask(const neamc::api::HttpRequest& request)
{
  auto trace_id = get_or_create_trace_id(request);
  neamc::security::TraceContext trace_ctx(trace_id);
  auto& audit = neamc::security::AuditLogger::instance();

  // v0.6.9 D5: Rate limit by IP before auth
  std::string client_ip = get_source_ip(request);
  auto& limiter = neamc::security::RateLimiter::instance();
  if (!limiter.check_ip(client_ip))
  {
    audit.log_rate_limit(trace_id, client_ip, "ip");
    return rate_limit_response();
  }

  if (!authenticate_request(request))
  {
    audit.log_auth(trace_id, false, client_ip, "");
    return unauthorized_response();
  }
  audit.log_auth(trace_id, true, client_ip, key_fingerprint(get_api_key()));

  // v0.6.9 D5: Rate limit by API key + concurrent check
  std::string api_key_fp = key_fingerprint(get_api_key());
  if (!limiter.check_api_key(get_api_key()))
  {
    audit.log_rate_limit(trace_id, api_key_fp, "api_key");
    return rate_limit_response();
  }
  if (!limiter.check_concurrent(api_key_fp))
  {
    audit.log_rate_limit(trace_id, api_key_fp, "concurrent");
    return rate_limit_response();
  }

  // v0.6.9 D5/D8: Request body size limit
  auto max_body = limiter.input_limits().max_request_body_bytes;
  if (request.body.size() > max_body)
  {
    limiter.release_concurrent(api_key_fp);
    nlohmann::json err = {
        {"error", "Payload Too Large"},
        {"message", "Request body exceeds maximum size of " + std::to_string(max_body) + " bytes"}};
    auto resp = neamc::api::HttpResponse::json(err, 413);
    resp.status_code = 413;
    resp.status_text = "Payload Too Large";
    return resp;
  }

  // Parse JSON body
  nlohmann::json body;
  try
  {
    body = nlohmann::json::parse(request.body);
  }
  catch (const std::exception&)
  {
    limiter.release_concurrent(api_key_fp);
    return neamc::api::HttpResponse::bad_request("Invalid JSON body");
  }

  // Validate required fields
  if (!body.contains("agent_id"))
  {
    limiter.release_concurrent(api_key_fp);
    return neamc::api::HttpResponse::bad_request("Missing 'agent_id' field");
  }
  if (!body.contains("query"))
  {
    limiter.release_concurrent(api_key_fp);
    return neamc::api::HttpResponse::bad_request("Missing 'query' field");
  }

  std::string agent_id = body["agent_id"];
  std::string query = body["query"];

  // v0.6.9 D8: Prompt size limit
  if (query.size() > limiter.input_limits().max_prompt_bytes)
  {
    limiter.release_concurrent(api_key_fp);
    return neamc::api::HttpResponse::bad_request(
        "Query exceeds maximum prompt size of " +
        std::to_string(limiter.input_limits().max_prompt_bytes) + " bytes");
  }

  // Find agent configuration
  auto agent_it = kAgents.find(agent_id);
  if (agent_it == kAgents.end())
  {
    std::vector<std::string> available;
    for (const auto& [id, _] : kAgents)
    {
      available.push_back(id);
    }

    std::ostringstream error_msg;
    error_msg << "Unknown agent: " << agent_id << ". Available: [";
    for (size_t i = 0; i < available.size(); ++i)
    {
      if (i > 0)
        error_msg << ", ";
      error_msg << "'" << available[i] << "'";
    }
    error_msg << "]";

    return neamc::api::HttpResponse::bad_request(error_msg.str());
  }

  const AgentConfig& config = agent_it->second;

  // Log agent invocation
  audit.log({
      {},  // timestamp auto-filled
      trace_id,
      neamc::security::EventType::AgentInvoke,
      agent_id,
      {},
      "Agent invoked with query",
      {{"query_length", query.size()}, {"provider", config.provider}, {"model", config.model}}});

  // Generate and execute Neam program
  auto start = std::chrono::steady_clock::now();
  std::string program = generate_neam_program(agent_id, query, config);
  std::string response = execute_neam_program(program);
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count();

  // v0.6.9 D5: Release concurrent slot
  limiter.release_concurrent(api_key_fp);

  // v0.6.9 D7: Redact credentials from error responses
  bool is_error = response.size() >= 6 && response.substr(0, 6) == "Error:";
  if (is_error)
  {
    response = neamc::security::CredentialManager::instance().redact(response);
  }

  // Log agent result
  audit.log({
      {},
      trace_id,
      neamc::security::EventType::ToolResult,
      agent_id,
      {},
      "Agent execution completed",
      {{"duration_ms", elapsed},
       {"response_length", response.size()},
       {"is_error", is_error}}});

  nlohmann::json result = {
      {"agent_id", agent_id},
      {"query", query},
      {"response", response},
      {"trace_id", trace_id}};

  return neamc::api::HttpResponse::json(result);
}

// v0.6.9 D9: Admin authentication — requires NEAM_ADMIN_KEY
bool authenticate_admin(const neamc::api::HttpRequest& request)
{
  const char* admin_key = std::getenv("NEAM_ADMIN_KEY");
  if (!admin_key || std::strlen(admin_key) == 0) return false;

  auto it = request.headers.find("Authorization");
  if (it == request.headers.end())
  {
    it = request.headers.find("authorization");
    if (it == request.headers.end()) return false;
  }

  const std::string prefix = "Bearer ";
  if (it->second.size() < prefix.size() ||
      it->second.substr(0, prefix.size()) != prefix)
    return false;

  return neamc::security::CredentialManager::constant_time_compare(
      it->second.substr(prefix.size()), std::string(admin_key));
}

neamc::api::HttpResponse forbidden_response()
{
  nlohmann::json body = {
      {"error", "Forbidden"},
      {"message", "Admin API key required (NEAM_ADMIN_KEY)"}};
  auto resp = neamc::api::HttpResponse::json(body, 403);
  resp.status_code = 403;
  resp.status_text = "Forbidden";
  return resp;
}

// POST /api/v1/admin/disable — body: {"agent": "name"}
neamc::api::HttpResponse handle_disable_agent(const neamc::api::HttpRequest& request)
{
  if (!authenticate_admin(request)) return forbidden_response();

  nlohmann::json body;
  try { body = nlohmann::json::parse(request.body); }
  catch (...) { return neamc::api::HttpResponse::bad_request("Invalid JSON"); }

  std::string agent = body.value("agent", "");
  if (agent.empty())
    return neamc::api::HttpResponse::bad_request("Missing 'agent' field");

  auto& monitor = neamc::security::BehavioralMonitor::instance();
  monitor.disable_agent(agent);

  neamc::security::AuditLogger::instance().log({
      {},
      get_or_create_trace_id(request),
      neamc::security::EventType::AgentDisabled,
      agent,
      {},
      "Agent disabled via admin API",
      {{"source_ip", get_source_ip(request)}}});

  return neamc::api::HttpResponse::json({{"status", "disabled"}, {"agent", agent}});
}

// POST /api/v1/admin/enable — body: {"agent": "name"}
neamc::api::HttpResponse handle_enable_agent(const neamc::api::HttpRequest& request)
{
  if (!authenticate_admin(request)) return forbidden_response();

  nlohmann::json body;
  try { body = nlohmann::json::parse(request.body); }
  catch (...) { return neamc::api::HttpResponse::bad_request("Invalid JSON"); }

  std::string agent = body.value("agent", "");
  if (agent.empty())
    return neamc::api::HttpResponse::bad_request("Missing 'agent' field");

  neamc::security::BehavioralMonitor::instance().enable_agent(agent);

  return neamc::api::HttpResponse::json({{"status", "enabled"}, {"agent", agent}});
}

// GET /api/v1/admin/status
neamc::api::HttpResponse handle_admin_status(const neamc::api::HttpRequest& request)
{
  if (!authenticate_admin(request)) return forbidden_response();

  auto& monitor = neamc::security::BehavioralMonitor::instance();
  auto disabled = monitor.disabled_agents();

  nlohmann::json baselines_json = nlohmann::json::object();
  for (const auto& [name, baseline] : monitor.baselines())
  {
    baselines_json[name] = {
        {"avg_tool_calls", baseline.avg_tool_calls},
        {"avg_response_time_ms", baseline.avg_response_time_ms},
        {"sample_count", baseline.sample_count},
        {"typical_tools_count", static_cast<int>(baseline.typical_tools.size())}};
  }

  return neamc::api::HttpResponse::json({
      {"disabled_agents", disabled},
      {"baselines", baselines_json}});
}

// POST /api/v1/confirm — body: {"trace_id": "...", "action": "approve"|"deny"}
neamc::api::HttpResponse handle_confirm(const neamc::api::HttpRequest& request)
{
  if (!authenticate_admin(request)) return forbidden_response();

  nlohmann::json body;
  try { body = nlohmann::json::parse(request.body); }
  catch (...) { return neamc::api::HttpResponse::bad_request("Invalid JSON"); }

  std::string trace_id = body.value("trace_id", "");
  std::string action = body.value("action", "");
  if (trace_id.empty())
    return neamc::api::HttpResponse::bad_request("Missing 'trace_id' field");
  if (action != "approve" && action != "deny")
    return neamc::api::HttpResponse::bad_request("'action' must be 'approve' or 'deny'");

  auto& cm = neamc::security::ConfirmationManager::instance();
  auto& audit = neamc::security::AuditLogger::instance();

  bool ok = false;
  if (action == "approve")
  {
    ok = cm.approve(trace_id);
    if (ok)
    {
      audit.log({
          {},
          trace_id,
          neamc::security::EventType::PolicyConfirm,
          "",
          "",
          "Confirmation approved via admin API",
          {{"source_ip", get_source_ip(request)}}});
    }
  }
  else
  {
    ok = cm.deny(trace_id);
    if (ok)
    {
      audit.log({
          {},
          trace_id,
          neamc::security::EventType::PolicyConfirm,
          "",
          "",
          "Confirmation denied via admin API",
          {{"source_ip", get_source_ip(request)}}});
    }
  }

  if (!ok)
  {
    return neamc::api::HttpResponse::json(
        {{"error", "Confirmation not found, already resolved, or expired"}}, 404);
  }

  return neamc::api::HttpResponse::json(
      {{"status", action == "approve" ? "approved" : "denied"},
       {"trace_id", trace_id}});
}

// GET /api/v1/admin/confirmations — list pending confirmations
neamc::api::HttpResponse handle_list_confirmations(const neamc::api::HttpRequest& request)
{
  if (!authenticate_admin(request)) return forbidden_response();

  auto& cm = neamc::security::ConfirmationManager::instance();
  cm.cleanup_expired();
  auto pending = cm.pending_list();

  nlohmann::json items = nlohmann::json::array();
  for (const auto& pc : pending)
  {
    items.push_back({
        {"trace_id", pc.trace_id},
        {"agent", pc.agent_name},
        {"tool", pc.tool_name},
        {"args", pc.args_json},
        {"reason", pc.reason},
        {"timeout_seconds", pc.timeout_seconds}});
  }

  return neamc::api::HttpResponse::json({{"pending", items}});
}

void print_usage(const char* program_name)
{
  std::cout << "Usage: " << program_name << " [options]\n"
            << "\nOptions:\n"
            << "  --host HOST       Host to bind to (default: 0.0.0.0)\n"
            << "  --port PORT       Port to listen on (default: 8080)\n"
            << "  --workers N       Thread pool size (default: 4)\n"
            << "  --timeout MS      Request timeout in ms (default: 60000)\n"
            << "  --audit-sink SINK Audit log sink: stderr|file|json (default: stderr)\n"
            << "  --audit-log PATH  Audit log file path (when sink=file)\n"
            << "  --help            Show this help message\n"
            << "\nEnvironment Variables:\n"
            << "  NEAM_API_KEY     API authentication key (required in production)\n"
            << "  NEAM_ADMIN_KEY   Admin API key for behavioral monitoring endpoints\n"
            << "  OPENAI_API_KEY   Required for OpenAI agents\n"
            << "\nAPI Endpoints:\n"
            << "  GET  /api/v1/health      Health check\n"
            << "  GET  /api/v1/agents      List available agents\n"
            << "  POST /api/v1/agent/ask   Query an agent\n"
            << "  POST /api/v1/admin/disable  Disable an agent (admin)\n"
            << "  POST /api/v1/admin/enable   Enable an agent (admin)\n"
            << "  GET  /api/v1/admin/status   Monitoring status (admin)\n"
            << "  POST /api/v1/confirm         Approve/deny tool (admin)\n"
            << "  GET  /api/v1/admin/confirmations Pending confirms (admin)\n"
            << "  GET  /health             Health check (alias)\n"
            << "  GET  /ready              Readiness check (alias)\n"
            << "\nAvailable Agents:\n";

  for (const auto& [id, config] : kAgents)
  {
    std::cout << "  " << id << " - " << config.name;
    if (!config.knowledge_base.empty())
    {
      std::cout << " (RAG)";
    }
    std::cout << "\n";
  }

  std::cout << "\nExample:\n"
            << "  curl -X POST http://localhost:8080/api/v1/agent/ask \\\n"
            << "    -H \"Content-Type: application/json\" \\\n"
            << "    -d '{\"agent_id\": \"assistant\", \"query\": \"Hello!\"}'\n";
}

}  // namespace

int main(int argc, char** argv)
{
  std::string host = "0.0.0.0";
  int port = 8080;
  int workers = 4;
  std::string audit_sink_str = "stderr";
  std::string audit_log_path;

  // Parse command line arguments
  for (int i = 1; i < argc; ++i)
  {
    std::string arg = argv[i];

    if (arg == "--help" || arg == "-h")
    {
      print_usage(argv[0]);
      return 0;
    }
    else if (arg == "--host" && i + 1 < argc)
    {
      host = argv[++i];
    }
    else if (arg == "--port" && i + 1 < argc)
    {
      port = std::stoi(argv[++i]);
    }
    else if (arg == "--audit-sink" && i + 1 < argc)
    {
      audit_sink_str = argv[++i];
    }
    else if (arg == "--audit-log" && i + 1 < argc)
    {
      audit_log_path = argv[++i];
    }
    else if (arg == "--workers" && i + 1 < argc)
    {
      workers = std::stoi(argv[++i]);
      if (workers < 1)
      {
        workers = 1;
      }
      if (workers > 64)
      {
        workers = 64;
      }
    }
    else if (arg == "--timeout" && i + 1 < argc)
    {
      g_request_timeout_ms = std::stol(argv[++i]);
    }
    else
    {
      std::cerr << "Unknown option: " << arg << "\n";
      print_usage(argv[0]);
      return 1;
    }
  }

  // Configure audit logging
  {
    auto sink = neamc::security::AuditSink::Stderr;
    if (audit_sink_str == "file")
    {
      sink = neamc::security::AuditSink::File;
    }
    else if (audit_sink_str == "json")
    {
      sink = neamc::security::AuditSink::JsonStdout;
    }
    neamc::security::AuditLogger::instance().configure(sink, audit_log_path);
  }

  // Check for API key
  const char* api_key = std::getenv("OPENAI_API_KEY");
  if (!api_key || std::strlen(api_key) == 0)
  {
    std::cerr << "Warning: OPENAI_API_KEY environment variable not set.\n";
    std::cerr << "         OpenAI agents will not work without it.\n\n";
  }

  try
  {
    neamc::api::HttpServer server(port, host);

    // Enable CORS for cross-origin requests
    server.enable_cors();

    // Register routes
    server.get("/api/v1/health", handle_health);
    server.get("/api/v1/agents", handle_list_agents);
    server.post("/api/v1/agent/ask", handle_agent_ask);

    // Root-level health aliases for K8s probes and Lambda Web Adapter
    server.get("/health", handle_health);
    server.get("/ready", handle_health);

    // Admin routes (D9 behavioral monitoring)
    server.post("/api/v1/admin/disable", handle_disable_agent);
    server.post("/api/v1/admin/enable", handle_enable_agent);
    server.get("/api/v1/admin/status", handle_admin_status);

    // D10 Human-in-the-Loop confirmation routes
    server.post("/api/v1/confirm", handle_confirm);
    server.get("/api/v1/admin/confirmations", handle_list_confirmations);

    std::cout << "Starting Neam API Server...\n";
    std::cout << "  Host: " << host << "\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  Audit: sink=" << audit_sink_str;
    if (!audit_log_path.empty())
    {
      std::cout << " file=" << audit_log_path;
    }
    std::cout << "\n";
    std::cout << "  Endpoints:\n";
    std::cout << "    GET  /api/v1/health\n";
    std::cout << "    GET  /api/v1/agents\n";
    std::cout << "    POST /api/v1/agent/ask\n";
    std::cout << "    POST /api/v1/admin/disable (admin)\n";
    std::cout << "    POST /api/v1/admin/enable  (admin)\n";
    std::cout << "    GET  /api/v1/admin/status  (admin)\n";
    std::cout << "    POST /api/v1/confirm       (admin)\n";
    std::cout << "    GET  /api/v1/admin/confirmations (admin)\n";
    std::cout << "    GET  /health          (alias)\n";
    std::cout << "    GET  /ready           (alias)\n";
    std::cout << "\n";

    server.run();
  }
  catch (const std::exception& ex)
  {
    std::cerr << "neam-api error: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
