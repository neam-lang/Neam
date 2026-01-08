//
// Neam DAP - Debug Adapter Protocol implementation
//

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"

namespace
{
constexpr const char* kContentLengthHeader = "Content-Length:";

class SocketReader
{
public:
  explicit SocketReader(int fd) : fd_(fd) {}

  bool read_line(std::string& line)
  {
    while (true)
    {
      const auto newline = buffer_.find('\n');
      if (newline != std::string::npos)
      {
        line = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        if (!line.empty() && line.back() == '\r')
        {
          line.pop_back();
        }
        return true;
      }
      if (!read_more())
      {
        return false;
      }
    }
  }

  bool read_bytes(std::size_t count, std::string& out)
  {
    while (buffer_.size() < count)
    {
      if (!read_more())
      {
        return false;
      }
    }
    out.assign(buffer_.data(), count);
    buffer_.erase(0, count);
    return true;
  }

private:
  bool read_more()
  {
    char chunk[4096];
    const auto received = ::recv(fd_, chunk, sizeof(chunk), 0);
    if (received <= 0)
    {
      return false;
    }
    buffer_.append(chunk, static_cast<std::size_t>(received));
    return true;
  }

  int fd_;
  std::string buffer_;
};

void send_message(int fd, const std::string& payload)
{
  std::ostringstream header;
  header << "Content-Length: " << payload.size() << "\r\n\r\n";
  const std::string data = header.str() + payload;
  ::send(fd, data.data(), data.size(), 0);
}

bool read_message(SocketReader& reader, std::string& payload)
{
  payload.clear();
  std::size_t content_length = 0;
  std::string line;
  while (reader.read_line(line))
  {
    if (line.empty())
    {
      break;
    }
    if (line.rfind(kContentLengthHeader, 0) == 0)
    {
      const std::string value = line.substr(std::strlen(kContentLengthHeader));
      content_length = static_cast<std::size_t>(std::stoul(value));
    }
  }
  if (content_length == 0)
  {
    return false;
  }
  return reader.read_bytes(content_length, payload);
}

std::optional<std::string> read_file(const std::string& path)
{
  std::ifstream in(path);
  if (!in)
  {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  return buffer.str();
}
}  // namespace

class DapServer
{
public:
  explicit DapServer(int port) : port_(port) {}

  void run()
  {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0)
    {
      throw std::runtime_error("Failed to create socket");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<uint16_t>(port_));
    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
      throw std::runtime_error("Failed to bind socket");
    }
    if (::listen(server_fd_, 1) < 0)
    {
      throw std::runtime_error("Failed to listen");
    }

    client_fd_ = ::accept(server_fd_, nullptr, nullptr);
    if (client_fd_ < 0)
    {
      throw std::runtime_error("Failed to accept client");
    }

    SocketReader reader(client_fd_);
    std::string payload;
    while (read_message(reader, payload))
    {
      handle_message(payload);
    }
    resume_execution();
    if (vm_thread_.joinable())
    {
      vm_thread_.join();
    }
  }

