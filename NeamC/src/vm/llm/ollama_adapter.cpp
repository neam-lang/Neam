//
// Neam LLM - Ollama adapter
//
// v0.6.6: Added chat_with_tools() for Ollama tool calling (OpenAI-compatible format).
//

#include "neamc/llm/ollama_adapter.hpp"

#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"
#include "neamc/llm/llm_logger.hpp"
#include "neamc/vm/async/executor.hpp"

namespace neamc::llm
{
namespace
{
std::string ensure_scheme(std::string host)
{
  if (host.rfind("http://", 0) == 0 || host.rfind("https://", 0) == 0)
  {
    return host;
  }
  return "http://" + host;
}

std::string trim_trailing_slash(std::string value)
{
  while (!value.empty() && value.back() == '/')
  {
    value.pop_back();
  }
  return value;
}

std::string build_url(const std::string& base, const std::string& path)
{
  return trim_trailing_slash(base) + path;
}

std::string extract_response_text(const nlohmann::json& json)
{
  if (json.contains("message"))
  {
    const auto& message = json.at("message");
    if (message.is_object() && message.contains("content"))
    {
      return message.at("content").get<std::string>();
    }
  }
  if (json.contains("response"))
  {
    return json.at("response").get<std::string>();
  }
  if (json.contains("choices") && json.at("choices").is_array() &&
      !json.at("choices").empty())
  {
    const auto& choice = json.at("choices").front();
    if (choice.contains("message"))
    {
      return choice.at("message").at("content").get<std::string>();
    }
    if (choice.contains("text"))
    {
      return choice.at("text").get<std::string>();
    }
  }
  throw std::runtime_error("Ollama response missing content");
}

// Build messages array supporting tool call history.
nlohmann::json build_messages_json(const std::vector<Message>& messages)
{
  nlohmann::json arr = nlohmann::json::array();
  for (const auto& msg : messages)
  {
    nlohmann::json entry;
    entry["role"] = msg.role;

    if (!msg.content_blocks.is_null() && msg.content_blocks.is_array() &&
        !msg.content_blocks.empty())
    {
      // Assistant message with tool_calls (Ollama uses OpenAI-compatible format)
      if (msg.role == "assistant" && msg.content_blocks.front().contains("type") &&
          msg.content_blocks.front().at("type") == "function")
      {
        entry["content"] = msg.content.empty() ? nlohmann::json(nullptr) : nlohmann::json(msg.content);
        nlohmann::json tool_calls = nlohmann::json::array();
        for (const auto& block : msg.content_blocks)
        {
          nlohmann::json tc;
          tc["id"] = block.value("id", "");
          tc["type"] = "function";
          tc["function"] = {
              {"name", block.value("name", "")},
              {"arguments", block.value("arguments", "")}};
          tool_calls.push_back(std::move(tc));
        }
        entry["tool_calls"] = std::move(tool_calls);
      }
      else if (msg.role == "tool")
      {
        entry["content"] = msg.content;
        if (msg.content_blocks.front().contains("tool_call_id"))
        {
          entry["tool_call_id"] = msg.content_blocks.front().at("tool_call_id");
        }
      }
      else
      {
        entry["content"] = msg.content;
      }
    }
    else
    {
      entry["content"] = msg.content;
    }
    arr.push_back(std::move(entry));
  }
  return arr;
}

// Parse Ollama chat response with tool call detection.
ChatResponse parse_ollama_response(const nlohmann::json& parsed)
{
  ChatResponse resp;

  if (parsed.contains("message"))
  {
    const auto& message = parsed.at("message");

    if (message.contains("content") && !message.at("content").is_null())
    {
      resp.text = message.value("content", "");
    }

    // Ollama tool calls (same format as OpenAI)
    if (message.contains("tool_calls") && message.at("tool_calls").is_array())
    {
      for (const auto& tc : message.at("tool_calls"))
      {
        ToolCall tool_call;
        tool_call.id = tc.value("id", "");
        if (tc.contains("function"))
        {
          const auto& fn = tc.at("function");
          tool_call.name = fn.value("name", "");
          if (fn.contains("arguments"))
          {
            if (fn.at("arguments").is_string())
            {
              try
              {
                tool_call.input = nlohmann::json::parse(fn.at("arguments").get<std::string>());
              }
              catch (...)
              {
                tool_call.input = nlohmann::json::object();
              }
            }
            else
            {
              tool_call.input = fn.at("arguments");
            }
          }
        }
        resp.tool_calls.push_back(std::move(tool_call));
      }
    }
  }

  resp.stop_reason = resp.tool_calls.empty() ? "end_turn" : "tool_use";
  return resp;
}
}  // namespace

class OllamaAdapter final : public LLMProvider
{
public:
  explicit OllamaAdapter(ProviderConfig config)
      : config_(std::move(config))
  {
    if (config_.model.empty())
    {
      throw std::runtime_error("Ollama model is required");
    }
    if (config_.endpoint.empty())
    {
      const std::string fallback =
          config_.default_host.empty() ? "http://localhost:11434" : config_.default_host;
      config_.endpoint = fallback;
    }
    config_.endpoint = ensure_scheme(config_.endpoint);
  }

