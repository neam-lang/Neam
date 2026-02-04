// SPDX-License-Identifier: Apache-2.0
//
// Neam v0.6.0 - Thread Pool HTTP Server
//
// Provides a fixed thread pool with connection queue, backpressure,
// graceful shutdown, request ID generation, and JSON access logging.
// Designed as a drop-in enhancement that will be integrated into
// http_server.cpp in a later phase.
//

#include "neamc/api/http_server.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

#ifdef _WIN32
#define NEAM_CLOSESOCKET(s) ::closesocket(s)
#else
#define NEAM_CLOSESOCKET(s) ::close(s)
#endif

namespace neamc::api
{

// ---------- UUID-v4 generation ----------

namespace
{

std::string generate_uuid_v4()
{
  static thread_local std::mt19937 rng{std::random_device{}()};
  static thread_local std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFF);

  uint32_t data[4] = {dist(rng), dist(rng), dist(rng), dist(rng)};

  // Set version 4 bits
  data[1] = (data[1] & 0xFFFF0FFF) | 0x00004000;
  // Set variant bits
  data[2] = (data[2] & 0x3FFFFFFF) | 0x80000000;

  auto hex_byte = [](uint8_t b) -> std::string {
    const char hex[] = "0123456789abcdef";
    return {hex[b >> 4], hex[b & 0x0F]};
  };

  auto hex_u32 = [&](uint32_t v) -> std::string {
    return hex_byte(static_cast<uint8_t>((v >> 24) & 0xFF)) +
           hex_byte(static_cast<uint8_t>((v >> 16) & 0xFF)) +
           hex_byte(static_cast<uint8_t>((v >> 8) & 0xFF)) +
           hex_byte(static_cast<uint8_t>(v & 0xFF));
  };

  std::string a = hex_u32(data[0]);
  std::string b = hex_u32(data[1]);
  std::string c = hex_u32(data[2]);
  std::string d = hex_u32(data[3]);

  // Format: 8-4-4-4-12
  return a + "-" + b.substr(0, 4) + "-" + b.substr(4, 4) + "-" +
         c.substr(0, 4) + "-" + c.substr(4, 4) + d;
}

// Escape a string for JSON output
std::string json_escape(const std::string& s)
{
  std::string result;
  result.reserve(s.size() + 8);
  for (char c : s)
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

// Get current time as ISO 8601 string
std::string iso8601_now()
{
  auto now = std::chrono::system_clock::now();
  auto time_t_now = std::chrono::system_clock::to_time_t(now);
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()) %
            1000;

  struct tm tm_buf{};
#ifdef _WIN32
  gmtime_s(&tm_buf, &time_t_now);
#else
  gmtime_r(&time_t_now, &tm_buf);
#endif

  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_buf);

  std::ostringstream oss;
  oss << buf << "." << std::setw(3) << std::setfill('0') << ms.count() << "Z";
  return oss.str();
}

}  // namespace

// ---------- Connection Queue ----------

class ConnectionQueue
{
public:
  explicit ConnectionQueue(std::size_t max_size)
      : max_size_(max_size)
  {
  }

  // Returns false if queue is full (backpressure)
  bool push(int client_fd)
  {
    std::unique_lock<std::mutex> lock(mutex_);
    if (queue_.size() >= max_size_)
    {
      return false;
    }
    queue_.push_back(client_fd);
    cv_.notify_one();
    return true;
  }

  // Blocking pop; returns -1 when shutdown is signaled
  int pop()
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this]() { return !queue_.empty() || shutdown_; });
    if (shutdown_ && queue_.empty())
    {
      return -1;
    }
    int fd = queue_.front();
    queue_.pop_front();
    return fd;
  }

  void shutdown()
  {
    std::unique_lock<std::mutex> lock(mutex_);
    shutdown_ = true;
    cv_.notify_all();
  }

  std::size_t size() const
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size();
  }

  bool is_full() const
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.size() >= max_size_;
  }

