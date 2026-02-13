//
// Neam Lambda - Custom AWS Lambda Runtime (v0.7.2 HotVM)
//
// Purpose-built C++ Lambda custom runtime that eliminates Lambda Web Adapter
// overhead. Uses the Lambda Runtime API directly for minimal cold-start latency.
//
// Flow:
//   INIT (once): Load agents, compile bytecode, warm VM pool, prewarm LLM connections
//   REQUEST LOOP: GET /next → route → execute → POST /response
//

#include "neamc/llm/http_client.hpp"
#include "neamc/pipeline.hpp"
#include "neamc/project_manifest.hpp"
#include "neamc/version.hpp"
#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/bytecode_cache.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/vm_pool.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace
{

// Agent configuration (mirrors neam_api.cpp)
struct AgentConfig
{
  std::string name;
  std::string provider;
  std::string model;
  std::string system_prompt;
  std::string knowledge_base;
};

std::map<std::string, AgentConfig> g_agents;
neamc::vm::BytecodeCache g_cache;
std::unique_ptr<neamc::vm::VMPool> g_pool;

std::string escape_string(const std::string& input)
{
  std::string result;
  result.reserve(input.size() * 2);
  for (char c : input)
  {
    switch (c)
    {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += c; break;
    }
  }
  return result;
}

std::string generate_agent_template(const std::string& agent_id, const AgentConfig& config)
{
  std::ostringstream program;
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
  program << "agent QueryAgent {\n";
  program << "  provider: \"" << config.provider << "\"\n";
  program << "  model: \"" << config.model << "\"\n";
  program << "  system: \"" << config.system_prompt << "\"";
  if (!config.knowledge_base.empty())
  {
    program << "\n  connected_knowledge: [AgentKB]";
  }
  program << "\n}\n\n";
  program << "{\n";
  program << "  let response = QueryAgent.ask(__query__);\n";
  program << "  emit response;\n";
  program << "}\n";
  return program.str();
}

std::string execute_with_hotvm(const std::string& agent_id, const std::string& query)
{
  try
  {
    auto vm = g_pool->acquire();
    if (!vm.get()) return "Error: VM pool exhausted";

    auto* key_str = neamc::vm::copy_string("__query__", 9);
    vm->globals().set(key_str, neamc::vm::Value::String(query.c_str(), query.size()));

    const auto* bytecode = g_cache.get(agent_id);
    if (!bytecode) return "Error: Agent not preloaded: " + agent_id;

    std::ostringstream out;
    std::istringstream in;
    vm->set_io(&in, &out);
    vm->run(*bytecode);

    const auto& emitted = vm->emitted();
    return !emitted.empty() ? neamc::vm::value_to_string(emitted.back()) : out.str();
  }
  catch (const std::exception& ex)
  {
    return std::string("Error: ") + ex.what();
  }
}

// Lambda Runtime API helpers — direct curl to capture response headers
struct LambdaInvocation
{
  std::string request_id;
  std::string event_body;
};

size_t write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* s = static_cast<std::string*>(userdata);
  s->append(ptr, size * nmemb);
  return size * nmemb;
}

size_t header_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* inv = static_cast<LambdaInvocation*>(userdata);
  std::string line(ptr, size * nmemb);
  const std::string prefix = "lambda-runtime-aws-request-id:";
  // Case-insensitive header match
  std::string lower_line = line;
  for (auto& c : lower_line)
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (lower_line.substr(0, prefix.size()) == prefix)
  {
    std::string val = line.substr(prefix.size());
    while (!val.empty() && (val.front() == ' ' || val.front() == '\t'))
      val.erase(val.begin());
    while (!val.empty() && (val.back() == '\r' || val.back() == '\n' || val.back() == ' '))
      val.pop_back();
    inv->request_id = val;
  }
  return size * nmemb;
}

LambdaInvocation lambda_get_next(const std::string& url)
{
  LambdaInvocation inv;
  std::string body;
  CURL* curl = curl_easy_init();
  if (!curl) throw std::runtime_error("curl_easy_init failed");

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &inv);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 0L);

  CURLcode res = curl_easy_perform(curl);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK)
    throw std::runtime_error(std::string("Lambda GET /next failed: ") +
                             curl_easy_strerror(res));

  inv.event_body = std::move(body);
  return inv;
}

