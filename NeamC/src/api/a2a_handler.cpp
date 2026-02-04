// SPDX-License-Identifier: Apache-2.0
//
// Neam API - A2A Protocol Handler Implementation
//

#include "neamc/api/a2a_handler.hpp"

#include <iostream>
#include <mutex>
#include <sstream>

namespace neamc::api
{

void A2AHandler::register_routes(HttpServer& server)
{
  server.get("/.well-known/agent.json",
             [this](const HttpRequest& req) { return handle_well_known(req); });

  server.post("/a2a",
              [this](const HttpRequest& req) { return handle_a2a_rpc(req); });

  // Prefix route for /a2a/tasks/*/stream is handled via get_prefix
  server.get_prefix("/a2a/tasks/",
                    [this](const HttpRequest& req) { return handle_task_stream(req); });
}

HttpResponse A2AHandler::handle_well_known(const HttpRequest&)
{
  std::lock_guard<std::mutex> lock(vm_mutex_);

  // Find the first registered agent card
  const auto& cards = vm_.agent_cards();
  if (cards.empty())
  {
    return HttpResponse::error("No agent cards registered", 404);
  }

  // Use the first agent card
  const auto& [agent_name, card] = *cards.begin();
  nlohmann::json agent_card = vm_.get_agent_card_json(agent_name);

  // Wrap in A2A-compliant format
  nlohmann::json a2a_card;
  a2a_card["name"] = agent_name;
  a2a_card["url"] = "/a2a";
  a2a_card["version"] = card.version.empty() ? "1.0.0" : card.version;
  a2a_card["description"] = card.description;
  a2a_card["capabilities"] = {{"streaming", true}, {"pushNotifications", false}};

  nlohmann::json skills = nlohmann::json::array();
  skills.push_back({{"id", "ask"}, {"name", "Ask Agent"}});
  a2a_card["skills"] = skills;

  if (!card.authentication.empty())
  {
    a2a_card["authentication"] = {{"schemes", nlohmann::json::array({card.authentication})}};
  }

  // Include input/output schemas if defined
  if (!agent_card.empty())
  {
    if (agent_card.contains("input_schema"))
    {
      a2a_card["inputSchema"] = agent_card["input_schema"];
    }
    if (agent_card.contains("output_schema"))
    {
      a2a_card["outputSchema"] = agent_card["output_schema"];
    }
  }

  return HttpResponse::json(a2a_card);
}

HttpResponse A2AHandler::handle_a2a_rpc(const HttpRequest& request)
{
  nlohmann::json body;
  try
  {
    body = nlohmann::json::parse(request.body);
  }
  catch (const std::exception&)
  {
    return json_rpc_error(-32700, "Parse error", nullptr);
  }

  // Validate JSON-RPC 2.0 structure
  if (!body.contains("jsonrpc") || body["jsonrpc"] != "2.0")
  {
    return json_rpc_error(-32600, "Invalid Request: missing jsonrpc 2.0", body.value("id", nullptr));
  }
  if (!body.contains("method") || !body["method"].is_string())
  {
    return json_rpc_error(-32600, "Invalid Request: missing method", body.value("id", nullptr));
  }

  const std::string method = body["method"];
  const nlohmann::json params = body.value("params", nlohmann::json::object());
  const nlohmann::json id = body.value("id", nullptr);

  // Dispatch by method
  if (method == "tasks/send")
  {
    return dispatch_tasks_send(params, id);
  }
  if (method == "tasks/get")
  {
    return dispatch_tasks_get(params, id);
  }
  if (method == "tasks/cancel")
  {
    return dispatch_tasks_cancel(params, id);
  }

  return json_rpc_error(-32601, "Method not found: " + method, id);
}

HttpResponse A2AHandler::handle_task_stream(const HttpRequest& request)
{
  // Extract task ID from path: /a2a/tasks/{id}/stream
  const std::string prefix = "/a2a/tasks/";
  if (request.path.size() <= prefix.size())
  {
    return HttpResponse::bad_request("Missing task ID");
  }

  std::string remainder = request.path.substr(prefix.size());
  // Remove trailing /stream if present
  auto slash_pos = remainder.find('/');
  std::string task_id = (slash_pos != std::string::npos) ? remainder.substr(0, slash_pos) : remainder;

  std::lock_guard<std::mutex> lock(vm_mutex_);

  nlohmann::json result = vm_.get_task_result(task_id);

  // Return as SSE-formatted response
  std::ostringstream sse;
  sse << "data: " << result.dump() << "\n\n";

  HttpResponse response;
  response.status_code = 200;
  response.status_text = "OK";
  response.headers["Content-Type"] = "text/event-stream";
  response.headers["Cache-Control"] = "no-cache";
  response.headers["Connection"] = "keep-alive";
  response.body = sse.str();
  return response;
}

HttpResponse A2AHandler::dispatch_tasks_send(const nlohmann::json& params,
                                              const nlohmann::json& id)
{
  // Extract agent name and input
  std::string agent_name;
  if (params.contains("agent"))
  {
    agent_name = params["agent"];
  }
  else
  {
    // Use first registered agent
    std::lock_guard<std::mutex> lock(vm_mutex_);
    const auto& cards = vm_.agent_cards();
    if (!cards.empty())
    {
      agent_name = cards.begin()->first;
    }
    else
    {
      return json_rpc_error(-32602, "No agent specified and none registered", id);
    }
  }

  nlohmann::json input = params.value("input", nlohmann::json::object());
  std::string input_text;
  if (input.is_string())
  {
    input_text = input.get<std::string>();
  }
  else if (input.contains("text"))
  {
    input_text = input["text"].get<std::string>();
  }
  else
  {
    input_text = input.dump();
  }

  std::lock_guard<std::mutex> lock(vm_mutex_);

  // Create and submit task
  std::string task_id = vm_.create_task(agent_name, input);
  vm_.submit_task(task_id);

  // Run the agent synchronously
  std::string response = vm_.call_agent_internal(agent_name, input_text);

  // Update task with result
  auto* task = vm_.get_task(task_id);
  if (task)
  {
    task->output = {{"text", response}};
    task->status = vm::VirtualMachine::TaskStatusVM::kCompleted;
    task->completed_at = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
  }

  nlohmann::json result;
  result["taskId"] = task_id;
  result["status"] = "completed";
  result["output"] = {{"text", response}};

  return json_rpc_result(result, id);
}

HttpResponse A2AHandler::dispatch_tasks_get(const nlohmann::json& params,
                                             const nlohmann::json& id)
{
  if (!params.contains("taskId"))
  {
    return json_rpc_error(-32602, "Missing taskId parameter", id);
  }

  const std::string task_id = params["taskId"];

  std::lock_guard<std::mutex> lock(vm_mutex_);

  std::string status_str = vm_.get_task_status_string(task_id);
  nlohmann::json task_result = vm_.get_task_result(task_id);

  nlohmann::json result;
  result["taskId"] = task_id;
  result["status"] = status_str;
  result["output"] = task_result;

  return json_rpc_result(result, id);
}

HttpResponse A2AHandler::dispatch_tasks_cancel(const nlohmann::json& params,
                                                const nlohmann::json& id)
{
  if (!params.contains("taskId"))
  {
    return json_rpc_error(-32602, "Missing taskId parameter", id);
  }

  const std::string task_id = params["taskId"];

  std::lock_guard<std::mutex> lock(vm_mutex_);
  vm_.cancel_task(task_id);

  nlohmann::json result;
  result["taskId"] = task_id;
  result["status"] = "cancelled";

  return json_rpc_result(result, id);
}

HttpResponse A2AHandler::json_rpc_error(int code, const std::string& message,
                                         const nlohmann::json& id)
{
  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["error"] = {{"code", code}, {"message", message}};
  response["id"] = id;
  return HttpResponse::json(response);
}

HttpResponse A2AHandler::json_rpc_result(const nlohmann::json& result,
                                          const nlohmann::json& id)
{
  nlohmann::json response;
  response["jsonrpc"] = "2.0";
  response["result"] = result;
  response["id"] = id;
  return HttpResponse::json(response);
}

}  // namespace neamc::api