private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<int> queue_;
  std::size_t max_size_;
  bool shutdown_ = false;
};

// ---------- Access Logger ----------

class AccessLogger
{
public:
  void log(const std::string& request_id,
           const std::string& method,
           const std::string& path,
           int status_code,
           std::size_t response_size,
           double duration_ms,
           const std::string& remote_addr)
  {
    std::ostringstream json;
    json << "{"
         << "\"timestamp\":\"" << iso8601_now() << "\","
         << "\"request_id\":\"" << json_escape(request_id) << "\","
         << "\"method\":\"" << json_escape(method) << "\","
         << "\"path\":\"" << json_escape(path) << "\","
         << "\"status\":" << status_code << ","
         << "\"size\":" << response_size << ","
         << "\"duration_ms\":" << std::fixed << std::setprecision(2) << duration_ms << ","
         << "\"remote_addr\":\"" << json_escape(remote_addr) << "\""
         << "}";

    std::lock_guard<std::mutex> lock(mutex_);
    std::cerr << json.str() << "\n";
  }

private:
  std::mutex mutex_;
};

// ---------- ThreadPoolHttpServer ----------

class ThreadPoolHttpServer
{
public:
  struct Config
  {
    int port = 8080;
    std::string host = "0.0.0.0";
    std::size_t num_workers = 4;
    std::size_t max_queue_size = 1024;
    int drain_timeout_seconds = 30;
    bool cors_enabled = false;
  };

  explicit ThreadPoolHttpServer(const Config& config)
      : config_(config),
        queue_(config.max_queue_size)
  {
  }

  ~ThreadPoolHttpServer()
  {
    stop();
  }

  // Route registration
  void get(const std::string& path, RouteHandler handler)
  {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    routes_["GET"][path] = std::move(handler);
  }

  void post(const std::string& path, RouteHandler handler)
  {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    routes_["POST"][path] = std::move(handler);
  }

  void options(const std::string& path, RouteHandler handler)
  {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    routes_["OPTIONS"][path] = std::move(handler);
  }

  void get_prefix(const std::string& prefix, RouteHandler handler)
  {
    std::lock_guard<std::mutex> lock(routes_mutex_);
    prefix_routes_["GET"].push_back({prefix, std::move(handler)});
  }

  void enable_cors()
  {
    cors_enabled_ = true;
  }

  // Start the server (blocking on acceptor thread)
  void run()
  {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0)
    {
      throw std::runtime_error("ThreadPoolHttpServer: failed to create socket");
    }

