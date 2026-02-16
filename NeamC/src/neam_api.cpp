//
// Neam API - Native REST API Server for Neam Agents
//
// Exposes Neam agents as HTTP endpoints, enabling integration with any application.
//

#include "neamc/api/http_server.hpp"
#include "neamc/llm/http_client.hpp"
#include "neamc/llm/provider.hpp"
#include "neamc/llm/provider_factory.hpp"
#include "neamc/pipeline.hpp"
#include "neamc/project_manifest.hpp"
#include "neamc/security/audit_log.hpp"
#include "neamc/security/behavioral_monitor.hpp"
#include "neamc/security/credential_manager.hpp"
#include "neamc/security/human_in_the_loop.hpp"
#include "neamc/security/rate_limiter.hpp"
#include "neamc/version.hpp"
#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/bytecode_cache.hpp"
#include "neamc/vm/claw_agent_type.hpp"
#include "neamc/vm/forge_agent_type.hpp"
#include "neamc/vm/forge_loop.hpp"
#include "neamc/vm/metrics.hpp"
#include "neamc/vm/session_manager.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/vm_pool.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
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

// v0.7.2 HotVM: Global pointers set in main(), used by handlers
neamc::vm::BytecodeCache* g_bytecode_cache = nullptr;
neamc::vm::VMPool* g_vm_pool = nullptr;
neamc::vm::RequestMetrics* g_metrics = nullptr;
std::atomic<bool> g_hotvm_ready{false};   // Set true after preload + warm
std::atomic<bool> g_draining{false};      // v0.7.2 W9: Set on SIGTERM
int g_drain_seconds = 10;                 // v0.7.2 W9: Graceful drain period
bool g_warmstart_trace = false;           // v0.7.2 W8: NEAM_WARMSTART_TRACE

// v0.7.2: Dynamic agents map (loaded from neam.toml or defaults)
const std::map<std::string, AgentConfig>* g_active_agents = nullptr;

// v0.8 Phase 9: Global VM pointer for claw/forge agent access
neamc::vm::VirtualMachine* g_vm = nullptr;

// v0.8 Phase 9: Session manager for claw agent endpoints
neamc::vm::SessionManager g_session_manager;
std::mutex g_session_mutex;

// v0.8 Phase 9: Forge loop state tracking
struct ForgeLoopState
{
  bool running{false};
  bool stop_requested{false};
  int iteration{0};
  double total_cost{0.0};
  std::string last_outcome;
  std::thread loop_thread;
};
std::mutex g_forge_mutex;
std::unordered_map<std::string, ForgeLoopState> g_forge_states;

// Parse URL path into segments: "/api/v1/claw/my_agent" -> ["api","v1","claw","my_agent"]
std::vector<std::string> parse_path_segments(const std::string& path)
{
  std::vector<std::string> segments;
  std::string segment;
  for (char c : path)
  {
    if (c == '/')
    {
      if (!segment.empty())
      {
        segments.push_back(segment);
        segment.clear();
      }
    }
    else
    {
      segment += c;
    }
  }
  if (!segment.empty())
  {
    segments.push_back(segment);
  }
  return segments;
}

// Helper: convert ObjString* to std::string safely
std::string obj_str(const neamc::vm::ObjString* s)
{
  if (!s || !s->chars) return "";
  return std::string(s->chars, s->length);
}

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

// v0.7.2: Generate agent template with __query__ placeholder (cacheable)
std::string generate_agent_template(const std::string& agent_id, const AgentConfig& config)
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

  // Define agent
  program << "agent QueryAgent {\n";
  program << "  provider: \"" << config.provider << "\"\n";
  program << "  model: \"" << config.model << "\"\n";
  program << "  system: \"" << config.system_prompt << "\"";

  if (!config.knowledge_base.empty())
  {
    program << "\n  connected_knowledge: [AgentKB]";
  }

  program << "\n}\n\n";

  // Execute query using __query__ global injected at runtime
  program << "{\n";
  program << "  let response = QueryAgent.ask(__query__);\n";
  program << "  emit response;\n";
  program << "}\n";

  return program.str();
}

