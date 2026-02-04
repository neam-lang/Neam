// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Gemini STT provider implementation
//
// Uses Gemini generateContent with inline audio data to transcribe.
//

#include "stt_gemini.hpp"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "neamc/llm/http_client.hpp"
#include "neamc/util/base64.hpp"

namespace neamc::voice
{
namespace
{
std::string detect_mime_type(const std::string& path)
{
  if (path.size() >= 4)
  {
    const std::string ext = path.substr(path.rfind('.'));
    if (ext == ".wav") return "audio/wav";
    if (ext == ".mp3") return "audio/mp3";
    if (ext == ".ogg") return "audio/ogg";
    if (ext == ".flac") return "audio/flac";
    if (ext == ".m4a") return "audio/mp4";
    if (ext == ".webm") return "audio/webm";
  }
  return "audio/wav";
}

class GeminiSTT : public STTProvider
{
public:
  explicit GeminiSTT(const STTConfig& config)
      : model_(config.model.empty() ? "gemini-2.0-flash" : config.model),
        api_key_(config.api_key),
        endpoint_(config.endpoint),
        language_(config.language)
  {
  }

  std::string transcribe(const std::string& audio_path) override
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
        throw std::runtime_error("GEMINI_API_KEY not set for Gemini STT");
      }
    }

    // Read audio file
    std::ifstream file(audio_path, std::ios::binary);
    if (!file)
    {
      throw std::runtime_error("Failed to open audio file: " + audio_path);
    }
    std::vector<uint8_t> audio_data(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    const std::string b64_audio = util::base64_encode(audio_data);
    const std::string mime_type = detect_mime_type(audio_path);

    std::string prompt = "Transcribe this audio accurately. Return only the transcription text.";
    if (!language_.empty())
    {
      prompt += " The audio is in " + language_ + ".";
    }

    nlohmann::json payload;
    payload["contents"] = nlohmann::json::array({
        {{"parts", nlohmann::json::array({
            {{"inline_data", {{"mime_type", mime_type}, {"data", b64_audio}}}},
            {{"text", prompt}}
        })}}
    });

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

    // Check for API error response
    if (response_json.contains("error"))
    {
      std::string msg = response_json["error"].value("message", "Unknown Gemini API error");
      throw std::runtime_error("Gemini STT error: " + msg);
    }

    // Validate expected structure
    if (!response_json.contains("candidates") ||
        response_json["candidates"].empty())
    {
      throw std::runtime_error("Gemini STT: no candidates in response");
    }

    const auto& parts = response_json["candidates"][0]["content"]["parts"];
    if (parts.empty() || !parts[0].contains("text"))
    {
      throw std::runtime_error("Gemini STT: no text in response");
    }
    return parts[0]["text"].get<std::string>();
  }

private:
  std::string model_;
  std::string api_key_;
  std::string endpoint_;
  std::string language_;
};
}  // namespace

std::unique_ptr<STTProvider> make_gemini_stt(const STTConfig& config)
{
  return std::make_unique<GeminiSTT>(config);
}
}  // namespace neamc::voice
