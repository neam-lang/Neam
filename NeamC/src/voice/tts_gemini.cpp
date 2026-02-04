// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Gemini TTS provider implementation
//
// Uses Gemini generateContent with response_modalities: ["AUDIO"]
// Response is base64 PCM 24kHz 16-bit mono. Decoded and written as WAV.
//

#include "tts_gemini.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "neamc/llm/http_client.hpp"
#include "neamc/util/base64.hpp"

namespace neamc::voice
{
namespace
{
void write_wav_header(std::ofstream& out, uint32_t data_size,
                      uint32_t sample_rate, uint16_t bits_per_sample, uint16_t channels)
{
  const uint32_t byte_rate = sample_rate * channels * bits_per_sample / 8;
  const uint16_t block_align = channels * bits_per_sample / 8;
  const uint32_t chunk_size = 36 + data_size;

  auto write_u32 = [&](uint32_t v) { out.write(reinterpret_cast<const char*>(&v), 4); };
  auto write_u16 = [&](uint16_t v) { out.write(reinterpret_cast<const char*>(&v), 2); };

  out.write("RIFF", 4);
  write_u32(chunk_size);
  out.write("WAVE", 4);
  out.write("fmt ", 4);
  write_u32(16);  // subchunk1 size
  write_u16(1);   // PCM format
  write_u16(channels);
  write_u32(sample_rate);
  write_u32(byte_rate);
  write_u16(block_align);
  write_u16(bits_per_sample);
  out.write("data", 4);
  write_u32(data_size);
}

class GeminiTTS : public TTSProvider
{
public:
  explicit GeminiTTS(const TTSConfig& config)
      : model_(config.model.empty() ? "gemini-2.5-flash-preview-tts" : config.model),
        voice_(config.voice.empty() ? "Kore" : config.voice),
        api_key_(config.api_key),
        endpoint_(config.endpoint),
        speed_(config.speed),
        instructions_(config.instructions)
  {
  }

  void synthesize(const std::string& text, const std::string& output_path) override
  {
    std::string key = api_key_;
    if (key.empty())
    {
      if (const char* env_val = std::getenv("GEMINI_API_KEY"))
      {
        key = env_val;
      }
      else
      {
        throw std::runtime_error("GEMINI_API_KEY not set for Gemini TTS");
      }
    }

    std::string prompt = text;
    if (!instructions_.empty())
    {
      prompt = instructions_ + "\n\n" + text;
    }

    nlohmann::json payload;
    payload["contents"] = nlohmann::json::array({
        {{"parts", nlohmann::json::array({
            {{"text", prompt}}
        })}}
    });
    payload["generationConfig"] = {
        {"response_modalities", nlohmann::json::array({"AUDIO"})},
        {"speech_config", {
            {"voice_config", {
                {"prebuilt_voice_config", {{"voice_name", voice_}}}
            }}
        }}
    };

    std::string base_url = endpoint_;
    if (base_url.empty())
    {
      base_url = "https://generativelanguage.googleapis.com";
    }
    const std::string url = base_url + "/v1beta/models/" + model_ +
                            ":generateContent?key=" + key;

    const std::string response_str = llm::http_post_json(
        url, payload.dump(),
        {"Content-Type: application/json"},
        120000);

    const auto response_json = nlohmann::json::parse(response_str);

    // Extract base64 audio data from response
    const auto& parts = response_json["candidates"][0]["content"]["parts"];
    std::string b64_audio;
    for (const auto& part : parts)
    {
      if (part.contains("inlineData"))
      {
        b64_audio = part["inlineData"]["data"].get<std::string>();
        break;
      }
    }

    if (b64_audio.empty())
    {
      throw std::runtime_error("Gemini TTS response contained no audio data");
    }

    // Decode base64 PCM data
    std::vector<uint8_t> pcm_data = util::base64_decode(b64_audio);

    // Write WAV file (24kHz, 16-bit, mono)
    std::ofstream out(output_path, std::ios::binary);
    if (!out)
    {
      throw std::runtime_error("Failed to open output file: " + output_path);
    }
    write_wav_header(out, static_cast<uint32_t>(pcm_data.size()), 24000, 16, 1);
    out.write(reinterpret_cast<const char*>(pcm_data.data()),
              static_cast<std::streamsize>(pcm_data.size()));
  }

private:
  std::string model_;
  std::string voice_;
  std::string api_key_;
  std::string endpoint_;
  std::string speed_;
  std::string instructions_;
};
}  // namespace

std::unique_ptr<TTSProvider> make_gemini_tts(const TTSConfig& config)
{
  return std::make_unique<GeminiTTS>(config);
}
}  // namespace neamc::voice