// v0.7.2: Execute agent query using HotVM (pooled VM + cached bytecode)
std::string execute_with_hotvm(const std::string& agent_id, const std::string& query,
                               neamc::vm::VMPool& pool, neamc::vm::BytecodeCache& cache)
{
  try
  {
    auto vm = pool.acquire();
    if (!vm.get())
    {
      return "Error: VM pool exhausted";
    }

    // Inject __query__ as global variable
    auto* key_str = neamc::vm::copy_string("__query__", 9);
    vm->globals().set(key_str, neamc::vm::Value::String(query.c_str(), query.size()));

    // Get pre-compiled bytecode
    const auto* bytecode = cache.get(agent_id);
    if (!bytecode)
    {
      return "Error: Agent not preloaded: " + agent_id;
    }

    std::ostringstream out;
    std::istringstream in;
    vm->set_io(&in, &out);
    vm->run(*bytecode);

    const auto& emitted = vm->emitted();
    return !emitted.empty() ? neamc::vm::value_to_string(emitted.back()) : out.str();
    // PooledVM dtor returns VM to pool
  }
  catch (const std::exception& ex)
  {
    return std::string("Error: ") + ex.what();
  }
}

// v0.6.8: Execute Neam program with per-request VM (fallback path).
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
      return neamc::vm::value_to_string(emitted.back());
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

  // v0.7.2: HotVM metrics
  if (g_bytecode_cache && g_vm_pool)
  {
    auto cs = g_bytecode_cache->stats();
    auto ps = g_vm_pool->stats();
    response["hotvm"] = {
        {"cache_entries", cs.entries},
        {"cache_hits", cs.hits},
        {"cache_misses", cs.misses},
        {"pool_total", ps.total},
        {"pool_available", ps.available},
        {"pool_in_use", ps.in_use},
        {"pool_peak", ps.peak}};
  }

  return neamc::api::HttpResponse::json(response);
}

