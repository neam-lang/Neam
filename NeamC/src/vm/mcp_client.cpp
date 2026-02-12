//
// Neam Virtual Machine - MCP Client (v0.6.9)
//
// JSON-RPC 2.0 over stdio transport for Model Context Protocol.
// v0.6.9 D6: Resource limits, environment isolation, hash pinning.
//

#include "neamc/vm/mcp_client.hpp"

#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#ifdef __APPLE__
#include <crt_externs.h>  // _NSGetEnviron
#endif
#endif

#include <openssl/evp.h>

#include "neamc/security/audit_log.hpp"
#include "neamc/security/credential_manager.hpp"

namespace neamc::vm
{

McpClient::McpClient(const std::string& command, const std::vector<std::string>& args,
                     const std::unordered_map<std::string, std::string>& env,
                     size_t max_memory_bytes, size_t max_cpu_seconds,
                     long startup_timeout_ms, const std::string& expected_hash)
    : command_(command), args_(args), env_(env),
      max_memory_bytes_(max_memory_bytes), max_cpu_seconds_(max_cpu_seconds),
      startup_timeout_ms_(startup_timeout_ms), expected_hash_(expected_hash)
{
}

McpClient::~McpClient()
{
  kill_process();
}

void McpClient::start_process()
{
#ifdef _WIN32
  throw std::runtime_error("MCP client not yet supported on Windows");
#else
  int to_child[2];   // parent writes, child reads (child's stdin)
  int from_child[2]; // child writes, parent reads (child's stdout)

  if (pipe(to_child) != 0 || pipe(from_child) != 0)
  {
    throw std::runtime_error("MCP client: pipe() failed");
  }

  pid_t pid = fork();
  if (pid < 0)
  {
    close(to_child[0]);
    close(to_child[1]);
    close(from_child[0]);
    close(from_child[1]);
    throw std::runtime_error("MCP client: fork() failed");
  }

  if (pid == 0)
  {
    // Child process
    close(to_child[1]);    // Close parent's write end
    close(from_child[0]);  // Close parent's read end

    dup2(to_child[0], STDIN_FILENO);
    dup2(from_child[1], STDOUT_FILENO);
    close(to_child[0]);
    close(from_child[1]);

    // v0.6.9 D6+D7: Environment isolation — build clean env
    auto clean_env = security::CredentialManager::instance().build_clean_env(env_);

    // Clear inherited environment and set only declared vars
    // Use _NSGetEnviron on macOS, environ on Linux
#ifdef __APPLE__
    *_NSGetEnviron() = nullptr;
#else
    extern char** environ;
    environ = nullptr;
#endif
    for (const auto& [key, value] : clean_env)
    {
      setenv(key.c_str(), value.c_str(), 1);
    }

    // v0.6.9 D6: Resource limits for child process
    struct rlimit rl;

    // Address space limit
    rl.rlim_cur = max_memory_bytes_;
    rl.rlim_max = max_memory_bytes_;
    setrlimit(RLIMIT_AS, &rl);

    // CPU time limit
    rl.rlim_cur = max_cpu_seconds_;
    rl.rlim_max = max_cpu_seconds_;
    setrlimit(RLIMIT_CPU, &rl);

    // Max child processes
    rl.rlim_cur = 16;
    rl.rlim_max = 16;
    setrlimit(RLIMIT_NPROC, &rl);

    // Max file write size (10MB)
    rl.rlim_cur = 10 * 1024 * 1024;
    rl.rlim_max = 10 * 1024 * 1024;
    setrlimit(RLIMIT_FSIZE, &rl);

    // Build argv
    std::vector<const char*> argv;
    argv.push_back(command_.c_str());
    for (const auto& arg : args_)
    {
      argv.push_back(arg.c_str());
    }
    argv.push_back(nullptr);

    execvp(command_.c_str(), const_cast<char* const*>(argv.data()));
    _exit(127);  // exec failed
  }

  // Parent process
  close(to_child[0]);    // Close child's read end
  close(from_child[1]);  // Close child's write end

  stdin_fd_ = to_child[1];
  stdout_fd_ = from_child[0];
  child_pid_ = pid;

  // Ignore SIGPIPE so writes to dead child return EPIPE instead of killing us
  signal(SIGPIPE, SIG_IGN);

  // Set stdout_fd_ to non-blocking for timeout reads
  int flags = fcntl(stdout_fd_, F_GETFL, 0);
  fcntl(stdout_fd_, F_SETFL, flags | O_NONBLOCK);
#endif
}

void McpClient::kill_process()
{
#ifndef _WIN32
  if (child_pid_ > 0)
  {
    if (stdin_fd_ >= 0)
    {
      close(stdin_fd_);
      stdin_fd_ = -1;
    }
    if (stdout_fd_ >= 0)
    {
      close(stdout_fd_);
      stdout_fd_ = -1;
    }
    kill(child_pid_, SIGTERM);

    // Give it 2 seconds to exit gracefully
    int status = 0;
    auto start = std::chrono::steady_clock::now();
    while (true)
    {
      pid_t result = waitpid(child_pid_, &status, WNOHANG);
      if (result != 0)
      {
        break;
      }
      auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now() - start)
                         .count();
      if (elapsed > 2000)
      {
        kill(child_pid_, SIGKILL);
        waitpid(child_pid_, &status, 0);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    child_pid_ = -1;
  }
#endif
}

nlohmann::json McpClient::send_request(const std::string& method, const nlohmann::json& params)
{
#ifdef _WIN32
  throw std::runtime_error("MCP client not yet supported on Windows");
#else
  if (child_pid_ <= 0)
  {
    throw std::runtime_error("MCP client: not connected");
  }

  int id = next_id_++;
  nlohmann::json request = {
      {"jsonrpc", "2.0"},
      {"id", id},
      {"method", method}};
  if (!params.is_null())
  {
    request["params"] = params;
  }

  std::string msg = request.dump() + "\n";
  ssize_t written = write(stdin_fd_, msg.data(), msg.size());
  if (written < 0 || static_cast<size_t>(written) != msg.size())
  {
    throw std::runtime_error("MCP client: write failed");
  }

  return read_response(id);
#endif
}

nlohmann::json McpClient::read_response(int id, long timeout_ms)
{
#ifdef _WIN32
  throw std::runtime_error("MCP client not yet supported on Windows");
#else
  auto start = std::chrono::steady_clock::now();
  std::string buffer;
  char chunk[4096];

  while (true)
  {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    if (elapsed > timeout_ms)
    {
      throw std::runtime_error("MCP client: response timeout after " +
                               std::to_string(timeout_ms) + "ms");
    }

    ssize_t n = read(stdout_fd_, chunk, sizeof(chunk) - 1);
    if (n > 0)
    {
      buffer.append(chunk, static_cast<size_t>(n));

      // Try to parse complete JSON lines
      size_t pos = 0;
      while (pos < buffer.size())
      {
        size_t nl = buffer.find('\n', pos);
        if (nl == std::string::npos)
        {
          break;
        }
        std::string line = buffer.substr(pos, nl - pos);
        pos = nl + 1;

        if (line.empty())
        {
          continue;
        }

        try
        {
          auto json = nlohmann::json::parse(line);

          // Skip notifications (no id)
          if (!json.contains("id"))
          {
            continue;
          }

          if (json["id"].get<int>() == id)
          {
            buffer = buffer.substr(pos);

            if (json.contains("error"))
            {
              std::string err_msg = json["error"].value("message", "Unknown MCP error");
              throw std::runtime_error("MCP error: " + err_msg);
            }
            return json.value("result", nlohmann::json::object());
          }
        }
        catch (const nlohmann::json::parse_error&)
        {
          // Not valid JSON, skip this line
          continue;
        }
      }
      buffer = buffer.substr(pos);
    }
    else if (n == 0)
    {
      throw std::runtime_error("MCP client: server closed connection");
    }
    else if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    else
    {
      throw std::runtime_error("MCP client: read error");
    }
  }
#endif
}

// v0.6.9 D6: Compute SHA-256 of a file
std::string McpClient::compute_sha256_file(const std::string& path)
{
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return "";

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return "";

  EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);

  char buf[8192];
  while (file.read(buf, sizeof(buf)) || file.gcount() > 0)
  {
    EVP_DigestUpdate(ctx, buf, static_cast<size_t>(file.gcount()));
  }

  unsigned char hash[EVP_MAX_MD_SIZE];
  unsigned int hash_len = 0;
  EVP_DigestFinal_ex(ctx, hash, &hash_len);
  EVP_MD_CTX_free(ctx);

  std::ostringstream hex;
  hex << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < hash_len; ++i)
  {
    hex << std::setw(2) << static_cast<int>(hash[i]);
  }
  return hex.str();
}

