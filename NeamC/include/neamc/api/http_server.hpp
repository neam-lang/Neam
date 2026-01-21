//
// Neam API - Native HTTP Server for Agent Endpoints
//

#pragma once

#include <string>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>

namespace neamc::api
{

// HTTP Request structure
struct HttpRequest
{
  std::string method;
  std::string path;
  std::string version;
  std::map<std::string, std::string> headers;
  std::string body;

  // Query parameters extracted from path
  std::map<std::string, std::string> query_params;
};

// HTTP Response structure
struct HttpResponse
{
  int status_code = 200;
  std::string status_text = "OK";
  std::map<std::string, std::string> headers;
  std::string body;

  static HttpResponse json(const nlohmann::json& data, int code = 200);
  static HttpResponse error(const std::string& message, int code = 500);
  static HttpResponse not_found(const std::string& message = "Not Found");
  static HttpResponse bad_request(const std::string& message);
};

// Route handler type
using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

// HTTP Server class
class HttpServer
{
public:
  explicit HttpServer(int port, const std::string& host = "0.0.0.0");
  ~HttpServer();

  // Register route handlers
  void get(const std::string& path, RouteHandler handler);
  void post(const std::string& path, RouteHandler handler);
  void options(const std::string& path, RouteHandler handler);

  // Enable CORS for all routes
  void enable_cors();

  // Start the server (blocking)
  void run();

  // Stop the server
  void stop();

  // Check if running
  bool is_running() const;

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace neamc::api
