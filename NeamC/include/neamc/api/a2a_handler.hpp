// SPDX-License-Identifier: Apache-2.0
//
// Neam API - A2A (Agent-to-Agent) Protocol Handler
//
// Implements Google A2A protocol endpoints:
//   GET  /.well-known/agent.json  — Agent card discovery
//   POST /a2a                     — JSON-RPC 2.0 dispatch
//   GET  /a2a/tasks/*/stream      — SSE task result streaming
//

#pragma once

#include <string>

#include "neamc/api/http_server.hpp"
#include "neamc/vm/vm.hpp"

namespace neamc::api
{

class A2AHandler
{
public:
  explicit A2AHandler(vm::VirtualMachine& vm, std::mutex& vm_mutex)
      : vm_(vm), vm_mutex_(vm_mutex)
  {
  }

  /// Register all A2A routes on the given HTTP server.
  void register_routes(HttpServer& server);

  /// GET /.well-known/agent.json — returns A2A agent card
  HttpResponse handle_well_known(const HttpRequest& request);

  /// POST /a2a — JSON-RPC 2.0 dispatcher
  HttpResponse handle_a2a_rpc(const HttpRequest& request);

  /// GET /a2a/tasks/* — SSE stream for task result (prefix-matched)
  HttpResponse handle_task_stream(const HttpRequest& request);

private:
  HttpResponse dispatch_tasks_send(const nlohmann::json& params, const nlohmann::json& id);
  HttpResponse dispatch_tasks_get(const nlohmann::json& params, const nlohmann::json& id);
  HttpResponse dispatch_tasks_cancel(const nlohmann::json& params, const nlohmann::json& id);

  static HttpResponse json_rpc_error(int code, const std::string& message,
                                     const nlohmann::json& id);
  static HttpResponse json_rpc_result(const nlohmann::json& result, const nlohmann::json& id);

  vm::VirtualMachine& vm_;
  std::mutex& vm_mutex_;
};

}  // namespace neamc::api