  std::string complete(const std::string& prompt) override
  {
    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["prompt"] = prompt;
    payload["stream"] = false;
    if (config_.temperature > 0.0)
    {
      payload["options"] = { {"temperature", config_.temperature} };
    }

    const std::string response = http_post_json(
        build_url(config_.endpoint, "/api/generate"), payload.dump(),
        {"Content-Type: application/json"});
    return extract_response_text(nlohmann::json::parse(response));
  }

  vm::async::Future<std::string> complete_async(const std::string& prompt) override
  {
    return vm::async::Executor::global().submit([this, prompt]() { return complete(prompt); });
  }

  std::string chat(const std::vector<Message>& messages) override
  {
    LLMLogger::info("ollama", "Chat request: model=" + config_.model +
                                  ", messages=" + std::to_string(messages.size()));

    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["messages"] = build_messages_json(messages);
    payload["stream"] = false;
    if (config_.temperature > 0.0)
    {
      payload["options"] = { {"temperature", config_.temperature} };
    }

    try
    {
      const std::string response = http_post_json(
          build_url(config_.endpoint, "/api/chat"), payload.dump(),
          {"Content-Type: application/json"});
      auto text = extract_response_text(nlohmann::json::parse(response));
      LLMLogger::debug("ollama", "Response: " + std::to_string(text.size()) + " chars");
      return text;
    }
    catch (const std::exception& e)
    {
      LLMLogger::error("ollama", "Chat failed: " + std::string(e.what()));
      throw;
    }
  }

  // v0.6.6: Ollama tool calling via chat API (OpenAI-compatible format)
  ChatResponse chat_with_tools(const std::vector<Message>& messages,
                               const std::vector<ToolDefinition>& tools,
                               const std::string& tool_choice) override
  {
    if (tools.empty())
    {
      ChatResponse resp;
      resp.text = chat(messages);
      resp.stop_reason = "end_turn";
      return resp;
    }

    LLMLogger::info("ollama", "Tool-aware chat: model=" + config_.model +
                                    ", tools=" + std::to_string(tools.size()) +
                                    ", messages=" + std::to_string(messages.size()));

    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["messages"] = build_messages_json(messages);
    payload["stream"] = false;
    if (config_.temperature > 0.0)
    {
      payload["options"] = { {"temperature", config_.temperature} };
    }

    // Build tools array in OpenAI-compatible format
    nlohmann::json tools_json = nlohmann::json::array();
    for (const auto& tool : tools)
    {
      tools_json.push_back(
          {{"type", "function"},
           {"function",
            {{"name", tool.name},
             {"description", tool.description},
             {"parameters", tool.input_schema}}}});
    }
    payload["tools"] = std::move(tools_json);
    (void)tool_choice;  // Ollama does not support tool_choice parameter

    try
    {
      const std::string response = http_post_json(
          build_url(config_.endpoint, "/api/chat"), payload.dump(),
          {"Content-Type: application/json"});
      auto parsed = nlohmann::json::parse(response);
      auto chat_resp = parse_ollama_response(parsed);
      LLMLogger::debug("ollama", "Tool response: stop_reason=" + chat_resp.stop_reason +
                                       ", tool_calls=" + std::to_string(chat_resp.tool_calls.size()));
      return chat_resp;
    }
    catch (const std::exception& e)
    {
      LLMLogger::error("ollama", "Tool chat failed: " + std::string(e.what()));
      throw;
    }
  }

  // v0.6.8: NDJSON streaming for Ollama
  void chat_stream(const std::vector<Message>& messages, StreamCallback callback) override
  {
    LLMLogger::info("ollama", "Stream request: model=" + config_.model);

    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["messages"] = build_messages_json(messages);
    payload["stream"] = true;
    if (config_.temperature > 0.0)
    {
      payload["options"] = {{"temperature", config_.temperature}};
    }

    std::string buffer;
    http_post_streaming(
        build_url(config_.endpoint, "/api/chat"), payload.dump(),
        {"Content-Type: application/json"},
        [&](const char* data, size_t size) {
          buffer.append(data, size);

          // Parse NDJSON: one JSON object per line
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
              auto parsed = nlohmann::json::parse(line);
              bool done = parsed.value("done", false);

              if (parsed.contains("message"))
              {
                std::string content = parsed["message"].value("content", "");
                if (!content.empty())
                {
                  callback(content, done);
                }
              }
              else if (parsed.contains("response"))
              {
                std::string content = parsed["response"].get<std::string>();
                if (!content.empty())
                {
                  callback(content, done);
                }
              }

              if (done)
              {
                callback("", true);
              }
            }
            catch (const nlohmann::json::parse_error&)
            {
              // Skip malformed lines
            }
          }
          buffer = buffer.substr(pos);
        },
        120000);
  }

private:
  ProviderConfig config_;
};

std::unique_ptr<LLMProvider> create_ollama_provider(const ProviderConfig& config)
{
  return std::make_unique<OllamaAdapter>(config);
}
}  // namespace neamc::llm
