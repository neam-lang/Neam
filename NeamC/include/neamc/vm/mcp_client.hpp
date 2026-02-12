//
// Neam Virtual Machine - MCP Client (v0.6.9)
//
// JSON-RPC 2.0 over stdio transport for Model Context Protocol.
// Manages child MCP server processes and provides tools/list + tools/call.
// v0.6.9 D6: Resource limits, environment isolation, hash pinning.
//

#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

namespace neamc::vm
{

struct McpToolInfo
{
  std::string name;
  std::string description;
  nlohmann::json input_schema;
};

class McpClient
{
public:
  // v0.6.9 D6: Extended constructor with resource limits and hash pinning
  McpClient(const std::string& command, const std::vector<std::string>& args,
            const std::unordered_map<std::string, std::string>& env = {},
            size_t max_memory_bytes = 268435456,   // 256MB
            size_t max_cpu_seconds = 300,           // 5 min
            long startup_timeout_ms = 30000,        // 30s
            const std::string& expected_hash = ""); // SHA-256 of binary
  ~McpClient();

  McpClient(const McpClient&) = delete;
  McpClient& operator=(const McpClient&) = delete;

  bool initialize();
  std::vector<McpToolInfo> list_tools();
  nlohmann::json call_tool(const std::string& name, const nlohmann::json& args);
  void shutdown();
  bool is_connected() const
  {
#ifdef _WIN32
    return child_handle_ != nullptr;
#else
    return child_pid_ > 0;
#endif
  }

private:
  nlohmann::json send_request(const std::string& method, const nlohmann::json& params);
  nlohmann::json read_response(int id, long timeout_ms = 30000);
  void start_process();
  void kill_process();

  // v0.6.9 D6: Verify binary hash before launch
  void verify_hash();
  // v0.6.9 D6: Compute SHA-256 of a file
  static std::string compute_sha256_file(const std::string& path);

  std::string command_;
  std::vector<std::string> args_;
  std::unordered_map<std::string, std::string> env_;

  // v0.6.9 D6: Resource limits
  size_t max_memory_bytes_;
  size_t max_cpu_seconds_;
  long startup_timeout_ms_;
  std::string expected_hash_;

#ifdef _WIN32
  void* child_handle_{nullptr};
  void* stdin_handle_{nullptr};
  void* stdout_handle_{nullptr};
#else
  int child_pid_{-1};
  int stdin_fd_{-1};
  int stdout_fd_{-1};
#endif

  int next_id_{1};
  bool initialized_{false};
};

}  // namespace neamc::vm