    int opt = 1;
    if (::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char*>(&opt), sizeof(opt)) < 0)
    {
      NEAM_CLOSESOCKET(server_fd_);
      throw std::runtime_error("ThreadPoolHttpServer: failed to set socket options");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(config_.port));

    if (config_.host == "0.0.0.0")
    {
      addr.sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
      if (::inet_pton(AF_INET, config_.host.c_str(), &addr.sin_addr) <= 0)
      {
        NEAM_CLOSESOCKET(server_fd_);
        throw std::runtime_error("ThreadPoolHttpServer: invalid host address");
      }
    }

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
      NEAM_CLOSESOCKET(server_fd_);
      throw std::runtime_error("ThreadPoolHttpServer: failed to bind to " +
                               config_.host + ":" + std::to_string(config_.port));
    }

    if (::listen(server_fd_, static_cast<int>(config_.max_queue_size)) < 0)
    {
      NEAM_CLOSESOCKET(server_fd_);
      throw std::runtime_error("ThreadPoolHttpServer: failed to listen");
    }

    running_ = true;

    // Spawn worker threads
    for (std::size_t i = 0; i < config_.num_workers; ++i)
    {
      workers_.emplace_back([this]() { worker_loop(); });
    }

    std::cerr << "{\"event\":\"server_started\",\"host\":\"" << config_.host
              << "\",\"port\":" << config_.port
              << ",\"workers\":" << config_.num_workers
              << ",\"max_queue\":" << config_.max_queue_size << "}\n";

    // Acceptor loop
    while (running_)
    {
      sockaddr_in client_addr{};
      socklen_t client_len = sizeof(client_addr);
      int client_fd = ::accept(server_fd_,
                               reinterpret_cast<sockaddr*>(&client_addr),
                               &client_len);

      if (client_fd < 0)
      {
        if (!running_)
        {
          break;
        }
        continue;
      }

      // Backpressure: respond 503 if queue is full
      if (!queue_.push(client_fd))
      {
        send_503(client_fd);
        NEAM_CLOSESOCKET(client_fd);
        continue;
      }
    }
  }

  // Graceful shutdown with drain timeout
  void stop()
  {
    if (!running_.exchange(false))
    {
      return;
    }

    // Close listening socket to unblock accept()
    if (server_fd_ >= 0)
    {
      NEAM_CLOSESOCKET(server_fd_);
      server_fd_ = -1;
    }

    // Signal the queue to wake all workers
    queue_.shutdown();

    // Wait for workers to finish with drain timeout
    auto deadline = std::chrono::steady_clock::now() +
                    std::chrono::seconds(config_.drain_timeout_seconds);

    for (auto& worker : workers_)
    {
      if (worker.joinable())
      {
        auto remaining = deadline - std::chrono::steady_clock::now();
        if (remaining.count() > 0)
        {
          worker.join();
        }
        else
        {
          worker.detach();
        }
      }
    }
    workers_.clear();

    std::cerr << "{\"event\":\"server_stopped\"}\n";
  }

  bool is_running() const
  {
    return running_;
  }

