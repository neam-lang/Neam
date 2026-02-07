//
// Neam LLM - OpenAI adapter
//

#include "neamc/llm/openai_adapter.hpp"

#include <nlohmann/json.hpp>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"
#include "neamc/llm/llm_logger.hpp"
#include "neamc/vm/async/executor.hpp"

namespace neamc::llm
{
namespace
{
std::string extract_response_text(const nlohmann::json& json)
{
  if (json.contains("choices") && json.at("choices").is_array() &&
      !json.at("choices").empty())
  {
    const auto& choice = json.at("choices").front();
    if (choice.contains("message"))
    {
      return choice.at("message").value("content", "");
    }
    if (choice.contains("text"))
    {
      return choice.at("text").get<std::string>();
    }
  }
  throw std::runtime_error("OpenAI response missing content");
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

class OpenAIAdapter final : public LLMProvider
{
public:
  explicit OpenAIAdapter(ProviderConfig config)
      : config_(std::move(config))
  {
    if (config_.model.empty())
    {
      throw std::runtime_error("OpenAI model is required");
    }
    if (config_.endpoint.empty())
    {
      throw std::runtime_error("OpenAI endpoint is required");
    }
    if (config_.api_key.empty())
    {
      throw std::runtime_error("OpenAI API key is required");
    }
  }

  std::string complete(const std::string& prompt) override
  {
    std::vector<Message> messages;
    messages.push_back({"user", prompt});
    return chat(messages);
  }

  vm::async::Future<std::string> complete_async(const std::string& prompt) override
  {
    return vm::async::Executor::global().submit([this, prompt]() { return complete(prompt); });
  }

  std::string chat(const std::vector<Message>& messages) override
  {
    LLMLogger::info("openai", "Chat request: model=" + config_.model +
                                  ", messages=" + std::to_string(messages.size()));

    nlohmann::json payload;
    payload["model"] = config_.model;
    payload["messages"] = message_list_to_json(messages);
    if (config_.temperature > 0.0)
    {
      payload["temperature"] = config_.temperature;
    }

    try
    {
      const std::string response = http_post_json(
          config_.endpoint, payload.dump(),
          {"Content-Type: application/json",
           "Authorization: Bearer " + config_.api_key});
      auto parsed = nlohmann::json::parse(response);
      auto text = extract_response_text(parsed);
      LLMLogger::debug("openai", "Response: " + std::to_string(text.size()) + " chars");
      return text;
    }
    catch (const std::exception& e)
    {
      LLMLogger::error("openai", "Chat failed: " + std::string(e.what()));
      throw;
    }
  }

private:
  ProviderConfig config_;
};

std::unique_ptr<LLMProvider> create_openai_provider(const ProviderConfig& config)
{
  return std::make_unique<OpenAIAdapter>(config);
}
}  // namespace neamc::llm
