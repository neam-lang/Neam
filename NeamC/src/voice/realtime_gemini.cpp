// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Gemini Live API provider
//
// WebSocket-based full-duplex voice streaming via
// wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent
//
// Features:
//   - Session resumption via handle tokens (~10 min sessions)
//   - Context window compression (summarize old turns to stay within limits)
//

#include "realtime_gemini.hpp"

#include <chrono>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/llm/websocket_client.hpp"
#include "neamc/util/base64.hpp"

namespace neamc::voice
{
namespace
{
class GeminiLiveSession : public RealtimeVoiceSession
{
public:
  explicit GeminiLiveSession(const RealtimeVoiceConfig& config)
      : config_(config)
  {
  }

  void connect() override
  {
    std::string key = config_.api_key;
    if (key.empty())
    {
      if (const char* env_val = std::getenv("GEMINI_API_KEY"))
      {
        key = env_val;
      }
      else
      {
        throw std::runtime_error("GEMINI_API_KEY not set for Gemini Live");
      }
    }
    api_key_ = key;

    std::string url =
        "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage."
        "v1beta.GenerativeService.BidiGenerateContent?key=" + key;

    ws_.set_on_message([this](const std::string& msg) { handle_message(msg); });
    ws_.set_on_error([this](const std::string& err)
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      if (error_cb_)
      {
        error_cb_(err);
      }
    });
    ws_.set_on_close([this](int, const std::string&)
    {
      // Session closed — if we have a handle token, we can resume later.
    });

    ws_.connect(url, {});
    session_start_ = std::chrono::steady_clock::now();

    send_setup_message();
  }

  void send_audio(const std::vector<uint8_t>& pcm_data) override
  {
    check_session_expiry();
    // Strip WAV header if present (starts with "RIFF")
    const uint8_t* data = pcm_data.data();
    size_t len = pcm_data.size();
    if (len > 44 &&
        data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F')
    {
      data += 44;
      len -= 44;
    }
    std::string b64 = util::base64_encode(std::vector<uint8_t>(data, data + len));
    send_audio_base64(b64);
  }

  void send_audio_base64(const std::string& b64_audio) override
  {
    check_session_expiry();
    nlohmann::json event;
    event["realtimeInput"]["mediaChunks"] = nlohmann::json::array({
        {{"mimeType", "audio/pcm;rate=16000"}, {"data", b64_audio}}
    });
    ws_.send_text(event.dump());
  }

  void send_text(const std::string& text) override
  {
    check_session_expiry();

    // Track turns for context compression
    {
      std::lock_guard<std::mutex> lock(context_mutex_);
      Turn t;
      t.role = "user";
      t.content = text;
      turns_.push_back(std::move(t));
      compress_context_if_needed();
    }

    nlohmann::json event;
    event["clientContent"]["turns"] = nlohmann::json::array({
        {{"role", "user"},
         {"parts", nlohmann::json::array({{{"text", text}}})}}
    });
    event["clientContent"]["turnComplete"] = true;
    ws_.send_text(event.dump());
  }

  void commit_audio() override
  {
    // Gemini auto-detects end of speech via activity detection.
  }

  void clear_audio() override
  {
    // Not directly supported by Gemini Live
  }

  void request_response() override
  {
    // Gemini auto-responds after turn detection
  }

  void cancel_response() override
  {
    // Interruption in Gemini happens via sending new audio (barge-in)
  }

  void send_tool_result(const std::string& call_id,
                         const std::string& result) override
  {
    nlohmann::json event;
    event["toolResponse"]["functionResponses"] = nlohmann::json::array({
        {{"id", call_id}, {"response", nlohmann::json::parse(result)}}
    });
    ws_.send_text(event.dump());
  }

  void close() override
  {
    ws_.close();
  }

  bool is_connected() const override
  {
    return ws_.is_connected();
  }

  void on_transcript(TranscriptCallback cb) override
  {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    transcript_cb_ = std::move(cb);
  }

  void on_response_audio(AudioCallback cb) override
  {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    audio_cb_ = std::move(cb);
  }

  void on_response_text(TextCallback cb) override
  {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    text_cb_ = std::move(cb);
  }

  void on_tool_call(ToolCallCallback cb) override
  {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    tool_call_cb_ = std::move(cb);
  }

  void on_error(ErrorCallback cb) override
  {
    std::lock_guard<std::mutex> lock(cb_mutex_);
    error_cb_ = std::move(cb);
  }

private:
  // ---------- Session Resumption ----------

  // Gemini Live sessions last ~10 minutes. Before expiry, the server sends
  // a "sessionResumptionUpdate" with a handle token.  We store it so
  // check_session_expiry() can transparently reconnect.

  void check_session_expiry()
  {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - session_start_).count();

    // Proactively reconnect at 9 minutes to avoid cut-off at ~10 min.
    constexpr int kReconnectThresholdSec = 9 * 60;
    if (elapsed >= kReconnectThresholdSec && !session_handle_.empty())
    {
      reconnect_with_handle();
    }
  }

