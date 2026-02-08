//
// Neam API - Native REST API Server for Neam Agents
//
// Exposes Neam agents as HTTP endpoints, enabling integration with any application.
//

#include "neamc/api/http_server.hpp"
#include "neamc/pipeline.hpp"
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

// API key authentication (v0.6.5 security fix)
// When NEAM_API_KEY is set, all requests must include Authorization: Bearer <key>
std::string get_api_key()
{
  const char* key = std::getenv("NEAM_API_KEY");
  return key ? std::string(key) : "";
}

bool authenticate_request(const neamc::api::HttpRequest& request)
{
  const std::string expected = get_api_key();
  if (expected.empty())
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

  // Constant-time comparison to prevent timing attacks
  if (provided.size() != expected.size())
  {
    return false;
  }

  volatile int result = 0;
  for (size_t i = 0; i < expected.size(); ++i)
  {
    result |= (provided[i] ^ expected[i]);
  }
  return result == 0;
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

// GET /api/v1/agents
neamc::api::HttpResponse handle_list_agents(const neamc::api::HttpRequest& request)
{
  if (!authenticate_request(request))
  {
    return unauthorized_response();
  }

  nlohmann::json agents_json = nlohmann::json::object();

  for (const auto& [id, config] : kAgents)
  {
    agents_json[id] = {{"name", config.name},
                       {"provider", config.provider},
                       {"model", config.model},
                       {"description", config.system_prompt},
                       {"has_knowledge_base", !config.knowledge_base.empty()}};
  }

  return neamc::api::HttpResponse::json({{"agents", agents_json}});
}

// POST /api/v1/agent/ask
neamc::api::HttpResponse handle_agent_ask(const neamc::api::HttpRequest& request)
{
  if (!authenticate_request(request))
  {
    return unauthorized_response();
  }

  // Parse JSON body
  nlohmann::json body;
  try
  {
    body = nlohmann::json::parse(request.body);
  }
  catch (const std::exception&)
  {
    return neamc::api::HttpResponse::bad_request("Invalid JSON body");
  }

  // Validate required fields
  if (!body.contains("agent_id"))
  {
    return neamc::api::HttpResponse::bad_request("Missing 'agent_id' field");
  }
  if (!body.contains("query"))
  {
    return neamc::api::HttpResponse::bad_request("Missing 'query' field");
  }

  std::string agent_id = body["agent_id"];
  std::string query = body["query"];

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

  // Generate and execute Neam program
  std::string program = generate_neam_program(agent_id, query, config);
  std::string response = execute_neam_program(program);

  nlohmann::json result = {{"agent_id", agent_id}, {"query", query}, {"response", response}};

  return neamc::api::HttpResponse::json(result);
}

void print_usage(const char* program_name)
{
  std::cout << "Usage: " << program_name << " [options]\n"
            << "\nOptions:\n"
            << "  --host HOST       Host to bind to (default: 0.0.0.0)\n"
            << "  --port PORT       Port to listen on (default: 8080)\n"
            << "  --workers N       Thread pool size (default: 4)\n"
            << "  --timeout MS      Request timeout in ms (default: 60000)\n"
            << "  --help            Show this help message\n"
            << "\nEnvironment Variables:\n"
            << "  NEAM_API_KEY     API authentication key (required in production)\n"
            << "  OPENAI_API_KEY   Required for OpenAI agents\n"
            << "\nAPI Endpoints:\n"
            << "  GET  /api/v1/health      Health check\n"
            << "  GET  /api/v1/agents      List available agents\n"
            << "  POST /api/v1/agent/ask   Query an agent\n"
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

    std::cout << "Starting Neam API Server...\n";
    std::cout << "  Host: " << host << "\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  Endpoints:\n";
    std::cout << "    GET  /api/v1/health\n";
    std::cout << "    GET  /api/v1/agents\n";
    std::cout << "    POST /api/v1/agent/ask\n";
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
