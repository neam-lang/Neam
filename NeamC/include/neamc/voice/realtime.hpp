// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Real-time streaming voice provider interface
//

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace neamc::voice
{
struct RealtimeVoiceConfig
{
  std::string provider;       // "openai" | "gemini" | "local"
  std::string model;          // e.g. "gpt-realtime-mini"
  std::string voice;          // output voice
  std::string api_key;
  std::string vad;            // "server" | "client" | "manual"
  double vad_threshold = 0.5;
  int silence_duration_ms = 500;
  std::string input_format;   // "pcm16"
  std::string output_format;  // "pcm16" | "g711_ulaw" | "g711_alaw"
  int sample_rate = 24000;
  std::vector<std::string> modalities;  // ["text", "audio"]
  double speed = 1.0;
  // Local streaming config
  std::string stt_endpoint;
  std::string tts_endpoint;
  std::string llm_endpoint;
};

class RealtimeVoiceSession
{
public:
  using TranscriptCallback = std::function<void(const std::string& text)>;
  using AudioCallback = std::function<void(const std::vector<uint8_t>& audio)>;
  using TextCallback = std::function<void(const std::string& text)>;
  using ToolCallCallback = std::function<void(const std::string& call_id,
                                               const std::string& name,
                                               const std::string& args)>;
  using ErrorCallback = std::function<void(const std::string& error)>;

  virtual ~RealtimeVoiceSession() = default;

  virtual void connect() = 0;
  virtual void send_audio(const std::vector<uint8_t>& pcm_data) = 0;
  virtual void send_audio_base64(const std::string& b64_audio) = 0;
  virtual void send_text(const std::string& text) = 0;
  virtual void commit_audio() = 0;
  virtual void clear_audio() = 0;
  virtual void request_response() = 0;
  virtual void cancel_response() = 0;
  virtual void send_tool_result(const std::string& call_id,
                                 const std::string& result) = 0;
  virtual void close() = 0;
  virtual bool is_connected() const = 0;

  virtual void on_transcript(TranscriptCallback cb) = 0;
  virtual void on_response_audio(AudioCallback cb) = 0;
  virtual void on_response_text(TextCallback cb) = 0;
  virtual void on_tool_call(ToolCallCallback cb) = 0;
  virtual void on_error(ErrorCallback cb) = 0;
};

std::unique_ptr<RealtimeVoiceSession> create_realtime_session(
    const RealtimeVoiceConfig& config);
}  // namespace neamc::voice
