// SPDX-License-Identifier: Apache-2.0
//
// Neam MCP - stdio transport (spawns child process)
//

#include "neamc/mcp/transport.hpp"

#include <stdexcept>

#ifndef _WIN32
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#include <windows.h>
#endif

namespace neamc::mcp
{
#ifndef _WIN32

class StdioTransport final : public Transport
{
public:
  StdioTransport(const std::string& command,
                 const std::vector<std::string>& args,
                 const std::unordered_map<std::string, std::string>& env)
  {
    int stdin_pipe[2];
    int stdout_pipe[2];
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0)
    {
      throw std::runtime_error("Failed to create pipes for MCP transport");
    }

    pid_ = fork();
    if (pid_ < 0)
    {
      throw std::runtime_error("Failed to fork for MCP transport");
    }

    if (pid_ == 0)
    {
      // Child process
      ::close(stdin_pipe[1]);
      ::close(stdout_pipe[0]);
      dup2(stdin_pipe[0], STDIN_FILENO);
      dup2(stdout_pipe[1], STDOUT_FILENO);
      ::close(stdin_pipe[0]);
      ::close(stdout_pipe[1]);

      // Set environment variables
      for (const auto& [key, value] : env)
      {
        setenv(key.c_str(), value.c_str(), 1);
      }

      // Build argv
      std::vector<const char*> argv;
      argv.push_back(command.c_str());
      for (const auto& arg : args)
      {
        argv.push_back(arg.c_str());
      }
      argv.push_back(nullptr);

      execvp(command.c_str(), const_cast<char* const*>(argv.data()));
      _exit(127);
    }

    // Parent process
    ::close(stdin_pipe[0]);
    ::close(stdout_pipe[1]);
    write_fd_ = stdin_pipe[1];
    read_fd_ = stdout_pipe[0];
    open_ = true;
  }

  ~StdioTransport() override
  {
    close();
  }

  void send(const std::string& data) override
  {
    if (!open_)
    {
      throw std::runtime_error("Transport is closed");
    }
    std::string msg = data + "\n";
    ssize_t total = 0;
    while (total < static_cast<ssize_t>(msg.size()))
    {
      ssize_t written = write(write_fd_, msg.data() + total,
                              msg.size() - static_cast<std::size_t>(total));
      if (written <= 0)
      {
        throw std::runtime_error("Failed to write to MCP process");
      }
      total += written;
    }
  }

  std::string receive() override
  {
    if (!open_)
    {
      throw std::runtime_error("Transport is closed");
    }
    std::string line;
    char ch;
    while (true)
    {
      ssize_t n = read(read_fd_, &ch, 1);
      if (n <= 0)
      {
        if (line.empty())
        {
          throw std::runtime_error("MCP process closed");
        }
        break;
      }
      if (ch == '\n')
      {
        break;
      }
      line += ch;
    }
    // Strip trailing \r
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    return line;
  }

  void close() override
  {
    if (!open_)
    {
      return;
    }
    open_ = false;
    if (write_fd_ >= 0)
    {
      ::close(write_fd_);
      write_fd_ = -1;
    }
    if (read_fd_ >= 0)
    {
      ::close(read_fd_);
      read_fd_ = -1;
    }
    if (pid_ > 0)
    {
      kill(pid_, SIGTERM);
      int status = 0;
      waitpid(pid_, &status, 0);
      pid_ = -1;
    }
  }

  bool is_open() const override { return open_; }

private:
  pid_t pid_{-1};
  int write_fd_{-1};
  int read_fd_{-1};
  bool open_{false};
};

#else  // _WIN32

class StdioTransport final : public Transport
{
public:
  StdioTransport(const std::string& command,
                 const std::vector<std::string>& args,
                 const std::unordered_map<std::string, std::string>& env)
  {
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = NULL;

    if (!CreatePipe(&child_stdin_read_, &child_stdin_write_, &sa, 0) ||
        !CreatePipe(&child_stdout_read_, &child_stdout_write_, &sa, 0))
    {
      throw std::runtime_error("Failed to create pipes for MCP transport");
    }

    SetHandleInformation(child_stdin_write_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(child_stdout_read_, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si = {};
    si.cb = sizeof(STARTUPINFOA);
    si.hStdInput = child_stdin_read_;
    si.hStdOutput = child_stdout_write_;
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES;

    std::string cmdline = command;
    for (const auto& arg : args)
    {
      cmdline += " " + arg;
    }

    // Set environment
    std::string env_block;
    for (const auto& [key, value] : env)
    {
      SetEnvironmentVariableA(key.c_str(), value.c_str());
    }

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(NULL, const_cast<char*>(cmdline.c_str()), NULL, NULL,
                        TRUE, 0, NULL, NULL, &si, &pi))
    {
      throw std::runtime_error("Failed to create MCP process");
    }

    process_ = pi.hProcess;
    CloseHandle(pi.hThread);
    CloseHandle(child_stdin_read_);
    CloseHandle(child_stdout_write_);
    child_stdin_read_ = INVALID_HANDLE_VALUE;
    child_stdout_write_ = INVALID_HANDLE_VALUE;
    open_ = true;
  }

  ~StdioTransport() override { close(); }

  void send(const std::string& data) override
  {
    if (!open_)
    {
      throw std::runtime_error("Transport is closed");
    }
    std::string msg = data + "\n";
    DWORD written = 0;
    if (!WriteFile(child_stdin_write_, msg.data(),
                   static_cast<DWORD>(msg.size()), &written, NULL))
    {
      throw std::runtime_error("Failed to write to MCP process");
    }
  }

  std::string receive() override
  {
    if (!open_)
    {
      throw std::runtime_error("Transport is closed");
    }
    std::string line;
    char ch;
    DWORD read_count = 0;
    while (ReadFile(child_stdout_read_, &ch, 1, &read_count, NULL) && read_count > 0)
    {
      if (ch == '\n')
      {
        break;
      }
      line += ch;
    }
    if (!line.empty() && line.back() == '\r')
    {
      line.pop_back();
    }
    return line;
  }

  void close() override
  {
    if (!open_)
    {
      return;
    }
    open_ = false;
    if (child_stdin_write_ != INVALID_HANDLE_VALUE)
    {
      CloseHandle(child_stdin_write_);
      child_stdin_write_ = INVALID_HANDLE_VALUE;
    }
    if (child_stdout_read_ != INVALID_HANDLE_VALUE)
    {
      CloseHandle(child_stdout_read_);
      child_stdout_read_ = INVALID_HANDLE_VALUE;
    }
    if (process_ != INVALID_HANDLE_VALUE)
    {
      TerminateProcess(process_, 0);
      WaitForSingleObject(process_, 5000);
      CloseHandle(process_);
      process_ = INVALID_HANDLE_VALUE;
    }
  }

  bool is_open() const override { return open_; }

private:
  HANDLE child_stdin_read_{INVALID_HANDLE_VALUE};
  HANDLE child_stdin_write_{INVALID_HANDLE_VALUE};
  HANDLE child_stdout_read_{INVALID_HANDLE_VALUE};
  HANDLE child_stdout_write_{INVALID_HANDLE_VALUE};
  HANDLE process_{INVALID_HANDLE_VALUE};
  bool open_{false};
};

#endif

std::unique_ptr<Transport> create_stdio_transport(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::unordered_map<std::string, std::string>& env)
{
  return std::make_unique<StdioTransport>(command, args, env);
}
}  // namespace neamc::mcp