private:
  void handle_message(const std::string& payload)
  {
    auto message = nlohmann::json::parse(payload, nullptr, false);
    if (message.is_discarded())
    {
      return;
    }
    const std::string command = message.value("command", "");
    const auto id = message.value("seq", 0);

    if (command == "initialize")
    {
      nlohmann::json result = {{"supportsConfigurationDoneRequest", true},
                               {"supportsEvaluateForHovers", true}};
      send_response(id, command, result);
      send_event("initialized", nlohmann::json::object());
      return;
    }
    if (command == "launch")
    {
      launch_args_ = message.value("arguments", nlohmann::json::object());
      send_response(id, command, nlohmann::json::object());
      return;
    }
    if (command == "configurationDone")
    {
      send_response(id, command, nlohmann::json::object());
      start_vm();
      return;
    }
    if (command == "setBreakpoints")
    {
      send_response(id, command, {{"breakpoints", nlohmann::json::array()}});
      return;
    }
    if (command == "threads")
    {
      send_response(id, command, {{"threads", {{{"id", 1}, {"name", "main"}}}}});
      return;
    }
    if (command == "stackTrace")
    {
      nlohmann::json frames = nlohmann::json::array();
      if (last_source_.has_value())
      {
        frames.push_back({{"id", 1},
                          {"name", "neam"},
                          {"line", static_cast<int>(last_source_->line + 1)},
                          {"column", 1},
                          {"source", {{"path", last_source_path_}}}});
      }
      send_response(id, command, {{"stackFrames", frames}, {"totalFrames", frames.size()}});
      return;
    }
    if (command == "scopes")
    {
      send_response(id, command, {{"scopes", {{{"name", "runtime"}, {"variablesReference", 1}}}}});
      return;
    }
    if (command == "variables")
    {
      nlohmann::json variables = nlohmann::json::array();
      if (!last_payload_.empty())
      {
        variables.push_back({{"name", "conversation"},
                             {"value", last_payload_},
                             {"variablesReference", 0}});
      }
      send_response(id, command, {{"variables", variables}});
      return;
    }
    if (command == "continue")
    {
      send_response(id, command, {{"allThreadsContinued", true}});
      resume_execution();
      return;
    }
    if (command == "disconnect")
    {
      send_response(id, command, nlohmann::json::object());
      resume_execution();
      return;
    }
  }

  void start_vm()
  {
    if (running_)
    {
      return;
    }
    running_ = true;
    const auto program = launch_args_.value("program", "");
    if (program.empty())
    {
      send_event("output", {{"category", "stderr"}, {"output", "No program provided.\n"}});
      return;
    }
    auto source = read_file(program);
    if (!source.has_value())
    {
      send_event("output", {{"category", "stderr"}, {"output", "Failed to open program.\n"}});
      return;
    }
    last_source_path_ = program;
    neamc::Pipeline pipeline;
    auto unit = pipeline.compile(*source, {});
    vm_chunk_ = std::move(unit.chunk);

    vm_thread_ = std::thread([this]()
                             {
                               neamc::vm::VirtualMachine vm;
                               vm.set_debug_hook([this](const neamc::vm::VirtualMachine::DebugEvent& event)
                                                 { on_debug_event(event); });
                               try
                               {
                                 vm.run(vm_chunk_);
                                 send_event("terminated", nlohmann::json::object());
                               }
                               catch (const std::exception& ex)
                               {
                                 send_event("output", {{"category", "stderr"},
                                                       {"output", std::string("Runtime error: ") + ex.what() + "\n"}});
                               } });
  }

  void on_debug_event(const neamc::vm::VirtualMachine::DebugEvent& event)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    last_payload_ = event.payload;
    last_source_ = resolve_source_line(event.ip);
    send_event("stopped", {{"reason", "step"},
                           {"description", event.label},
                           {"threadId", 1},
                           {"text", event.payload}});
    paused_ = true;
    cv_.wait(lock, [this]() { return resume_requested_; });
    resume_requested_ = false;
    paused_ = false;
  }

  struct SourceLocation
  {
    std::size_t line = 0;
  };

  std::optional<SourceLocation> resolve_source_line(std::size_t ip)
  {
    const auto& map = vm_chunk_.source_map();
    if (map.empty())
    {
      return std::nullopt;
    }
    std::size_t line = map.front().line;
    for (const auto& entry : map)
    {
      if (entry.offset > ip)
      {
        break;
      }
      line = entry.line;
    }
    return SourceLocation{line};
  }

  void resume_execution()
  {
    std::lock_guard<std::mutex> lock(mutex_);
    resume_requested_ = true;
    cv_.notify_all();
  }

  void send_response(int request_seq, const std::string& command, const nlohmann::json& result)
  {
    nlohmann::json response = {{"type", "response"},
                               {"seq", next_seq_++},
                               {"request_seq", request_seq},
                               {"success", true},
                               {"command", command},
                               {"body", result}};
    send_message(client_fd_, response.dump());
  }

  void send_event(const std::string& name, const nlohmann::json& body)
  {
    nlohmann::json event = {{"type", "event"}, {"seq", next_seq_++}, {"event", name}, {"body", body}};
    send_message(client_fd_, event.dump());
  }

  int port_;
  int server_fd_ = -1;
  int client_fd_ = -1;
  std::atomic<int> next_seq_{1};
  std::mutex mutex_;
  std::condition_variable cv_;
  bool running_ = false;
  bool paused_ = false;
  bool resume_requested_ = false;
  nlohmann::json launch_args_{};
  neamc::vm::Chunk vm_chunk_{};
  std::thread vm_thread_{};
  std::optional<SourceLocation> last_source_{};
  std::string last_source_path_{};
  std::string last_payload_{};
};

int main(int argc, char** argv)
{
  int port = 4711;
  for (int i = 1; i < argc; ++i)
  {
    const std::string arg = argv[i];
    if (arg == "--port" && i + 1 < argc)
    {
      port = std::stoi(argv[++i]);
    }
  }
  try
  {
    DapServer server(port);
    server.run();
  }
  catch (const std::exception& ex)
  {
    std::cerr << "neam-dap error: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
