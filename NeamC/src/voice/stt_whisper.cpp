// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Whisper STT provider implementation
//

#include "neamc/voice/stt.hpp"

#include <cstdlib>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "neamc/llm/http_client.hpp"

// Forward declarations for new provider factories
namespace neamc::voice
{
// Defined in stt_gemini.cpp
std::unique_ptr<STTProvider> make_gemini_stt(const STTConfig& config);
// Defined in stt_whisper_local.cpp
std::unique_ptr<STTProvider> make_whisper_cpp_stt(const STTConfig& config);
}  // namespace neamc::voice

namespace neamc::voice
{
namespace
{
class WhisperSTT : public STTProvider
{
public:
  explicit WhisperSTT(const STTConfig& config)
      : model_(config.model.empty() ? "whisper-1" : config.model),
        api_key_(config.api_key),
        language_(config.language),
        format_(config.format)
  {
  }

  std::string transcribe(const std::string& audio_path) override
  {
    std::string key = api_key_;
    if (key.empty())
    {
      if (const char* env_val = std::getenv("OPENAI_API_KEY"))
      {
        key = env_val;
      }
      else
      {
        throw std::runtime_error("OPENAI_API_KEY not set for Whisper STT");
      }
    }

    std::vector<std::pair<std::string, std::string>> fields;
    fields.emplace_back("model", model_);
    if (!language_.empty())
    {
      fields.emplace_back("language", language_);
    }
    if (!format_.empty())
    {
      fields.emplace_back("response_format", format_);
    }

    const std::string response_str = llm::http_post_multipart(
        "https://api.openai.com/v1/audio/transcriptions",
        {"Authorization: Bearer " + key},
        "file", audio_path,
        fields,
        60000);

    const auto response_json = nlohmann::json::parse(response_str);
    return response_json.value("text", "");
  }

private:
  std::string model_;
  std::string api_key_;
  std::string language_;
  std::string format_;
};
}  // namespace

std::unique_ptr<STTProvider> create_stt_provider(const STTConfig& config)
{
  const auto& provider = config.provider;
  if (provider == "whisper" || provider == "openai" || provider.empty())
  {
    return std::make_unique<WhisperSTT>(config);
  }
  if (provider == "gemini")
  {
    return make_gemini_stt(config);
  }
  if (provider == "whisper-local" || provider == "whisper-cpp" || provider == "whisper_cpp")
  {
    return make_whisper_cpp_stt(config);
  }
  throw std::runtime_error("Unknown STT provider: " + provider);
}
}  // namespace neamc::voice
