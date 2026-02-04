// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Kokoro TTS provider implementation
//
// OpenAI-compatible /v1/audio/speech at configurable endpoint.
//

#include "tts_kokoro.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "neamc/llm/http_client.hpp"

namespace neamc::voice
{
namespace
{
class KokoroTTS : public TTSProvider
{
public:
  explicit KokoroTTS(const TTSConfig& config)
      : model_(config.model.empty() ? "kokoro" : config.model),
        voice_(config.voice.empty() ? "af_heart" : config.voice),
        endpoint_(config.endpoint.empty() ? "http://localhost:8880" : config.endpoint),
        format_(config.format),
        speed_(config.speed)
  {
  }

  void synthesize(const std::string& text, const std::string& output_path) override
  {
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

    const std::string url = endpoint_ + "/v1/audio/speech";
    const std::string audio_data = llm::http_post_json(
        url, payload.dump(),
        {"Content-Type: application/json"},
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
  std::string endpoint_;
  std::string format_;
  std::string speed_;
};
}  // namespace

std::unique_ptr<TTSProvider> make_kokoro_tts(const TTSConfig& config)
{
  return std::make_unique<KokoroTTS>(config);
}
}  // namespace neamc::voice