// v0.6.9 D6: Verify binary hash before launch
void McpClient::verify_hash()
{
  if (expected_hash_.empty()) return;

  // Resolve command to full path
  std::string cmd_path = command_;

  // If not an absolute path, search PATH
  if (!command_.empty() && command_[0] != '/')
  {
    const char* path_env = std::getenv("PATH");
    if (path_env)
    {
      std::istringstream paths(path_env);
      std::string dir;
      while (std::getline(paths, dir, ':'))
      {
        std::string candidate = dir + "/" + command_;
        std::ifstream test(candidate);
        if (test.good())
        {
          cmd_path = candidate;
          break;
        }
      }
    }
  }

  auto actual = compute_sha256_file(cmd_path);
  if (actual.empty())
  {
    throw std::runtime_error("MCP D6: cannot compute hash for '" + cmd_path + "'");
  }
  if (actual != expected_hash_)
  {
    security::AuditLogger::instance().log({
        {},
        security::TraceContext::current(),
        security::EventType::MCPRequest,
        "",
        command_,
        "MCP server hash mismatch — possible supply chain attack",
        {{"expected", expected_hash_}, {"actual", actual}, {"path", cmd_path}}});
    throw std::runtime_error(
        "MCP D6: hash mismatch for '" + command_ +
        "' (expected " + expected_hash_.substr(0, 16) + "..., got " +
        actual.substr(0, 16) + "...)");
  }
}

