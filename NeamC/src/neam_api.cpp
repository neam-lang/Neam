// SPDX-License-Identifier: Apache-2.0
//
// Neam API - Native REST API Server for Neam Agents
//
// Exposes Neam agents as HTTP endpoints, enabling integration with any application.
//

#include "neamc/api/a2a_handler.hpp"
#include "neamc/api/http_server.hpp"
#include "neamc/pipeline.hpp"
#include "neamc/vm/health_manager.hpp"
#include "neamc/vm/vm.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>

namespace
{

// Forward declarations
std::string escape_string(const std::string& input);

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

// Global mutex for thread-safe operations
std::mutex g_vm_mutex;

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

// Execute Neam program and capture output
std::string execute_neam_program(const std::string& source)
{
  std::lock_guard<std::mutex> lock(g_vm_mutex);

  try
  {
    neamc::Pipeline pipeline;
    auto unit = pipeline.compile(source, {});

    // Create output stream to capture emitted values
    std::ostringstream output_stream;
    std::istringstream input_stream;

    neamc::vm::VirtualMachine vm;
    vm.set_io(&input_stream, &output_stream);
    vm.run(unit.chunk);

    // Get emitted values
    const auto& emitted = vm.emitted();
    if (!emitted.empty())
    {
      // Return the last emitted value as string
      return value_to_string(emitted.back());
    }

    // Fallback to output stream content
    return output_stream.str();
  }
  catch (const std::exception& ex)
  {
    return std::string("Error: ") + ex.what();
  }
}

// API Routes

// Health check endpoints (v0.6.0)
static std::unique_ptr<neamc::vm::HealthManager> g_health_manager;

neamc::api::HttpResponse handle_health(const neamc::api::HttpRequest&)
{
  if (g_health_manager)
  {
    return neamc::api::HttpResponse::json(g_health_manager->check_health());
  }
  nlohmann::json response = {{"status", "healthy"}, {"version", "0.6.0"}, {"server", "neam-api"}};
  return neamc::api::HttpResponse::json(response);
}

neamc::api::HttpResponse handle_ready(const neamc::api::HttpRequest&)
{
  if (g_health_manager)
  {
    auto result = g_health_manager->check_readiness();
    bool ready = result.value("ready", false);
    auto resp = neamc::api::HttpResponse::json(result);
    if (!ready) resp.status_code = 503;
    return resp;
  }
  return neamc::api::HttpResponse::json({{"ready", true}});
}

neamc::api::HttpResponse handle_startup(const neamc::api::HttpRequest&)
{
  if (g_health_manager)
  {
    auto result = g_health_manager->check_startup();
    bool started = result.value("started", false);
    auto resp = neamc::api::HttpResponse::json(result);
    if (!started) resp.status_code = 503;
    return resp;
  }
  return neamc::api::HttpResponse::json({{"started", true}});
}

// GET /api/v1/agents
neamc::api::HttpResponse handle_list_agents(const neamc::api::HttpRequest&)
{
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
            << "  --host HOST              Host to bind to (default: 0.0.0.0)\n"
            << "  --port PORT              Port to listen on (default: 8080)\n"
            << "  --agent-file <path>      Load compiled agent bundle (.neamb) for A2A\n"
            << "  --a2a                    Enable A2A protocol endpoints\n"
            << "  --help                   Show this help message\n"
            << "\nEnvironment Variables:\n"
            << "  OPENAI_API_KEY   Required for OpenAI agents\n"
            << "\nAPI Endpoints:\n"
            << "  GET  /api/v1/health              Health check\n"
            << "  GET  /api/v1/agents              List available agents\n"
            << "  POST /api/v1/agent/ask           Query an agent\n"
            << "\nA2A Endpoints (with --a2a):\n"
            << "  GET  /.well-known/agent.json     A2A agent card discovery\n"
            << "  POST /a2a                        JSON-RPC 2.0 (tasks/send, tasks/get, tasks/cancel)\n"
            << "  GET  /a2a/tasks/{id}/stream      SSE task result stream\n"
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
            << "    -d '{\"agent_id\": \"assistant\", \"query\": \"Hello!\"}'\n"
            << "\nA2A Example:\n"
            << "  " << program_name << " --port 9090 --agent-file agent.neamb --a2a\n"
            << "  curl http://localhost:9090/.well-known/agent.json\n";
}

}  // namespace

int main(int argc, char** argv)
{
  std::string host = "0.0.0.0";
  int port = 8080;
  std::string agent_file;
  bool a2a_enabled = false;

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
    else if (arg == "--agent-file" && i + 1 < argc)
    {
      agent_file = argv[++i];
    }
    else if (arg == "--a2a")
    {
      a2a_enabled = true;
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

    // Initialize health manager (v0.6.0)
    g_health_manager = std::make_unique<neamc::vm::HealthManager>();

    // Enable CORS for cross-origin requests
    server.enable_cors();

    // Register standard routes
    server.get("/api/v1/health", handle_health);
    server.get("/health", handle_health);
    server.get("/ready", handle_ready);
    server.get("/startup", handle_startup);
    server.get("/api/v1/agents", handle_list_agents);
    server.post("/api/v1/agent/ask", handle_agent_ask);

    // A2A mode: load agent bundle and register A2A endpoints
    std::unique_ptr<neamc::vm::VirtualMachine> a2a_vm;
    std::unique_ptr<neamc::api::A2AHandler> a2a_handler;

    if (a2a_enabled)
    {
      a2a_vm = std::make_unique<neamc::vm::VirtualMachine>();

      if (!agent_file.empty())
      {
        std::ifstream bundle(agent_file, std::ios::binary);
        if (!bundle)
        {
          std::cerr << "Failed to open agent bundle: " << agent_file << "\n";
          return 1;
        }
        neamc::vm::Chunk chunk = neamc::vm::Chunk::deserialize(bundle);
        a2a_vm->run(chunk);  // Executes agent definitions including OP_DEFINE_AGENT_CARD
        std::cout << "  Loaded agent bundle: " << agent_file << "\n";
      }

      a2a_handler = std::make_unique<neamc::api::A2AHandler>(*a2a_vm, g_vm_mutex);
      a2a_handler->register_routes(server);
    }

    std::cout << "Starting Neam API Server...\n";
    std::cout << "  Host: " << host << "\n";
    std::cout << "  Port: " << port << "\n";
    std::cout << "  Endpoints:\n";
    std::cout << "    GET  /api/v1/health\n";
    std::cout << "    GET  /health          (v0.6.0)\n";
    std::cout << "    GET  /ready           (v0.6.0)\n";
    std::cout << "    GET  /startup         (v0.6.0)\n";
    std::cout << "    GET  /api/v1/agents\n";
    std::cout << "    POST /api/v1/agent/ask\n";
    if (a2a_enabled)
    {
      std::cout << "    GET  /.well-known/agent.json  (A2A)\n";
      std::cout << "    POST /a2a                     (A2A)\n";
      std::cout << "    GET  /a2a/tasks/*/stream       (A2A)\n";
    }
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