  void reconnect_with_handle()
  {
    // Close existing connection
    ws_.close();

    std::string url =
        "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage."
        "v1beta.GenerativeService.BidiGenerateContent?key=" + api_key_;

    ws_.set_on_message([this](const std::string& msg) { handle_message(msg); });
    ws_.set_on_error([this](const std::string& err)
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      if (error_cb_)
      {
        error_cb_(err);
      }
    });

    ws_.connect(url, {});
    session_start_ = std::chrono::steady_clock::now();

    // Send setup with session handle for resumption
    send_setup_message();
  }

  void send_setup_message()
  {
    std::string model = config_.model.empty()
                            ? "models/gemini-2.0-flash-live-001"
                            : "models/" + config_.model;

    nlohmann::json setup;
    setup["setup"]["model"] = model;

    // Generation config
    nlohmann::json gen_config;
    gen_config["response_modalities"] = nlohmann::json::array({"AUDIO"});

    // Voice config
    std::string voice = config_.voice.empty() ? "Puck" : config_.voice;
    gen_config["speech_config"]["voice_config"]["prebuilt_voice_config"]["voice_name"] = voice;

    setup["setup"]["generationConfig"] = gen_config;

    // VAD / activity detection
    if (config_.vad != "manual")
    {
      nlohmann::json input_config;
      nlohmann::json activity;
      activity["startOfSpeechSensitivity"] = "START_SENSITIVITY_HIGH";
      activity["endOfSpeechSensitivity"] = "END_SENSITIVITY_HIGH";
      activity["prefixPaddingMs"] = 300;
      activity["silenceDurationMs"] = config_.silence_duration_ms;
      input_config["automaticActivityDetection"] = activity;
      setup["setup"]["realtimeInputConfig"] = input_config;
    }

    // Session resumption: include handle token if we have one
    if (!session_handle_.empty())
    {
      setup["setup"]["sessionResumption"]["handle"] = session_handle_;
    }
    else
    {
      // Enable session resumption for new sessions
      setup["setup"]["sessionResumption"]["transparent"] = true;
    }

    ws_.send_text(setup.dump());

    // If resuming, replay compressed context so the model remembers
    if (!session_handle_.empty())
    {
      replay_context();
    }
  }

  // ---------- Context Window Compression ----------

  struct Turn
  {
    std::string role;
    std::string content;
  };

  // When turns exceed the threshold, compress older turns into a summary.
  static constexpr std::size_t kMaxTurnsBeforeCompression = 20;
  static constexpr std::size_t kKeepRecentTurns = 6;

  void compress_context_if_needed()
  {
    // Called with context_mutex_ held
    if (turns_.size() <= kMaxTurnsBeforeCompression)
    {
      return;
    }

    // Build a summary of older turns
    std::size_t compress_count = turns_.size() - kKeepRecentTurns;
    std::string summary = "[Context summary: ";

    for (std::size_t i = 0; i < compress_count; ++i)
    {
      summary += turns_[i].role + " said: \"";
      // Truncate long turns
      if (turns_[i].content.size() > 100)
      {
        summary += turns_[i].content.substr(0, 100) + "...";
      }
      else
      {
        summary += turns_[i].content;
      }
      summary += "\"; ";
    }
    summary += "]";

    // Replace compressed turns with single summary turn
    std::vector<Turn> new_turns;
    Turn summary_turn;
    summary_turn.role = "user";
    summary_turn.content = summary;
    new_turns.push_back(std::move(summary_turn));

    for (std::size_t i = compress_count; i < turns_.size(); ++i)
    {
      new_turns.push_back(std::move(turns_[i]));
    }
    turns_ = std::move(new_turns);
  }

