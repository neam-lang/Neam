//
// Neam Virtual Machine - MCP Client (v0.6.8)
//
// JSON-RPC 2.0 over stdio transport for Model Context Protocol.
//

#include "neamc/vm/mcp_client.hpp"

#include <chrono>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace neamc::vm
{

McpClient::McpClient(const std::string& command, const std::vector<std::string>& args,
                     const std::unordered_map<std::string, std::string>& env)
    : command_(command), args_(args), env_(env)
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

    // Set environment variables
    for (const auto& [key, value] : env_)
    {
      setenv(key.c_str(), value.c_str(), 1);
    }

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

bool McpClient::initialize()
{
  if (initialized_)
  {
    return true;
  }

  start_process();

  nlohmann::json params = {
      {"protocolVersion", "2024-11-05"},
      {"capabilities", nlohmann::json::object()},
      {"clientInfo", {{"name", "neam"}, {"version", "0.6.8"}}}};

  auto result = send_request("initialize", params);

  // Send initialized notification (no response expected)
  nlohmann::json notification = {
      {"jsonrpc", "2.0"},
      {"method", "notifications/initialized"}};
  std::string msg = notification.dump() + "\n";
#ifndef _WIN32
  write(stdin_fd_, msg.data(), msg.size());
#endif

  initialized_ = true;
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

  nlohmann::json params = {{"name", name}, {"arguments", args}};
  auto result = send_request("tools/call", params);

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
