//
// Neam Virtual Machine - External skill dispatch (v0.6.8)
//
// Dispatches external skill calls to MCP servers, HTTP APIs,
// or Claude built-in tool handlers.
//

#include "neamc/vm/external_skill.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>

#include "neamc/security/network_policy.hpp"

#ifndef _WIN32
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include "neamc/llm/http_client.hpp"
#include "neamc/vm/mcp_client.hpp"
#include "neamc/vm/object.hpp"

namespace neamc::vm
{
namespace
{
// v0.6.9 D8: Template context determines how values are sanitized
enum class TemplateContext { Plain, URL, Header, JSON };

// Escape a string for JSON embedding (backslash-escapes special chars)
std::string json_escape(const std::string& s)
{
  std::string out;
  out.reserve(s.size());
  for (char c : s)
  {
    switch (c)
    {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n";  break;
      case '\r': out += "\\r";  break;
      case '\t': out += "\\t";  break;
      default:   out += c;      break;
    }
  }
  return out;
}

// Replace {param} placeholders in a template string with values from JSON input.
// The TemplateContext controls how substituted values are sanitized.
std::string expand_template(const std::string& tmpl, const nlohmann::json& input,
                            TemplateContext context = TemplateContext::Plain)
{
  std::string result = tmpl;
  // Match {param_name} patterns
  static const std::regex placeholder_re(R"(\{([a-zA-Z_][a-zA-Z0-9_]*)\})");
  std::smatch match;
  std::string working = tmpl;
  result.clear();
  while (std::regex_search(working, match, placeholder_re))
  {
    result += match.prefix().str();
    const std::string key = match[1].str();
    if (input.contains(key))
    {
      const auto& val = input.at(key);
      std::string value;
      if (val.is_string())
      {
        value = val.get<std::string>();
      }
      else
      {
        value = val.dump();
      }
      // v0.6.9 D8: Context-aware sanitization
      switch (context)
      {
        case TemplateContext::URL:
          result += security::url_encode_value(value);
          break;
        case TemplateContext::Header:
          result += security::sanitize_header_value(value);
          break;
        case TemplateContext::JSON:
          result += json_escape(value);
          break;
        case TemplateContext::Plain:
        default:
          result += value;
          break;
      }
    }
    else
    {
      result += match[0].str();  // Keep unresolved placeholder
    }
    working = match.suffix().str();
  }
  result += working;
  return result;
}

// Expand ${ENV_VAR} references in a string via std::getenv.
std::string expand_env_vars(const std::string& input)
{
  std::string result = input;
  static const std::regex env_re(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\})");
  std::smatch match;
  std::string working = input;
  result.clear();
  while (std::regex_search(working, match, env_re))
  {
    result += match.prefix().str();
    const char* env_val = std::getenv(match[1].str().c_str());
    if (env_val)
    {
      result += env_val;
    }
    working = match.suffix().str();
  }
  result += working;
  return result;
}

// Extract a value from JSON using a JSON pointer path (e.g., "/forecast/summary").
std::string extract_json_path(const nlohmann::json& data, const std::string& path)
{
  if (path.empty())
  {
    return data.dump();
  }
  try
  {
    const auto& value = data.at(nlohmann::json::json_pointer(path));
    if (value.is_string())
    {
      return value.get<std::string>();
    }
    return value.dump();
  }
  catch (const std::exception&)
  {
    return data.dump();
  }
}

std::string dispatch_http_skill(ObjSkill* skill, const nlohmann::json& input)
{
  const auto& cfg = *skill->external;

  // Expand URL template with parameter values (URL-encoded for SSRF prevention)
  std::string url = expand_template(cfg.http_url_template, input, TemplateContext::URL);

  // Build body if present
  std::string body;
  if (!cfg.http_body_template.empty())
  {
    body = expand_template(cfg.http_body_template, input, TemplateContext::JSON);
  }
  else if (cfg.http_method == "POST" || cfg.http_method == "PUT" ||
           cfg.http_method == "PATCH")
  {
    body = input.dump();
  }

  // Build headers with env var expansion
  std::vector<std::string> headers;
  for (const auto& [key, value] : cfg.http_headers)
  {
    std::string hdr_val = security::sanitize_header_value(expand_env_vars(value));
    if (!hdr_val.empty())
    {
      headers.push_back(key + ": " + hdr_val);
    }
  }
  // Default Content-Type if not set
  bool has_content_type = false;
  for (const auto& h : headers)
  {
    if (h.substr(0, 12) == "Content-Type" || h.substr(0, 12) == "content-type")
    {
      has_content_type = true;
      break;
    }
  }
  if (!has_content_type && !body.empty())
  {
    headers.push_back("Content-Type: application/json");
  }

  // Execute HTTP request
  auto result = llm::http_request(cfg.http_method, url, body, headers, cfg.http_timeout_ms);

  if (result.status >= 400)
  {
    throw std::runtime_error("HTTP " + std::to_string(result.status) + ": " + result.body);
  }

  // Extract response via JSON pointer if configured
  if (!cfg.http_response_path.empty())
  {
    try
    {
      auto parsed = nlohmann::json::parse(result.body);
      return extract_json_path(parsed, cfg.http_response_path);
    }
    catch (const nlohmann::json::parse_error&)
    {
      return result.body;
    }
  }

  return result.body;
}

std::string dispatch_mcp_skill(ObjSkill* skill, const nlohmann::json& input,
                               McpClient* mcp_client)
{
  const auto& cfg = *skill->external;

  if (!mcp_client)
  {
    throw std::runtime_error(
        "MCP tool dispatch: no active connection for server '" +
        cfg.mcp_server_name + "'. Ensure the mcp_server block is defined.");
  }

  auto result = mcp_client->call_tool(cfg.mcp_tool_name, input);
  if (result.is_string())
  {
    return result.get<std::string>();
  }
  return result.dump();
}

// v0.6.8: Safe command execution with shell metacharacter rejection,
// timeout (default 30s), and output size limit (default 1MB).
// Uses posix_spawn/fork+exec on Unix, falls back to constrained popen on Windows.
constexpr long kDefaultExecTimeoutMs = 30000;
constexpr size_t kDefaultMaxOutput = 1048576;  // 1 MB

bool contains_shell_metachar(const std::string& cmd)
{
  for (char c : cmd)
  {
    switch (c)
    {
      case ';':
      case '|':
      case '&':
      case '$':
      case '`':
      case '>':
      case '<':
      case '(':
      case ')':
        return true;
      default:
        break;
    }
  }
  return false;
}

std::string safe_exec(const std::string& command, long timeout_ms = kDefaultExecTimeoutMs,
                      size_t max_output = kDefaultMaxOutput)
{
  if (command.empty())
  {
    throw std::runtime_error("safe_exec: empty command");
  }

  // Reject shell metacharacters to prevent injection
  if (contains_shell_metachar(command))
  {
    throw std::runtime_error(
        "safe_exec: command contains shell metacharacters (;|&$`><()) which are "
        "not allowed for security. Use individual commands instead.");
  }

#ifdef _WIN32
  // Windows fallback: use _popen with metachar check already done above
  FILE* pipe = _popen(command.c_str(), "r");
  if (!pipe)
  {
    throw std::runtime_error("safe_exec: failed to execute command");
  }
  std::string output;
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe))
  {
    output += buffer;
    if (output.size() > max_output)
    {
      _pclose(pipe);
      output.resize(max_output);
      output += "\n[output truncated at " + std::to_string(max_output) + " bytes]";
      return output;
    }
  }
  _pclose(pipe);
  return output;
