//
// Neam API - Native HTTP Server Implementation
//

#include "neamc/api/http_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

namespace neamc::api
{

// HttpResponse static methods
HttpResponse HttpResponse::json(const nlohmann::json& data, int code)
{
  HttpResponse resp;
  resp.status_code = code;
  resp.status_text = (code == 200) ? "OK" : "Error";
  resp.headers["Content-Type"] = "application/json";
  resp.body = data.dump();
  return resp;
}

HttpResponse HttpResponse::error(const std::string& message, int code)
{
  nlohmann::json data = {{"error", message}};
  HttpResponse resp;
  resp.status_code = code;
  resp.status_text = "Error";
  resp.headers["Content-Type"] = "application/json";
  resp.body = data.dump();
  return resp;
}

HttpResponse HttpResponse::not_found(const std::string& message)
{
  return error(message, 404);
}

HttpResponse HttpResponse::bad_request(const std::string& message)
{
  return error(message, 400);
}

// Internal socket reader
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

// Parse HTTP request
bool parse_http_request(SocketReader& reader, HttpRequest& request)
{
  std::string line;

  // Read request line
  if (!reader.read_line(line) || line.empty())
  {
    return false;
  }

  // Parse request line: METHOD PATH HTTP/VERSION
  std::istringstream request_stream(line);
  request_stream >> request.method >> request.path >> request.version;

  // Extract query parameters from path
  auto query_pos = request.path.find('?');
  if (query_pos != std::string::npos)
  {
    std::string query = request.path.substr(query_pos + 1);
    request.path = request.path.substr(0, query_pos);

    // Parse query parameters
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

  // Read headers
  while (reader.read_line(line) && !line.empty())
  {
    auto colon_pos = line.find(':');
    if (colon_pos != std::string::npos)
    {
      std::string key = line.substr(0, colon_pos);
      std::string value = line.substr(colon_pos + 1);
      // Trim leading whitespace from value
      while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
      {
        value.erase(0, 1);
      }
      // Convert key to lowercase for easier matching
      for (auto& c : key)
      {
        c = static_cast<char>(std::tolower(c));
      }
      request.headers[key] = value;
    }
  }

  // Read body if Content-Length header is present
  auto content_length_it = request.headers.find("content-length");
  if (content_length_it != request.headers.end())
  {
    std::size_t content_length = std::stoul(content_length_it->second);
    if (content_length > 0)
    {
      reader.read_bytes(content_length, request.body);
    }
  }

  return true;
}

// Format HTTP response
std::string format_http_response(const HttpResponse& response)
{
  std::ostringstream stream;
  stream << "HTTP/1.1 " << response.status_code << " " << response.status_text << "\r\n";

  for (const auto& [key, value] : response.headers)
  {
    stream << key << ": " << value << "\r\n";
  }

  stream << "Content-Length: " << response.body.size() << "\r\n";
  stream << "\r\n";
  stream << response.body;

  return stream.str();
}

// Server implementation
class HttpServer::Impl
{
public:
  Impl(int port, const std::string& host)
      : port_(port), host_(host)
  {
  }

  ~Impl()
  {
    stop();
  }

  void get(const std::string& path, RouteHandler handler)
  {
    routes_["GET"][path] = std::move(handler);
  }

  void post(const std::string& path, RouteHandler handler)
  {
    routes_["POST"][path] = std::move(handler);
  }

  void options(const std::string& path, RouteHandler handler)
  {
    routes_["OPTIONS"][path] = std::move(handler);
  }

  void enable_cors()
  {
    cors_enabled_ = true;
  }

  void run()
  {
    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0)
    {
      throw std::runtime_error("Failed to create socket");
    }

    // Allow address reuse
    int opt = 1;
    if (::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
      ::close(server_fd_);
      throw std::runtime_error("Failed to set socket options");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port_));

    if (host_ == "0.0.0.0")
    {
      addr.sin_addr.s_addr = INADDR_ANY;
    }
    else
    {
      if (::inet_pton(AF_INET, host_.c_str(), &addr.sin_addr) <= 0)
      {
        ::close(server_fd_);
        throw std::runtime_error("Invalid host address");
      }
    }

    if (::bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
      ::close(server_fd_);
      throw std::runtime_error("Failed to bind socket to " + host_ + ":" + std::to_string(port_));
    }

    if (::listen(server_fd_, 10) < 0)
    {
      ::close(server_fd_);
      throw std::runtime_error("Failed to listen on socket");
    }

    running_ = true;
    std::cout << "Neam API Server running on http://" << host_ << ":" << port_ << "\n";

    while (running_)
    {
      sockaddr_in client_addr{};
      socklen_t client_len = sizeof(client_addr);
      int client_fd = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);

      if (client_fd < 0)
      {
        if (!running_)
        {
          break;
        }
        continue;
      }

      // Handle request in a separate thread
      std::thread([this, client_fd]()
                  { handle_client(client_fd); })
          .detach();
    }
  }

  void stop()
  {
    running_ = false;
    if (server_fd_ >= 0)
    {
      ::close(server_fd_);
      server_fd_ = -1;
    }
  }

  bool is_running() const
  {
    return running_;
  }

private:
  void handle_client(int client_fd)
  {
    SocketReader reader(client_fd);
    HttpRequest request;

    if (parse_http_request(reader, request))
    {
      HttpResponse response = handle_request(request);
      std::string response_str = format_http_response(response);
      ::send(client_fd, response_str.data(), response_str.size(), 0);
    }

    ::close(client_fd);
  }

  HttpResponse handle_request(const HttpRequest& request)
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

    // Find route handler
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

    HttpResponse response = HttpResponse::not_found("Route not found: " + request.method + " " + request.path);
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

  int port_;
  std::string host_;
  int server_fd_ = -1;
  std::atomic<bool> running_{false};
  bool cors_enabled_ = false;
  std::map<std::string, std::map<std::string, RouteHandler>> routes_;
};

// HttpServer public methods
HttpServer::HttpServer(int port, const std::string& host)
    : impl_(std::make_unique<Impl>(port, host))
{
}

HttpServer::~HttpServer() = default;

void HttpServer::get(const std::string& path, RouteHandler handler)
{
  impl_->get(path, std::move(handler));
}

void HttpServer::post(const std::string& path, RouteHandler handler)
{
  impl_->post(path, std::move(handler));
}

void HttpServer::options(const std::string& path, RouteHandler handler)
{
  impl_->options(path, std::move(handler));
}

void HttpServer::enable_cors()
{
  impl_->enable_cors();
}

void HttpServer::run()
{
  impl_->run();
}

void HttpServer::stop()
{
  impl_->stop();
}

bool HttpServer::is_running() const
{
  return impl_->is_running();
}

}  // namespace neamc::api
