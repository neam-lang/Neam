// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Whisper.cpp local STT provider implementation
//
// Connects to a local whisper.cpp server with OpenAI-compatible API.
//

#include "stt_whisper_local.hpp"

#include <stdexcept>

#include <nlohmann/json.hpp>

#include "neamc/llm/http_client.hpp"

namespace neamc::voice
{
namespace
{
class WhisperCppSTT : public STTProvider
{
public:
  explicit WhisperCppSTT(const STTConfig& config)
      : model_(config.model),
        endpoint_(config.endpoint.empty() ? "http://localhost:8080" : config.endpoint),
        language_(config.language),
        format_(config.format)
  {
  }

  std::string transcribe(const std::string& audio_path) override
  {
    std::vector<std::pair<std::string, std::string>> fields;
    if (!model_.empty())
    {
      fields.emplace_back("model", model_);
    }
    if (!language_.empty())
    {
      fields.emplace_back("language", language_);
    }
    if (!format_.empty())
    {
      fields.emplace_back("response_format", format_);
    }

    const std::string url = endpoint_ + "/v1/audio/transcriptions";
    const std::string response_str = llm::http_post_multipart(
        url,
        {},  // No auth headers for local server
        "file", audio_path,
        fields,
        60000);

    const auto response_json = nlohmann::json::parse(response_str);
    return response_json.value("text", "");
  }

private:
  std::string model_;
  std::string endpoint_;
  std::string language_;
  std::string format_;
};
}  // namespace

std::unique_ptr<STTProvider> make_whisper_cpp_stt(const STTConfig& config)
{
  return std::make_unique<WhisperCppSTT>(config);
}
}  // namespace neamc::voice