#else
  // Unix: fork + exec with stdout/stderr capture via pipe
  int pipefd[2];
  if (pipe(pipefd) != 0)
  {
    throw std::runtime_error("safe_exec: pipe() failed");
  }

  pid_t pid = fork();
  if (pid < 0)
  {
    close(pipefd[0]);
    close(pipefd[1]);
    throw std::runtime_error("safe_exec: fork() failed");
  }

  if (pid == 0)
  {
    // Child process
    close(pipefd[0]);  // Close read end
    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);
    close(pipefd[1]);

    // Use execv with /bin/sh -c for the command
    const char* argv[] = {"/bin/sh", "-c", command.c_str(), nullptr};
    execv("/bin/sh", const_cast<char* const*>(argv));
    _exit(127);  // exec failed
  }

  // Parent process
  close(pipefd[1]);  // Close write end

  // Set read end to non-blocking for timeout handling
  int flags = fcntl(pipefd[0], F_GETFL, 0);
  fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);

  std::string output;
  char buffer[4096];
  auto start = std::chrono::steady_clock::now();
  bool timed_out = false;

  while (true)
  {
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    if (elapsed > timeout_ms)
    {
      timed_out = true;
      break;
    }

    ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
    if (n > 0)
    {
      output.append(buffer, static_cast<size_t>(n));
      if (output.size() > max_output)
      {
        output.resize(max_output);
        output += "\n[output truncated at " + std::to_string(max_output) + " bytes]";
        break;
      }
    }
    else if (n == 0)
    {
      break;  // EOF — child closed its end
    }
    else if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      // No data yet — brief sleep then retry
      usleep(10000);  // 10ms
    }
    else
    {
      break;  // Read error
    }
  }

  close(pipefd[0]);

  if (timed_out)
  {
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
    throw std::runtime_error("safe_exec: command timed out after " +
                             std::to_string(timeout_ms) + "ms");
  }

  int status = 0;
  waitpid(pid, &status, 0);

  if (WIFEXITED(status) && WEXITSTATUS(status) != 0 && output.empty())
  {
    output = "Command exited with status " + std::to_string(WEXITSTATUS(status));
  }

  return output;