bool McpClient::initialize()
{
  if (initialized_)
  {
    return true;
  }

  // v0.6.9 D6: Verify hash before starting process
  verify_hash();

  auto init_start = std::chrono::steady_clock::now();

  start_process();

  // v0.6.9 D6: Audit MCP server start
  security::AuditLogger::instance().log({
      {},
      security::TraceContext::current(),
      security::EventType::MCPRequest,
      "",
      command_,
      "MCP server starting",
      {{"args_count", static_cast<int>(args_.size())},
       {"env_count", static_cast<int>(env_.size())},
       {"max_memory_mb", static_cast<int>(max_memory_bytes_ / 1048576)}}});

  nlohmann::json params = {
      {"protocolVersion", "2024-11-05"},
      {"capabilities", nlohmann::json::object()},
      {"clientInfo", {{"name", "neam"}, {"version", "0.6.9"}}}};

  auto result = send_request("initialize", params);

  // v0.6.9 D6: Startup timeout check
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - init_start)
                     .count();
  if (elapsed > startup_timeout_ms_)
  {
    kill_process();
    throw std::runtime_error("MCP D6: server startup timeout after " +
                             std::to_string(elapsed) + "ms");
  }

  // Send initialized notification (no response expected)
  nlohmann::json notification = {
      {"jsonrpc", "2.0"},
      {"method", "notifications/initialized"}};
  std::string msg = notification.dump() + "\n";
#ifndef _WIN32
  write(stdin_fd_, msg.data(), msg.size());
#endif

  initialized_ = true;

  // Audit successful initialization
  security::AuditLogger::instance().log({
      {},
      security::TraceContext::current(),
      security::EventType::MCPResponse,
      "",
      command_,
      "MCP server initialized",
      {{"elapsed_ms", static_cast<int>(elapsed)}}});

  return true;
}

std::vector<McpToolInfo> McpClient::list_tools()
{
  if (!initialized_)
  {
    throw std::runtime_error("MCP client: not initialized");
  }

  auto result = send_request("tools/list", nlohmann::json::object());

  std::vector<McpToolInfo> tools;
  if (result.contains("tools") && result["tools"].is_array())
  {
    for (const auto& tool : result["tools"])
    {
      McpToolInfo info;
      info.name = tool.value("name", "");
      info.description = tool.value("description", "");
      info.input_schema = tool.value("inputSchema", nlohmann::json::object());
      tools.push_back(std::move(info));
    }
  }
  return tools;
}

nlohmann::json McpClient::call_tool(const std::string& name, const nlohmann::json& args)
{
  if (!initialized_)
  {
    throw std::runtime_error("MCP client: not initialized");
  }

  // v0.6.9 D6: Audit MCP tool call
  auto& audit = security::AuditLogger::instance();
  nlohmann::json req_meta = {{"server", command_}};
  if (args.is_object())
  {
    std::vector<std::string> keys;
    for (auto& [k, v] : args.items()) keys.push_back(k);
    req_meta["args_keys"] = keys;
  }
  audit.log({
      {},
      security::TraceContext::current(),
      security::EventType::MCPRequest,
      "",
      name,
      "MCP tool call",
      req_meta});

  auto call_start = std::chrono::steady_clock::now();
  nlohmann::json params = {{"name", name}, {"arguments", args}};
  auto result = send_request("tools/call", params);

  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - call_start)
                     .count();

  audit.log({
      {},
      security::TraceContext::current(),
      security::EventType::MCPResponse,
      "",
      name,
      "MCP tool response",
      {{"server", command_}, {"elapsed_ms", static_cast<int>(elapsed)}}});

  // MCP tools/call returns {content: [{type: "text", text: "..."}]}
  if (result.contains("content") && result["content"].is_array() &&
      !result["content"].empty())
  {
    const auto& block = result["content"][0];
    if (block.contains("text"))
    {
      return block["text"];
    }
  }

  return result;
}

void McpClient::shutdown()
{
  if (!initialized_)
  {
    return;
  }

  try
  {
    // Best-effort shutdown
    nlohmann::json notification = {
        {"jsonrpc", "2.0"},
        {"method", "notifications/cancelled"},
        {"params", {{"reason", "shutdown"}}}};
    std::string msg = notification.dump() + "\n";
#ifndef _WIN32
    if (stdin_fd_ >= 0)
    {
      write(stdin_fd_, msg.data(), msg.size());
    }
#endif
  }
  catch (...)
  {
    // Ignore errors during shutdown
  }

  kill_process();
  initialized_ = false;
}

}  // namespace neamc::vm