  void replay_context()
  {
    // Send compressed context as clientContent so the model picks up where
    // we left off after session resumption.
    std::lock_guard<std::mutex> lock(context_mutex_);
    if (turns_.empty())
    {
      return;
    }

    nlohmann::json turns_json = nlohmann::json::array();
    for (const auto& t : turns_)
    {
      nlohmann::json turn;
      turn["role"] = t.role;
      turn["parts"] = nlohmann::json::array({{{"text", t.content}}});
      turns_json.push_back(turn);
    }

    nlohmann::json event;
    event["clientContent"]["turns"] = turns_json;
    event["clientContent"]["turnComplete"] = true;
    ws_.send_text(event.dump());
  }

  // ---------- Message Handling ----------

  void handle_message(const std::string& msg)
  {
    try
    {
      auto json = nlohmann::json::parse(msg);

      // Session resumption update — store the handle token
      if (json.contains("sessionResumptionUpdate"))
      {
        const auto& update = json["sessionResumptionUpdate"];
        if (update.contains("newHandle"))
        {
          session_handle_ = update["newHandle"].get<std::string>();
        }
        return;
      }

      if (json.contains("serverContent"))
      {
        const auto& content = json["serverContent"];

        if (content.contains("modelTurn"))
        {
          const auto& parts = content["modelTurn"]["parts"];
          for (const auto& part : parts)
          {
            if (part.contains("inlineData"))
            {
              std::lock_guard<std::mutex> lock(cb_mutex_);
              if (audio_cb_)
              {
                std::string b64 = part["inlineData"]["data"].get<std::string>();
                auto pcm = util::base64_decode(b64);
                audio_cb_(pcm);
              }
            }
            else if (part.contains("text"))
            {
              std::string text = part["text"].get<std::string>();
              // Track model responses for context compression
              {
                std::lock_guard<std::mutex> lock2(context_mutex_);
                Turn t;
                t.role = "model";
                t.content = text;
                turns_.push_back(std::move(t));
              }
              std::lock_guard<std::mutex> lock(cb_mutex_);
              if (text_cb_)
              {
                text_cb_(text);
              }
            }
          }
        }

        // Input transcription
        if (content.contains("inputTranscription"))
        {
          std::lock_guard<std::mutex> lock(cb_mutex_);
          if (transcript_cb_)
          {
            transcript_cb_(
                content["inputTranscription"]["text"].get<std::string>());
          }
        }
      }
      else if (json.contains("toolCall"))
      {
        const auto& calls = json["toolCall"]["functionCalls"];
        for (const auto& call : calls)
        {
          std::lock_guard<std::mutex> lock(cb_mutex_);
          if (tool_call_cb_)
          {
            tool_call_cb_(
                call["id"].get<std::string>(),
                call["name"].get<std::string>(),
                call.value("args", nlohmann::json::object()).dump());
          }
        }
      }
    }
    catch (const std::exception& e)
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      if (error_cb_)
      {
        error_cb_(std::string("Failed to parse Gemini event: ") + e.what());
      }
    }
  }

  RealtimeVoiceConfig config_;
  std::string api_key_;
  llm::WebSocketClient ws_;
  std::mutex cb_mutex_;
  TranscriptCallback transcript_cb_;
  AudioCallback audio_cb_;
  TextCallback text_cb_;
  ToolCallCallback tool_call_cb_;
  ErrorCallback error_cb_;

  // Session resumption
  std::string session_handle_;
  std::chrono::steady_clock::time_point session_start_;

  // Context tracking for compression
  std::mutex context_mutex_;
  std::vector<Turn> turns_;
};
}  // namespace

std::unique_ptr<RealtimeVoiceSession> make_gemini_realtime(
    const RealtimeVoiceConfig& config)
{
  return std::make_unique<GeminiLiveSession>(config);
}
}  // namespace neamc::voice
