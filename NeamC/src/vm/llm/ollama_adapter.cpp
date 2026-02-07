//
// Neam LLM - Ollama adapter
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

nlohmann::json message_list_to_json(const std::vector<Message>& messages)
{
  nlohmann::json json = nlohmann::json::array();
  for (const auto& message : messages)
  {
    json.push_back({{"role", message.role}, {"content", message.content}});
  }
  return json;
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
    payload["messages"] = message_list_to_json(messages);
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

private:
  ProviderConfig config_;
};

std::unique_ptr<LLMProvider> create_ollama_provider(const ProviderConfig& config)
{
  return std::make_unique<OllamaAdapter>(config);
}
}  // namespace neamc::llm
