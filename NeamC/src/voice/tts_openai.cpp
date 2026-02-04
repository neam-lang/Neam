// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - OpenAI TTS provider implementation
//

#include "neamc/voice/tts.hpp"

#include <cstdlib>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "neamc/llm/http_client.hpp"

// Forward declarations for new provider factories
namespace neamc::voice
{
// Defined in tts_gemini.cpp
std::unique_ptr<TTSProvider> make_gemini_tts(const TTSConfig& config);
// Defined in tts_kokoro.cpp
std::unique_ptr<TTSProvider> make_kokoro_tts(const TTSConfig& config);
// Defined in tts_piper.cpp
std::unique_ptr<TTSProvider> make_piper_tts(const TTSConfig& config);
}  // namespace neamc::voice

namespace neamc::voice
{
namespace
{
class OpenAITTS : public TTSProvider
{
public:
  explicit OpenAITTS(const TTSConfig& config)
      : model_(config.model.empty() ? "tts-1" : config.model),
        voice_(config.voice.empty() ? "alloy" : config.voice),
        api_key_(config.api_key),
        format_(config.format),
        speed_(config.speed),
        instructions_(config.instructions)
  {
  }

  void synthesize(const std::string& text, const std::string& output_path) override
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
        throw std::runtime_error("OPENAI_API_KEY not set for OpenAI TTS");
      }
    }

    nlohmann::json payload;
    payload["model"] = model_;
    payload["input"] = text;
    payload["voice"] = voice_;
    if (!format_.empty())
    {
      payload["response_format"] = format_;
    }
    if (!speed_.empty())
    {
      payload["speed"] = std::stod(speed_);
    }
    if (!instructions_.empty())
    {
      payload["instructions"] = instructions_;
    }

    // Response is raw audio bytes (mp3), not JSON
    const std::string audio_data = llm::http_post_json(
        "https://api.openai.com/v1/audio/speech",
        payload.dump(),
        {"Content-Type: application/json", "Authorization: Bearer " + key},
        60000);

    std::ofstream out(output_path, std::ios::binary);
    if (!out)
    {
      throw std::runtime_error("Failed to open output file: " + output_path);
    }
    out.write(audio_data.data(), static_cast<std::streamsize>(audio_data.size()));
  }

private:
  std::string model_;
  std::string voice_;
  std::string api_key_;
  std::string format_;
  std::string speed_;
  std::string instructions_;
};
}  // namespace

std::unique_ptr<TTSProvider> create_tts_provider(const TTSConfig& config)
{
  const auto& provider = config.provider;
  if (provider == "openai" || provider.empty())
  {
    return std::make_unique<OpenAITTS>(config);
  }
  if (provider == "gemini")
  {
    return make_gemini_tts(config);
  }
  if (provider == "kokoro")
  {
    return make_kokoro_tts(config);
  }
  if (provider == "piper")
  {
    return make_piper_tts(config);
  }
  throw std::runtime_error("Unknown TTS provider: " + provider);
}
}  // namespace neamc::voice