private:
  // Internal socket reader (same pattern as http_server.cpp)
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
          return !buffer_.empty();
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

  // Parse HTTP request (matches http_server.cpp pattern)
  static bool parse_request(SocketReader& reader, HttpRequest& request)
  {
    std::string line;
    if (!reader.read_line(line) || line.empty())
    {
      return false;
    }

    std::istringstream request_stream(line);
    request_stream >> request.method >> request.path >> request.version;

    auto query_pos = request.path.find('?');
    if (query_pos != std::string::npos)
    {
      std::string query = request.path.substr(query_pos + 1);
      request.path = request.path.substr(0, query_pos);

      std::istringstream query_stream(query);
      std::string param;
      while (std::getline(query_stream, param, '&'))
      {
        auto eq_pos = param.find('=');
        if (eq_pos != std::string::npos)
        {
          request.query_params[param.substr(0, eq_pos)] = param.substr(eq_pos + 1);
        }
      }
    }

    while (reader.read_line(line) && !line.empty())
    {
      auto colon_pos = line.find(':');
      if (colon_pos != std::string::npos)
      {
        std::string key = line.substr(0, colon_pos);
        std::string value = line.substr(colon_pos + 1);
        while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
        {
          value.erase(0, 1);
        }
        for (auto& c : key)
        {
          c = static_cast<char>(std::tolower(c));
        }
        request.headers[key] = value;
      }
    }

    auto cl_it = request.headers.find("content-length");
    if (cl_it != request.headers.end())
    {
      std::size_t content_length = std::stoul(cl_it->second);
      if (content_length > 0)
      {
        reader.read_bytes(content_length, request.body);
      }
    }

    return true;
  }

  // Format HTTP response (matches http_server.cpp pattern)
  static std::string format_response(const HttpResponse& response)
  {
    std::ostringstream stream;
    stream << "HTTP/1.1 " << response.status_code << " "
           << response.status_text << "\r\n";

    for (const auto& [key, value] : response.headers)
    {
      stream << key << ": " << value << "\r\n";
    }

    stream << "Content-Length: " << response.body.size() << "\r\n";
    stream << "\r\n";
    stream << response.body;

    return stream.str();
  }

  // Worker thread loop
  void worker_loop()
  {
    while (running_ || queue_.size() > 0)
    {
      int client_fd = queue_.pop();
      if (client_fd < 0)
      {
        break;
      }
      handle_client(client_fd);
    }
  }

  void handle_client(int client_fd)
  {
    auto start = std::chrono::steady_clock::now();
    std::string request_id = generate_uuid_v4();

    SocketReader reader(client_fd);
    HttpRequest request;

    if (parse_request(reader, request))
    {
      HttpResponse response = route_request(request);

      // Inject request ID header
      response.headers["X-Request-ID"] = request_id;

      // Security headers
      response.headers["X-Content-Type-Options"] = "nosniff";
      response.headers["X-Frame-Options"] = "DENY";
      response.headers["Cache-Control"] = "no-store";

      std::string response_str = format_response(response);
      ::send(client_fd, response_str.data(), response_str.size(), 0);

      auto end = std::chrono::steady_clock::now();
      double duration_ms = std::chrono::duration<double, std::milli>(end - start).count();

      logger_.log(request_id, request.method, request.path,
                  response.status_code, response.body.size(),
                  duration_ms, "" /* remote_addr placeholder */);
    }

    NEAM_CLOSESOCKET(client_fd);
  }

  HttpResponse route_request(const HttpRequest& request)
  {
    // Handle CORS preflight
    if (cors_enabled_ && request.method == "OPTIONS")
    {
      HttpResponse response;
      response.status_code = 204;
      response.status_text = "No Content";
      add_cors_headers(response);
      return response;
    }

    std::lock_guard<std::mutex> lock(routes_mutex_);

    // Exact match
    auto method_it = routes_.find(request.method);
    if (method_it != routes_.end())
    {
      auto path_it = method_it->second.find(request.path);
      if (path_it != method_it->second.end())
      {
        HttpResponse response = path_it->second(request);
        if (cors_enabled_)
        {
          add_cors_headers(response);
        }
        return response;
      }
    }

    // Prefix match
    auto prefix_it = prefix_routes_.find(request.method);
    if (prefix_it != prefix_routes_.end())
    {
      for (const auto& [prefix, handler] : prefix_it->second)
      {
        if (request.path.rfind(prefix, 0) == 0)
        {
          HttpResponse response = handler(request);
          if (cors_enabled_)
          {
            add_cors_headers(response);
          }
          return response;
        }
      }
    }

    HttpResponse response = HttpResponse::not_found(
        "Route not found: " + request.method + " " + request.path);
    if (cors_enabled_)
    {
      add_cors_headers(response);
    }
    return response;
  }

  void add_cors_headers(HttpResponse& response)
  {
    response.headers["Access-Control-Allow-Origin"] = "*";
    response.headers["Access-Control-Allow-Methods"] = "GET, POST, OPTIONS";
    response.headers["Access-Control-Allow-Headers"] = "Content-Type, Authorization";
  }

  void send_503(int client_fd)
  {
    HttpResponse response = HttpResponse::error("Service Unavailable: server overloaded", 503);
    response.status_text = "Service Unavailable";
    response.headers["Retry-After"] = "5";
    response.headers["X-Request-ID"] = generate_uuid_v4();
    response.headers["X-Content-Type-Options"] = "nosniff";

    std::string response_str = format_response(response);
    ::send(client_fd, response_str.data(), response_str.size(), 0);

    logger_.log(response.headers["X-Request-ID"], "UNKNOWN", "/",
                503, response.body.size(), 0.0, "");
  }

  struct PrefixRoute
  {
    std::string prefix;
    RouteHandler handler;
  };

  Config config_;
  ConnectionQueue queue_;
  AccessLogger logger_;
  std::atomic<bool> running_{false};
  std::atomic<bool> cors_enabled_{false};
  int server_fd_ = -1;

  std::mutex routes_mutex_;
  std::map<std::string, std::map<std::string, RouteHandler>> routes_;
  std::map<std::string, std::vector<PrefixRoute>> prefix_routes_;

  std::vector<std::thread> workers_;
};

}  // namespace neamc::api
