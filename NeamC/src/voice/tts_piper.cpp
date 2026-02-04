// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Piper TTS provider implementation
//
// POST {"text":"..."} to {endpoint}/tts. Response is raw WAV.
//

#include "tts_piper.hpp"

#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "neamc/llm/http_client.hpp"

namespace neamc::voice
{
namespace
{
class PiperTTS : public TTSProvider
{
public:
  explicit PiperTTS(const TTSConfig& config)
      : endpoint_(config.endpoint.empty() ? "http://localhost:5000" : config.endpoint)
  {
  }

  void synthesize(const std::string& text, const std::string& output_path) override
  {
    nlohmann::json payload;
    payload["text"] = text;

    const std::string url = endpoint_ + "/tts";
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
  std::string endpoint_;
};
}  // namespace

std::unique_ptr<TTSProvider> make_piper_tts(const TTSConfig& config)
{
  return std::make_unique<PiperTTS>(config);
}
}  // namespace neamc::voice