#endif
}

// Execute a Claude built-in tool locally.
// When the provider is Anthropic/Bedrock, these tools are dispatched server-side.
// For other providers (OpenAI, Ollama), we execute them locally so that
// extern skill declarations are portable across all LLM backends.
std::string dispatch_claude_builtin(ObjSkill* skill, const nlohmann::json& input)
{
  const auto& tool_type = skill->external->claude_tool_type;

  if (tool_type == "bash_20241022")
  {
    std::string command;
    if (input.contains("command") && input["command"].is_string())
    {
      command = input["command"].get<std::string>();
    }
    else
    {
      throw std::runtime_error("bash_20241022: 'command' parameter required");
    }

    // v0.6.8: Use safe_exec instead of raw popen
    return safe_exec(command, kDefaultExecTimeoutMs, kDefaultMaxOutput);
  }

  if (tool_type == "text_editor_20241022")
  {
    // Basic text editor: supports "view" and "create" commands
    std::string cmd;
    std::string path;
    if (input.contains("command") && input["command"].is_string())
      cmd = input["command"].get<std::string>();
    if (input.contains("path") && input["path"].is_string())
      path = input["path"].get<std::string>();

    if (cmd == "view" && !path.empty())
    {
      std::ifstream file(path);
      if (!file.is_open())
        throw std::runtime_error("text_editor: cannot open " + path);
      std::stringstream buf;
      buf << file.rdbuf();
      return buf.str();
    }
    else if (cmd == "create" && !path.empty())
    {
      std::string content;
      if (input.contains("file_text") && input["file_text"].is_string())
        content = input["file_text"].get<std::string>();
      std::ofstream file(path);
      if (!file.is_open())
        throw std::runtime_error("text_editor: cannot write " + path);
      file << content;
      return "File created: " + path;
    }

    throw std::runtime_error("text_editor_20241022: unsupported command '" + cmd + "'");
  }

  throw std::runtime_error("Unsupported Claude built-in tool type: " + tool_type);
}
}  // namespace

std::string dispatch_external_skill(ObjSkill* skill, const nlohmann::json& input,
                                    McpClient* mcp_client)
{
  if (!skill || !skill->external)
  {
    throw std::runtime_error("dispatch_external_skill called on non-external skill");
  }

  switch (skill->external->binding)
  {
    case SkillBinding::McpTool:
      return dispatch_mcp_skill(skill, input, mcp_client);
    case SkillBinding::HttpApi:
      return dispatch_http_skill(skill, input);
    case SkillBinding::ClaudeBuiltin:
      return dispatch_claude_builtin(skill, input);
    default:
      throw std::runtime_error("Unknown external skill binding type");
  }
}
}  // namespace neamc::vm