// v0.7.2: Readiness probe — returns 503 until HotVM is initialized or when draining
neamc::api::HttpResponse handle_ready(const neamc::api::HttpRequest&)
{
  if (!g_hotvm_ready.load() || g_draining.load())
  {
    std::string reason = g_draining.load() ? "draining" : "initializing";
    nlohmann::json body = {{"status", reason}, {"version", NEAM_VERSION}};
    auto resp = neamc::api::HttpResponse::json(body, 503);
    resp.status_code = 503;
    resp.status_text = "Service Unavailable";
    return resp;
  }
  return handle_health(neamc::api::HttpRequest{});
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

  const auto& agents = g_active_agents ? *g_active_agents : kAgents;
  for (const auto& [id, config] : agents)
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

  // v0.7.2 W9: Reject requests when draining
  if (g_draining.load())
  {
    limiter.release_concurrent(api_key_fp);
    nlohmann::json err = {{"error", "Service Unavailable"},
                          {"message", "Server is shutting down"}};
    auto resp = neamc::api::HttpResponse::json(err, 503);
    resp.status_code = 503;
    resp.status_text = "Service Unavailable";
    return resp;
  }

  // Find agent configuration
  const auto& agents = g_active_agents ? *g_active_agents : kAgents;
  auto agent_it = agents.find(agent_id);
  if (agent_it == agents.end())
  {
    std::vector<std::string> available;
    for (const auto& [id, _] : agents)
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

  // Execute via HotVM (pooled VM + cached bytecode) or fallback
  auto start = std::chrono::steady_clock::now();
  std::string response;
  if (g_vm_pool && g_bytecode_cache)
  {
    response = execute_with_hotvm(agent_id, query, *g_vm_pool, *g_bytecode_cache);
  }
  else
  {
    std::string program = generate_neam_program(agent_id, query, config);
    response = execute_neam_program(program);
  }
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start)
                     .count();

  // v0.7.2 W8: Record metrics
  if (g_metrics)
  {
    neamc::vm::RequestTiming timing;
    timing.total_us = static_cast<double>(elapsed) * 1000.0;  // ms -> us
    timing.warm = (g_vm_pool != nullptr);
    timing.agent_id = agent_id;
    g_metrics->record(timing);

    // Per-request trace when NEAM_WARMSTART_TRACE=1
    if (g_warmstart_trace)
    {
      nlohmann::json trace = {{"agent", agent_id},
                              {"total_us", timing.total_us},
                              {"warm", timing.warm}};
      std::cerr << trace.dump() << "\n";
    }
  }

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

// ─── v0.8 Phase 9: Claw Agent Endpoints ─────────────────────────────────

// GET /api/v1/claw — List claw agents
neamc::api::HttpResponse handle_list_claw(const neamc::api::HttpRequest& request)
{
  if (!authenticate_request(request)) return unauthorized_response();
  if (!g_vm)
  {
    return neamc::api::HttpResponse::error("VM not initialized", 503);
  }

  nlohmann::json agents = nlohmann::json::array();
  for (const auto& [name, agent] : g_vm->claw_agents())
  {
    nlohmann::json entry;
    entry["name"] = name;
    entry["provider"] = obj_str(agent->provider);
    entry["model"] = obj_str(agent->model);
    entry["session_count"] = static_cast<int>(agent->sessions.size());

    nlohmann::json channels = nlohmann::json::array();
    for (const auto& ch : agent->channel_names)
    {
      channels.push_back(ch);
    }
    entry["channels"] = channels;
    agents.push_back(entry);
  }

  return neamc::api::HttpResponse::json({{"claw_agents", agents}});
}

// POST /api/v1/claw/:agent/sessions/:key/message — Send message to claw session
neamc::api::HttpResponse handle_claw_message(const neamc::api::HttpRequest& request)
{
  if (!authenticate_request(request)) return unauthorized_response();
  if (!g_vm)
  {
    return neamc::api::HttpResponse::error("VM not initialized", 503);
  }

  // Parse path: /api/v1/claw/<agent>/sessions/<key>/message
  auto segments = parse_path_segments(request.path);
  // Expected: ["api","v1","claw",<agent>,"sessions",<key>,"message"]
  if (segments.size() < 7 || segments[2] != "claw" || segments[4] != "sessions" ||
      segments[6] != "message")
  {
    return neamc::api::HttpResponse::bad_request(
        "Expected path: /api/v1/claw/:agent/sessions/:key/message");
  }

  const std::string agent_name = segments[3];
  const std::string session_key = segments[5];

  // Look up agent
  const auto& claw_agents = g_vm->claw_agents();
  auto it = claw_agents.find(agent_name);
  if (it == claw_agents.end())
  {
    return neamc::api::HttpResponse::not_found("Claw agent not found: " + agent_name);
  }
  auto* agent = it->second;

  // Parse body for message
  nlohmann::json body;
  try
  {
    body = nlohmann::json::parse(request.body);
  }
  catch (...)
  {
    return neamc::api::HttpResponse::bad_request("Invalid JSON body");
  }

  std::string message = body.value("message", "");
  if (message.empty())
  {
    return neamc::api::HttpResponse::bad_request("Missing 'message' field");
  }

  try
  {
    // Use session manager to manage history
    std::lock_guard<std::mutex> lock(g_session_mutex);
    g_session_manager.append_message(agent, session_key, "user", message);

    // Load history for context
    auto history = g_session_manager.load_history(agent, session_key);

    // Build prompt string from system + history
    std::string prompt;
    if (agent->system && agent->system->length > 0)
    {
      prompt += obj_str(agent->system) + "\n\n";
    }
    for (const auto& [role, content] : history)
    {
      prompt += role + ": " + content + "\n";
    }

    // Create LLM provider and call
    std::string provider_str = obj_str(agent->provider);
    std::string model_str = obj_str(agent->model);

    std::string api_key;
    if (agent->api_key_env && agent->api_key_env->length > 0)
    {
      const char* env_val = std::getenv(obj_str(agent->api_key_env).c_str());
      if (env_val) api_key = env_val;
    }

    std::string endpoint;
    if (agent->endpoint && agent->endpoint->length > 0)
    {
      endpoint = obj_str(agent->endpoint);
    }

    neamc::llm::ProviderConfig prov_config;
    prov_config.model = model_str;
    prov_config.api_key = api_key;
    prov_config.endpoint = endpoint;
    prov_config.temperature = agent->temperature;

    auto provider = neamc::llm::create_provider(provider_str, prov_config);
    if (!provider)
    {
      return neamc::api::HttpResponse::error("Failed to create provider: " + provider_str, 500);
    }

    std::string assistant_content = provider->complete(prompt);

    // Append assistant response to session
    g_session_manager.append_message(agent, session_key, "assistant", assistant_content);

    return neamc::api::HttpResponse::json({
        {"agent", agent_name},
        {"session_key", session_key},
        {"response", assistant_content}});
  }
  catch (const std::exception& ex)
  {
    return neamc::api::HttpResponse::error(
        std::string("Claw agent error: ") + ex.what(), 500);
  }
}

// ─── v0.8 Phase 9: Forge Agent Endpoints ────────────────────────────────

// GET /api/v1/forge — List forge agents
neamc::api::HttpResponse handle_list_forge(const neamc::api::HttpRequest& request)
{
  if (!authenticate_request(request)) return unauthorized_response();
  if (!g_vm)
  {
    return neamc::api::HttpResponse::error("VM not initialized", 503);
  }

  nlohmann::json agents = nlohmann::json::array();
  for (const auto& [name, agent] : g_vm->forge_agents())
  {
    bool running = false;
    {
      std::lock_guard<std::mutex> lock(g_forge_mutex);
      auto it = g_forge_states.find(name);
      if (it != g_forge_states.end())
      {
        running = it->second.running;
      }
    }

    nlohmann::json entry;
    entry["name"] = name;
    entry["provider"] = obj_str(agent->provider);
    entry["model"] = obj_str(agent->model);
    entry["max_iterations"] = agent->loop_config.max_iterations;
    entry["max_cost"] = agent->loop_config.max_cost;
    entry["running"] = running;
    agents.push_back(entry);
  }

  return neamc::api::HttpResponse::json({{"forge_agents", agents}});
}

// POST /api/v1/forge/:agent/start — Start forge loop
neamc::api::HttpResponse handle_forge_action(const neamc::api::HttpRequest& request)
{
  if (!authenticate_request(request)) return unauthorized_response();
  if (!g_vm)
  {
    return neamc::api::HttpResponse::error("VM not initialized", 503);
  }

  // Parse path: /api/v1/forge/<agent>/<action>
  auto segments = parse_path_segments(request.path);
  if (segments.size() < 5 || segments[2] != "forge")
  {
    return neamc::api::HttpResponse::bad_request(
        "Expected path: /api/v1/forge/:agent/start|stop|status");
  }

  const std::string agent_name = segments[3];
  const std::string action = segments[4];

  // Look up agent
  const auto& forge_agents = g_vm->forge_agents();
  auto it = forge_agents.find(agent_name);
  if (it == forge_agents.end())
  {
    return neamc::api::HttpResponse::not_found("Forge agent not found: " + agent_name);
  }
  auto* agent = it->second;

  if (action == "status")
  {
    // GET /api/v1/forge/:agent/status
    std::lock_guard<std::mutex> lock(g_forge_mutex);
    auto state_it = g_forge_states.find(agent_name);
    if (state_it == g_forge_states.end())
    {
      return neamc::api::HttpResponse::json({
          {"agent", agent_name},
          {"running", false},
          {"iteration", 0},
          {"total_cost", 0.0},
          {"last_outcome", "not_started"}});
    }
    auto& state = state_it->second;
    return neamc::api::HttpResponse::json({
        {"agent", agent_name},
        {"running", state.running},
        {"iteration", state.iteration},
        {"total_cost", state.total_cost},
        {"last_outcome", state.last_outcome}});
  }

  if (action == "stop")
  {
    // POST /api/v1/forge/:agent/stop
    std::lock_guard<std::mutex> lock(g_forge_mutex);
    auto state_it = g_forge_states.find(agent_name);
    if (state_it == g_forge_states.end() || !state_it->second.running)
    {
      return neamc::api::HttpResponse::json({
          {"status", "not_running"}, {"agent", agent_name}});
    }
    state_it->second.stop_requested = true;
    return neamc::api::HttpResponse::json({
        {"status", "stopping"}, {"agent", agent_name}});
  }

  if (action == "start")
  {
    // POST /api/v1/forge/:agent/start
    {
      std::lock_guard<std::mutex> lock(g_forge_mutex);
      auto state_it = g_forge_states.find(agent_name);
      if (state_it != g_forge_states.end() && state_it->second.running)
      {
        return neamc::api::HttpResponse::json({
            {"status", "already_running"}, {"agent", agent_name}}, 409);
      }
    }

    // Parse optional overrides
    nlohmann::json body;
    if (!request.body.empty())
    {
      try { body = nlohmann::json::parse(request.body); }
      catch (...) { return neamc::api::HttpResponse::bad_request("Invalid JSON body"); }
    }

    int max_iterations = body.value("max_iterations", agent->loop_config.max_iterations);
    double max_cost = body.value("max_cost", agent->loop_config.max_cost);

    // Initialize state
    {
      std::lock_guard<std::mutex> lock(g_forge_mutex);
      auto& state = g_forge_states[agent_name];
      state.running = true;
      state.stop_requested = false;
      state.iteration = 0;
      state.total_cost = 0.0;
      state.last_outcome = "running";
    }

    // Launch background thread with simplified single-turn forge loop
    std::string a_name = agent_name;
    std::thread forge_thread([a_name, agent, max_iterations, max_cost]() {
      try
      {
        std::string provider_str = obj_str(agent->provider);
        std::string model_str = obj_str(agent->model);

        std::string api_key;
        if (agent->api_key_env && agent->api_key_env->length > 0)
        {
          const char* env = std::getenv(obj_str(agent->api_key_env).c_str());
          if (env) api_key = env;
        }

        std::string endpoint;
        if (agent->endpoint && agent->endpoint->length > 0)
        {
          endpoint = obj_str(agent->endpoint);
        }

        neamc::llm::ProviderConfig prov_config;
        prov_config.model = model_str;
        prov_config.api_key = api_key;
        prov_config.endpoint = endpoint;
        prov_config.temperature = agent->temperature;

        auto provider = neamc::llm::create_provider(provider_str, prov_config);
        if (!provider)
        {
          std::lock_guard<std::mutex> lock(g_forge_mutex);
          auto& s = g_forge_states[a_name];
          s.running = false;
          s.last_outcome = "aborted";
          return;
        }

        std::string system_prompt;
        if (agent->system && agent->system->length > 0)
        {
          system_prompt = obj_str(agent->system);
        }

        // Load plan tasks if plan_file set
        std::vector<std::string> tasks;
        if (!agent->loop_config.plan_file.empty())
        {
          tasks = neamc::vm::load_plan_tasks(agent->loop_config.plan_file);
        }
        if (tasks.empty())
        {
          tasks.push_back("Complete the assigned task");
        }

        std::string outcome_kind = "completed";
        int iteration = 0;
        double cost = 0.0;

        for (int i = 0; i < max_iterations && static_cast<int>(i) < static_cast<int>(tasks.size()); ++i)
        {
          // Check stop requested
          {
            std::lock_guard<std::mutex> lock(g_forge_mutex);
            if (g_forge_states[a_name].stop_requested)
            {
              outcome_kind = "aborted";
              break;
            }
          }

          iteration = i + 1;

          // Build prompt string
          std::string prompt;
          if (!system_prompt.empty())
          {
            prompt = system_prompt + "\n\n";
          }
          prompt += "Task: " + tasks[i];

          std::string resp = provider->complete(prompt);

          // Estimate cost (chars/4 heuristic)
          double tokens_est = static_cast<double>(resp.size()) / 4.0;
          cost += tokens_est * 0.00001;  // rough cost estimate

          // Update state
          {
            std::lock_guard<std::mutex> lock(g_forge_mutex);
            auto& s = g_forge_states[a_name];
            s.iteration = iteration;
            s.total_cost = cost;
          }

          if (cost >= max_cost)
          {
            outcome_kind = "budget_exhausted";
            break;
          }

          // Track progress
          if (!agent->loop_config.progress_file.empty())
          {
            neamc::vm::mark_task_done(agent->loop_config.progress_file, iteration, tasks[i]);
          }
        }

        if (outcome_kind == "completed" && iteration >= max_iterations)
        {
          outcome_kind = "max_iterations";
        }

        // Final state update
        {
          std::lock_guard<std::mutex> lock(g_forge_mutex);
          auto& s = g_forge_states[a_name];
          s.running = false;
          s.last_outcome = outcome_kind;
        }
      }
      catch (const std::exception& ex)
      {
        std::lock_guard<std::mutex> lock(g_forge_mutex);
        auto& s = g_forge_states[a_name];
        s.running = false;
        s.last_outcome = std::string("error: ") + ex.what();
      }
    });

    // Detach thread (state tracked via g_forge_states)
    forge_thread.detach();

    return neamc::api::HttpResponse::json({
        {"status", "started"},
        {"agent", agent_name},
        {"max_iterations", max_iterations},
        {"max_cost", max_cost}});
  }

  return neamc::api::HttpResponse::bad_request("Unknown action: " + action + ". Use start, stop, or status.");
}

// v0.7.2 W8: GET /api/v1/metrics — detailed percentile metrics
neamc::api::HttpResponse handle_metrics(const neamc::api::HttpRequest& request)
{
  if (!authenticate_request(request))
  {
    return unauthorized_response();
  }

  nlohmann::json response;

  // Request percentiles
  if (g_metrics)
  {
    auto snap = g_metrics->compute();
    response["requests"] = {{"p50_ms", snap.p50_ms},
                            {"p95_ms", snap.p95_ms},
                            {"p99_ms", snap.p99_ms},
                            {"count", snap.count}};
  }

  // VM pool stats
  if (g_vm_pool)
  {
    auto ps = g_vm_pool->stats();
    response["vm_pool"] = {{"total", ps.total},
                           {"available", ps.available},
                           {"in_use", ps.in_use},
                           {"peak", ps.peak}};
  }

  // Bytecode cache stats
  if (g_bytecode_cache)
  {
    auto cs = g_bytecode_cache->stats();
    response["bytecode_cache"] = {{"entries", cs.entries},
                                  {"hits", cs.hits},
                                  {"misses", cs.misses}};
  }

  // LLM connection pool stats
  auto llm_stats = neamc::llm::connection_pool_stats();
  response["llm_pool"] = {{"created", llm_stats.created},
                          {"reused", llm_stats.reused},
                          {"evicted", llm_stats.evicted},
                          {"idle", llm_stats.current_idle}};

  return neamc::api::HttpResponse::json(response);
}

void print_usage(const char* program_name)
{
  std::cout << "Usage: " << program_name << " [options]\n"
            << "\nOptions:\n"
            << "  --host HOST           Host to bind to (default: 0.0.0.0)\n"
            << "  --port PORT           Port to listen on (default: 8080)\n"
            << "  --workers N           Thread pool size (default: 4)\n"
            << "  --timeout MS          Request timeout in ms (default: 60000)\n"
            << "  --pool-size N         VM pool size override (default: workers value)\n"
            << "  --llm-pool-size N     LLM connection pool size (default: 8)\n"
            << "  --bytecode DIR        Load AOT-compiled .neamb files from directory\n"
            << "  --drain-seconds N     Graceful shutdown drain period (default: 10)\n"
            << "  --program FILE        Load .neam file for claw/forge agents\n"
            << "  --audit-sink SINK     Audit log sink: stderr|file|json (default: stderr)\n"
            << "  --audit-log PATH      Audit log file path (when sink=file)\n"
            << "  --help                Show this help message\n"
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
            << "  GET  /api/v1/metrics     Request metrics\n"
            << "  GET  /api/v1/claw        List claw agents\n"
            << "  POST /api/v1/claw/:agent/sessions/:key/message  Claw message\n"
            << "  GET  /api/v1/forge       List forge agents\n"
            << "  POST /api/v1/forge/:agent/start   Start forge loop\n"
            << "  POST /api/v1/forge/:agent/stop    Stop forge loop\n"
            << "  GET  /api/v1/forge/:agent/status  Forge loop status\n"
            << "  GET  /ready              Readiness check (alias)\n"
            << "\nEnvironment Variables:\n"
            << "  NEAM_WARMSTART_TRACE  Set to 1 for per-request JSON stderr logging\n";

  std::cout << "\nExample:\n"
            << "  curl -X POST http://localhost:8080/api/v1/agent/ask \\\n"
            << "    -H \"Content-Type: application/json\" \\\n"
            << "    -d '{\"agent_id\": \"assistant\", \"query\": \"Hello!\"}'\n";
}

// v0.7.2 W9: SIGTERM/SIGINT handler for graceful shutdown
std::atomic<bool> g_shutdown_requested{false};

void signal_handler(int sig)
{
  (void)sig;
  g_draining.store(true);
  g_shutdown_requested.store(true);
}

}  // namespace

