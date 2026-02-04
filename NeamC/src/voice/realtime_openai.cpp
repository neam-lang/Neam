// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - OpenAI Realtime API provider
//
// WebSocket-based full-duplex voice streaming via
// wss://api.openai.com/v1/realtime?model=<MODEL_ID>
//

#include "realtime_openai.hpp"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "neamc/llm/websocket_client.hpp"
#include "neamc/util/base64.hpp"

namespace neamc::voice
{
namespace
{
class OpenAIRealtimeSession : public RealtimeVoiceSession
{
public:
  explicit OpenAIRealtimeSession(const RealtimeVoiceConfig& config)
      : config_(config)
  {
  }

  void connect() override
  {
    std::string key = config_.api_key;
    if (key.empty())
    {
      if (const char* env_val = std::getenv("OPENAI_API_KEY"))
      {
        key = env_val;
      }
      else
      {
        throw std::runtime_error("OPENAI_API_KEY not set for OpenAI Realtime");
      }
    }

    std::string model = config_.model.empty() ? "gpt-4o-realtime-preview" : config_.model;
    std::string url = "wss://api.openai.com/v1/realtime?model=" + model;

    ws_.set_on_message([this](const std::string& msg) { handle_message(msg); });
    ws_.set_on_error([this](const std::string& err)
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      if (error_cb_)
      {
        error_cb_(err);
      }
    });

    ws_.connect(url, {
        "Authorization: Bearer " + key,
        "OpenAI-Beta: realtime=v1"
    });

    // Send session.update with config
    nlohmann::json session_update;
    session_update["type"] = "session.update";
    session_update["session"]["voice"] = config_.voice.empty() ? "alloy" : config_.voice;

    if (!config_.modalities.empty())
    {
      session_update["session"]["modalities"] = config_.modalities;
    }
    else
    {
      session_update["session"]["modalities"] = nlohmann::json::array({"text", "audio"});
    }

    session_update["session"]["input_audio_format"] =
        config_.input_format.empty() ? "pcm16" : config_.input_format;
    session_update["session"]["output_audio_format"] =
        config_.output_format.empty() ? "pcm16" : config_.output_format;

    // VAD configuration
    if (config_.vad != "manual" && config_.vad != "client")
    {
      nlohmann::json vad;
      vad["type"] = "server_vad";
      vad["threshold"] = config_.vad_threshold;
      vad["silence_duration_ms"] = config_.silence_duration_ms;
      vad["prefix_padding_ms"] = 300;
      session_update["session"]["turn_detection"] = vad;
    }
    else
    {
      session_update["session"]["turn_detection"] = nullptr;
    }

    ws_.send_text(session_update.dump());

    // Wait for session.created or session.updated with timeout
    {
      std::unique_lock<std::mutex> lock(session_ready_mutex_);
      if (!session_ready_cv_.wait_for(lock, std::chrono::seconds(15),
                                       [this] { return session_ready_; }))
      {
        ws_.close();
        throw std::runtime_error(
            "OpenAI Realtime: timed out waiting for session confirmation");
      }
    }
  }

  void send_audio(const std::vector<uint8_t>& pcm_data) override
  {
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
    nlohmann::json event;
    event["type"] = "input_audio_buffer.append";
    event["audio"] = b64_audio;
    ws_.send_text(event.dump());
  }

  void send_text(const std::string& text) override
  {
    nlohmann::json event;
    event["type"] = "conversation.item.create";
    event["item"]["type"] = "message";
    event["item"]["role"] = "user";
    event["item"]["content"] = nlohmann::json::array({
        {{"type", "input_text"}, {"text", text}}
    });
    ws_.send_text(event.dump());

    // Trigger response
    request_response();
  }

  void commit_audio() override
  {
    nlohmann::json event;
    event["type"] = "input_audio_buffer.commit";
    ws_.send_text(event.dump());
  }

  void clear_audio() override
  {
    nlohmann::json event;
    event["type"] = "input_audio_buffer.clear";
    ws_.send_text(event.dump());
  }

  void request_response() override
  {
    nlohmann::json event;
    event["type"] = "response.create";
    ws_.send_text(event.dump());
  }

  void cancel_response() override
  {
    nlohmann::json event;
    event["type"] = "response.cancel";
    ws_.send_text(event.dump());
  }

  void send_tool_result(const std::string& call_id,
                         const std::string& result) override
  {
    nlohmann::json event;
    event["type"] = "conversation.item.create";
    event["item"]["type"] = "function_call_output";
    event["item"]["call_id"] = call_id;
    event["item"]["output"] = result;
    ws_.send_text(event.dump());

    request_response();
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
  void handle_message(const std::string& msg)
  {
    try
    {
      auto json = nlohmann::json::parse(msg);
      const std::string type = json.value("type", "");

      if (type == "session.created" || type == "session.updated")
      {
        std::lock_guard<std::mutex> lock(session_ready_mutex_);
        session_ready_ = true;
        session_ready_cv_.notify_one();
        return;
      }

      if (type == "response.audio.delta")
      {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        if (audio_cb_)
        {
          std::string b64 = json["delta"].get<std::string>();
          auto pcm = util::base64_decode(b64);
          audio_cb_(pcm);
        }
      }
      else if (type == "response.audio_transcript.delta")
      {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        if (text_cb_)
        {
          text_cb_(json["delta"].get<std::string>());
        }
      }
      else if (type == "conversation.item.input_audio_transcription.completed")
      {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        if (transcript_cb_)
        {
          transcript_cb_(json["transcript"].get<std::string>());
        }
      }
      else if (type == "response.function_call_arguments.done")
      {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        if (tool_call_cb_)
        {
          tool_call_cb_(
              json["call_id"].get<std::string>(),
              json["name"].get<std::string>(),
              json["arguments"].get<std::string>());
        }
      }
      else if (type == "error")
      {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        if (error_cb_)
        {
          std::string err_msg = json.value("error", nlohmann::json::object())
                                     .value("message", "Unknown error");
          error_cb_(err_msg);
        }
      }
    }
    catch (const std::exception& e)
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      if (error_cb_)
      {
        error_cb_(std::string("Failed to parse server event: ") + e.what());
      }
    }
  }

  RealtimeVoiceConfig config_;
  llm::WebSocketClient ws_;
  std::mutex cb_mutex_;
  TranscriptCallback transcript_cb_;
  AudioCallback audio_cb_;
  TextCallback text_cb_;
  ToolCallCallback tool_call_cb_;
  ErrorCallback error_cb_;

  // Session readiness signaling
  std::condition_variable session_ready_cv_;
  std::mutex session_ready_mutex_;
  bool session_ready_ = false;
};
}  // namespace

std::unique_ptr<RealtimeVoiceSession> make_openai_realtime(
    const RealtimeVoiceConfig& config)
{
  return std::make_unique<OpenAIRealtimeSession>(config);
}
}  // namespace neamc::voice