void lambda_post(const std::string& url, const std::string& body)
{
  CURL* curl = curl_easy_init();
  if (!curl) throw std::runtime_error("curl_easy_init failed for POST");

  struct curl_slist* hdrs = nullptr;
  hdrs = curl_slist_append(hdrs, "Content-Type: application/json");

  std::string discard;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdrs);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &discard);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(hdrs);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK)
    std::cerr << "Lambda POST failed: " << curl_easy_strerror(res) << "\n";
}

// Load agents from neam.toml or use defaults
void load_agents()
{
  auto manifest_path = std::filesystem::path("neam.toml");
  if (std::filesystem::exists(manifest_path))
  {
    auto manifest = neamc::load_project_manifest(manifest_path);
    for (const auto& entry : manifest.agents)
    {
      if (entry.id.empty()) continue;
      AgentConfig cfg;
      cfg.name = entry.name.empty() ? entry.id : entry.name;
      cfg.provider = entry.provider.empty() ? "openai" : entry.provider;
      cfg.model = entry.model.empty() ? "gpt-4o-mini" : entry.model;
      cfg.system_prompt = entry.system_prompt;
      cfg.knowledge_base = entry.knowledge_base;

      if (!entry.source.empty())
      {
        g_cache.precompile_file(entry.id, entry.source);
      }
      else
      {
        auto tmpl = generate_agent_template(entry.id, cfg);
        g_cache.preload(entry.id, tmpl);
      }
      g_agents[entry.id] = std::move(cfg);
    }
  }

  // Default agents if none configured
  if (g_agents.empty())
  {
    g_agents["assistant"] = {"Assistant", "openai", "gpt-4o-mini",
                             "You are a helpful assistant.", ""};
    for (const auto& [id, cfg] : g_agents)
    {
      auto tmpl = generate_agent_template(id, cfg);
      g_cache.preload(id, tmpl);
    }
  }
}

// Load AOT bytecode from a directory
void load_bytecode_dir(const std::string& dir)
{
  namespace fs = std::filesystem;
  for (const auto& entry : fs::directory_iterator(dir))
  {
    if (entry.path().extension() != ".neamb") continue;
    std::ifstream in(entry.path(), std::ios::binary);
    if (!in) continue;
    auto bytecode = neamc::vm::Bytecode::deserialize(in);
    std::string key = entry.path().stem().string();
    g_cache.load_bytecode(key, std::move(bytecode));
    std::cerr << "  AOT loaded: " << key << "\n";
  }
}

// Handle a Lambda invocation event
std::string handle_event(const std::string& event_json)
{
  try
  {
    auto event = nlohmann::json::parse(event_json);

    // API Gateway v2 event format
    std::string raw_path = event.value("rawPath", "");
    std::string http_method = "GET";
    if (event.contains("requestContext") &&
        event["requestContext"].contains("http"))
    {
      http_method = event["requestContext"]["http"].value("method", "GET");
    }
    std::string body_str = event.value("body", "");

    // Route: /health or /ready
    if (raw_path == "/health" || raw_path == "/ready" || raw_path == "/api/v1/health")
    {
      nlohmann::json resp = {{"status", "healthy"}, {"version", NEAM_VERSION},
                             {"server", "neam-lambda"}, {"agents", g_agents.size()}};
      nlohmann::json apigw = {{"statusCode", 200},
                              {"headers", {{"Content-Type", "application/json"}}},
                              {"body", resp.dump()}};
      return apigw.dump();
    }

    // Route: /api/v1/agents
    if (raw_path == "/api/v1/agents" && http_method == "GET")
    {
      nlohmann::json agents_json = nlohmann::json::object();
      for (const auto& [id, cfg] : g_agents)
      {
        agents_json[id] = {{"name", cfg.name}, {"provider", cfg.provider},
                           {"model", cfg.model}};
      }
      nlohmann::json apigw = {{"statusCode", 200},
                              {"headers", {{"Content-Type", "application/json"}}},
                              {"body", nlohmann::json({{"agents", agents_json}}).dump()}};
      return apigw.dump();
    }

    // Route: /api/v1/agent/ask
    if (raw_path == "/api/v1/agent/ask" && http_method == "POST")
    {
      nlohmann::json req_body;
      try { req_body = nlohmann::json::parse(body_str); }
      catch (...) {
        nlohmann::json apigw = {{"statusCode", 400},
                                {"headers", {{"Content-Type", "application/json"}}},
                                {"body", R"({"error":"Invalid JSON body"})"}};
        return apigw.dump();
      }

      std::string agent_id = req_body.value("agent_id", "");
      std::string query = req_body.value("query", "");
      // Default to first agent if only one is configured
      if (agent_id.empty() && g_agents.size() == 1)
        agent_id = g_agents.begin()->first;
      if (agent_id.empty() || query.empty())
      {
        nlohmann::json apigw = {{"statusCode", 400},
                                {"headers", {{"Content-Type", "application/json"}}},
                                {"body", R"({"error":"Missing agent_id or query"})"}};
        return apigw.dump();
      }

      if (g_agents.find(agent_id) == g_agents.end())
      {
        nlohmann::json apigw = {{"statusCode", 400},
                                {"headers", {{"Content-Type", "application/json"}}},
                                {"body", R"({"error":"Unknown agent"})"}};
        return apigw.dump();
      }

      auto response = execute_with_hotvm(agent_id, query);
      nlohmann::json result = {{"agent_id", agent_id}, {"query", query},
                               {"response", response}};
      nlohmann::json apigw = {{"statusCode", 200},
                              {"headers", {{"Content-Type", "application/json"}}},
                              {"body", result.dump()}};
      return apigw.dump();
    }

    // 404 for unknown routes
    nlohmann::json apigw = {{"statusCode", 404},
                            {"headers", {{"Content-Type", "application/json"}}},
                            {"body", R"({"error":"Not Found"})"}};
    return apigw.dump();
  }
  catch (const std::exception& ex)
  {
    nlohmann::json apigw = {{"statusCode", 500},
                            {"headers", {{"Content-Type", "application/json"}}},
                            {"body", nlohmann::json({{"error", ex.what()}}).dump()}};
    return apigw.dump();
  }
}

}  // namespace