int main(int argc, char** argv)
{
  auto init_start = std::chrono::steady_clock::now();

  std::string host = "0.0.0.0";
  int port = 8080;
  int workers = 4;
  int pool_size = 0;  // 0 = use workers value
  int llm_pool_size = 8;
  std::string bytecode_dir;
  std::string audit_sink_str = "stderr";
  std::string audit_log_path;
  std::string program_file;  // v0.8 Phase 9: optional .neam file for claw/forge agents

  // 1. Parse CLI args
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
      if (workers < 1) workers = 1;
      if (workers > 64) workers = 64;
    }
    else if (arg == "--timeout" && i + 1 < argc)
    {
      g_request_timeout_ms = std::stol(argv[++i]);
    }
    else if (arg == "--pool-size" && i + 1 < argc)
    {
      pool_size = std::stoi(argv[++i]);
      if (pool_size < 1) pool_size = 1;
      if (pool_size > 128) pool_size = 128;
    }
    else if (arg == "--llm-pool-size" && i + 1 < argc)
    {
      llm_pool_size = std::stoi(argv[++i]);
      if (llm_pool_size < 1) llm_pool_size = 1;
      if (llm_pool_size > 256) llm_pool_size = 256;
    }
    else if (arg == "--bytecode" && i + 1 < argc)
    {
      bytecode_dir = argv[++i];
    }
    else if (arg == "--drain-seconds" && i + 1 < argc)
    {
      g_drain_seconds = std::stoi(argv[++i]);
      if (g_drain_seconds < 1) g_drain_seconds = 1;
      if (g_drain_seconds > 300) g_drain_seconds = 300;
    }
    else if (arg == "--program" && i + 1 < argc)
    {
      program_file = argv[++i];
    }
    else
    {
      std::cerr << "Unknown option: " << arg << "\n";
      print_usage(argv[0]);
      return 1;
    }
  }

  // 2. Configure audit logging
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

  // 3. Configure LLM connection pool
  neamc::llm::ConnectionPoolConfig llm_cfg;
  llm_cfg.max_pool_size = static_cast<size_t>(llm_pool_size);
  neamc::llm::configure_connection_pool(llm_cfg);

  // Check for API key
  const char* openai_key = std::getenv("OPENAI_API_KEY");
  if (!openai_key || std::strlen(openai_key) == 0)
  {
    std::cerr << "Warning: OPENAI_API_KEY environment variable not set.\n";
    std::cerr << "         OpenAI agents will not work without it.\n\n";
  }

  // Check NEAM_WARMSTART_TRACE
  const char* trace_env = std::getenv("NEAM_WARMSTART_TRACE");
  if (trace_env && std::string(trace_env) == "1")
  {
    g_warmstart_trace = true;
  }

  // 4. Load agents: neam.toml [[agents]] → fallback to kAgents
  neamc::vm::BytecodeCache bytecode_cache;
  std::map<std::string, AgentConfig> dynamic_agents;
  bool using_manifest_agents = false;

  {
    auto manifest_path = std::filesystem::path("neam.toml");
    if (std::filesystem::exists(manifest_path))
    {
      auto manifest = neamc::load_project_manifest(manifest_path);
      if (!manifest.agents.empty())
      {
        for (const auto& entry : manifest.agents)
        {
          if (entry.id.empty()) continue;
          AgentConfig cfg;
          cfg.name = entry.name.empty() ? entry.id : entry.name;
          cfg.provider = entry.provider.empty() ? "openai" : entry.provider;
          cfg.model = entry.model.empty() ? "gpt-4o-mini" : entry.model;
          cfg.system_prompt = entry.system_prompt;
          cfg.knowledge_base = entry.knowledge_base;
          dynamic_agents[entry.id] = cfg;

          // Pre-compile: source file or generated template
          if (!entry.source.empty())
          {
            bytecode_cache.precompile_file(entry.id, entry.source);
          }
          else
          {
            auto tmpl = generate_agent_template(entry.id, cfg);
            bytecode_cache.preload(entry.id, tmpl);
          }
        }
        using_manifest_agents = true;
        std::cout << "Loaded " << manifest.agents.size()
                  << " agents from neam.toml\n";
      }
    }
  }

  // Fallback to kAgents if no manifest agents
  if (!using_manifest_agents)
  {
    for (const auto& [id, config] : kAgents)
    {
      std::string tmpl = generate_agent_template(id, config);
      bytecode_cache.preload(id, tmpl);
    }
  }

  // 5. Load AOT bytecode from --bytecode dir (overrides source-compiled)
  if (!bytecode_dir.empty())
  {
    namespace fs = std::filesystem;
    if (fs::exists(bytecode_dir) && fs::is_directory(bytecode_dir))
    {
      int aot_count = 0;
      for (const auto& entry : fs::directory_iterator(bytecode_dir))
      {
        if (entry.path().extension() != ".neamb") continue;
        std::ifstream in(entry.path(), std::ios::binary);
        if (!in) continue;
        auto bc = neamc::vm::Bytecode::deserialize(in);
        std::string key = entry.path().stem().string();
        bytecode_cache.load_bytecode(key, std::move(bc));
        ++aot_count;
      }
      std::cout << "AOT: loaded " << aot_count << " bytecode files from "
                << bytecode_dir << "\n";
    }
    else
    {
      std::cerr << "Warning: --bytecode directory not found: " << bytecode_dir << "\n";
    }
  }

  // 6. Create VM pool with ResetPolicy
  int effective_pool = pool_size > 0 ? pool_size : workers;
  neamc::vm::VMPool::Config pool_cfg;
  pool_cfg.min_vms = static_cast<size_t>(effective_pool);
  pool_cfg.max_vms = static_cast<size_t>(effective_pool * 2);
  pool_cfg.timeout_ms = g_request_timeout_ms;
  pool_cfg.reset_policy = neamc::vm::ResetPolicy::Standard;
  neamc::vm::VMPool vm_pool(pool_cfg);

  // 7. Warm VM pool
  vm_pool.warm(pool_cfg.min_vms);

  // 8. Pre-warm LLM connections
  neamc::llm::prewarm_connection_pool({"https://api.openai.com"});

  // 9. Create RequestMetrics
  neamc::vm::RequestMetrics metrics;

  // Make pool/cache/metrics accessible to handlers
  g_bytecode_cache = &bytecode_cache;
  g_vm_pool = &vm_pool;
  g_metrics = &metrics;
  if (using_manifest_agents)
  {
    g_active_agents = &dynamic_agents;
  }

  // 9.5. v0.8 Phase 9: Load .neam program for claw/forge agents
  neamc::vm::VirtualMachine program_vm;
  if (!program_file.empty())
  {
    try
    {
      std::ifstream pf(program_file);
      if (!pf)
      {
        std::cerr << "Error: cannot open program file: " << program_file << "\n";
        return 1;
      }
      std::string source((std::istreambuf_iterator<char>(pf)),
                          std::istreambuf_iterator<char>());

      neamc::Pipeline pipeline;
      auto unit = pipeline.compile(source, {});

      std::ostringstream out;
      std::istringstream in;
      program_vm.set_io(&in, &out);
      program_vm.run(unit.chunk);

      g_vm = &program_vm;

      std::cout << "Loaded program: " << program_file
                << " (" << g_vm->claw_agents().size() << " claw, "
                << g_vm->forge_agents().size() << " forge agents)\n";
    }
    catch (const std::exception& ex)
    {
      std::cerr << "Error loading program: " << ex.what() << "\n";
      return 1;
    }
  }

  // 10. Set ready, log startup time
  g_hotvm_ready.store(true);
  auto init_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - init_start)
                          .count();

  const auto& agents_ref = using_manifest_agents ? dynamic_agents : kAgents;
  std::cout << "HotVM: " << agents_ref.size() << " agents precompiled, "
            << effective_pool << " VMs warmed\n";
  std::cout << "Ready in " << init_elapsed << "ms\n";

  // 11. Install SIGTERM/SIGINT handler
  std::signal(SIGTERM, signal_handler);
  std::signal(SIGINT, signal_handler);

  try
  {
    neamc::api::HttpServer server(port, host);

    // Enable CORS for cross-origin requests
    server.enable_cors();

    // 12. Register routes including /api/v1/metrics
    server.get("/api/v1/health", handle_health);
    server.get("/api/v1/agents", handle_list_agents);
    server.post("/api/v1/agent/ask", handle_agent_ask);
    server.get("/api/v1/metrics", handle_metrics);

    // Root-level health aliases for K8s probes
    server.get("/health", handle_health);
    server.get("/ready", handle_ready);

    // Admin routes (D9 behavioral monitoring)
    server.post("/api/v1/admin/disable", handle_disable_agent);
    server.post("/api/v1/admin/enable", handle_enable_agent);
    server.get("/api/v1/admin/status", handle_admin_status);

    // D10 Human-in-the-Loop confirmation routes
    server.post("/api/v1/confirm", handle_confirm);
    server.get("/api/v1/admin/confirmations", handle_list_confirmations);

    // v0.8 Phase 9: Claw agent endpoints
    server.get("/api/v1/claw", handle_list_claw);
    server.post("/api/v1/claw/", handle_claw_message);  // prefix match

    // v0.8 Phase 9: Forge agent endpoints
    server.get("/api/v1/forge", handle_list_forge);
    server.post("/api/v1/forge/", handle_forge_action);  // prefix match
    server.get("/api/v1/forge/", handle_forge_action);   // prefix match for status

    std::cout << "Starting Neam API Server...\n";
    std::cout << "  Host: " << host << "\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  Workers: " << workers << "\n";
    std::cout << "  LLM pool: " << llm_pool_size << "\n";
    if (!bytecode_dir.empty())
    {
      std::cout << "  Bytecode: " << bytecode_dir << "\n";
    }
    std::cout << "  Audit: sink=" << audit_sink_str;
    if (!audit_log_path.empty())
    {
      std::cout << " file=" << audit_log_path;
    }
    std::cout << "\n\n";

    // 13. Run server in a thread for graceful shutdown
    std::thread server_thread([&server]() {
      server.run();
    });

    // Wait for shutdown signal
    while (!g_shutdown_requested.load())
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Drain period — let in-flight requests complete
    std::cout << "SIGTERM received, draining for " << g_drain_seconds << "s...\n";

    // Signal all running forge loops to stop
    {
      std::lock_guard<std::mutex> lock(g_forge_mutex);
      for (auto& [name, state] : g_forge_states)
      {
        if (state.running) state.stop_requested = true;
      }
    }

    std::this_thread::sleep_for(std::chrono::seconds(g_drain_seconds));

    std::cout << "Shutdown complete.\n";
    // server destructor will stop the server
    if (server_thread.joinable())
    {
      server_thread.detach();
    }
  }
  catch (const std::exception& ex)
  {
    std::cerr << "neam-api error: " << ex.what() << "\n";
    return 1;
  }

  return 0;
}