int main(int argc, char** argv)
{
  auto init_start = std::chrono::steady_clock::now();

  // Parse args
  std::string bytecode_dir;
  for (int i = 1; i < argc; ++i)
  {
    std::string arg = argv[i];
    if (arg == "--bytecode" && i + 1 < argc)
    {
      bytecode_dir = argv[++i];
    }
  }

  // --- INIT PHASE ---
  const char* runtime_api = std::getenv("AWS_LAMBDA_RUNTIME_API");
  if (!runtime_api || std::strlen(runtime_api) == 0)
  {
    std::cerr << "Error: AWS_LAMBDA_RUNTIME_API not set. "
              << "This binary is designed to run inside AWS Lambda.\n";
    return 1;
  }
  std::string api_base = std::string("http://") + runtime_api;

  // Load agents
  if (!bytecode_dir.empty())
  {
    load_bytecode_dir(bytecode_dir);
  }
  load_agents();

  // Create VM pool (Lambda: small pool, 1-2 VMs)
  neamc::vm::VMPool::Config pool_cfg;
  pool_cfg.min_vms = 1;
  pool_cfg.max_vms = 2;
  pool_cfg.timeout_ms = 30000;
  pool_cfg.reset_policy = neamc::vm::ResetPolicy::Standard;
  g_pool = std::make_unique<neamc::vm::VMPool>(pool_cfg);
  g_pool->warm(1);

  // Pre-warm LLM connections
  neamc::llm::prewarm_connection_pool({"https://api.openai.com"});

  auto init_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - init_start)
                          .count();
  std::cerr << "neam-lambda INIT complete in " << init_elapsed << "ms, "
            << g_agents.size() << " agents loaded\n";

  // --- REQUEST LOOP ---
  std::string next_url = api_base + "/2018-06-01/runtime/invocation/next";

  while (true)
  {
    std::string request_id = "unknown";
    try
    {
      // GET next invocation — blocks until event arrives
      // Uses direct curl to capture Lambda-Runtime-Aws-Request-Id header
      auto inv = lambda_get_next(next_url);
      request_id = inv.request_id;

      // Handle the event
      std::string response = handle_event(inv.event_body);

      // POST response
      std::string response_url = api_base + "/2018-06-01/runtime/invocation/" +
                                 request_id + "/response";
      lambda_post(response_url, response);
    }
    catch (const std::exception& ex)
    {
      std::cerr << "neam-lambda error: " << ex.what() << "\n";
      try
      {
        nlohmann::json err = {{"errorMessage", ex.what()},
                              {"errorType", "RuntimeError"}};
        std::string error_url = api_base + "/2018-06-01/runtime/invocation/" +
                                request_id + "/error";
        lambda_post(error_url, err.dump());
      }
      catch (...)
      {
        std::cerr << "Failed to report error to Lambda runtime\n";
      }
    }
  }

  return 0;
}
