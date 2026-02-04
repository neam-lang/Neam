// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Local streaming pipeline
//
// Chunk-based STT (whisper.cpp) + token-streaming LLM (Ollama) + incremental
// TTS (Kokoro, sentence-level chunking).
//
// Phase E enhancements:
//   E.2 — Token-streaming LLM via Ollama (stream: true)
//   E.3 — Incremental TTS via Kokoro with sentence-level chunking
//   E.4 — Coordinator to stitch STT -> LLM -> TTS pipeline
//

#include "realtime_local.hpp"

#include <atomic>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "neamc/llm/http_client.hpp"
#include "neamc/util/base64.hpp"
#include "neamc/voice/audio_buffer.hpp"
#include "neamc/voice/stt.hpp"
#include "neamc/voice/tts.hpp"

namespace neamc::voice
{
namespace
{
// ---------- Sentence Splitter ----------
// Splits accumulated LLM tokens into complete sentences for TTS.
class SentenceSplitter
{
public:
  // Feed a token. Returns any complete sentences extracted so far.
  std::vector<std::string> feed(const std::string& token)
  {
    buffer_ += token;
    std::vector<std::string> sentences;

    while (true)
    {
      auto pos = find_sentence_end(buffer_);
      if (pos == std::string::npos)
      {
        break;
      }
      // Include the punctuation
      std::string sentence = buffer_.substr(0, pos + 1);
      // Trim leading whitespace
      auto first = sentence.find_first_not_of(" \t\n\r");
      if (first != std::string::npos)
      {
        sentence = sentence.substr(first);
      }
      if (!sentence.empty())
      {
        sentences.push_back(std::move(sentence));
      }
      buffer_ = buffer_.substr(pos + 1);
    }

    return sentences;
  }

  // Flush remaining text as a final sentence.
  std::string flush()
  {
    std::string remaining;
    remaining.swap(buffer_);
    auto first = remaining.find_first_not_of(" \t\n\r");
    if (first != std::string::npos)
    {
      remaining = remaining.substr(first);
    }
    return remaining;
  }

private:
  static std::size_t find_sentence_end(const std::string& text)
  {
    for (std::size_t i = 0; i < text.size(); ++i)
    {
      char c = text[i];
      if (c == '.' || c == '!' || c == '?')
      {
        // Make sure it's not an abbreviation (e.g. "Dr." or "3.14")
        // Simple heuristic: sentence end if followed by space/newline/end
        if (i + 1 >= text.size())
        {
          return i;
        }
        char next = text[i + 1];
        if (next == ' ' || next == '\n' || next == '\r' || next == '\t')
        {
          return i;
        }
      }
    }
    return std::string::npos;
  }

  std::string buffer_;
};

// ---------- Local Streaming Session ----------

class LocalStreamingSession : public RealtimeVoiceSession
{
public:
  explicit LocalStreamingSession(const RealtimeVoiceConfig& config)
      : config_(config)
      , audio_buf_(config.sample_rate > 0 ? config.sample_rate : 16000)
  {
  }

  void connect() override
  {
    connected_.store(true);
  }

  void send_audio(const std::vector<uint8_t>& pcm_data) override
  {
    audio_buf_.append(pcm_data);
  }

  void send_audio_base64(const std::string& b64_audio) override
  {
    auto decoded = util::base64_decode(b64_audio);
    send_audio(decoded);
  }

  void send_text(const std::string& text) override
  {
    // Process text through streaming LLM + incremental TTS
    process_text_streaming(text);
  }

  void commit_audio() override
  {
    // Transcribe accumulated audio, then process through streaming pipeline
    auto audio = audio_buf_.drain();
    if (audio.empty())
    {
      return;
    }

    // Write audio to temp file for STT
    std::string temp_path = "/tmp/neam_rt_audio_" +
                            std::to_string(reinterpret_cast<uintptr_t>(this)) + ".wav";
    AudioBuffer::write_wav_file(temp_path, audio,
                                audio_buf_.sample_rate(),
                                audio_buf_.channels(),
                                audio_buf_.bits_per_sample());

    // STT
    STTConfig stt_config;
    stt_config.provider = "whisper-local";
    stt_config.endpoint = config_.stt_endpoint.empty()
                              ? "http://localhost:8080"
                              : config_.stt_endpoint;
    auto stt = create_stt_provider(stt_config);

    try
    {
      std::string transcript = stt->transcribe(temp_path);
      {
        std::lock_guard<std::mutex> lock(cb_mutex_);
        if (transcript_cb_)
        {
          transcript_cb_(transcript);
        }
      }
      process_text_streaming(transcript);
    }
    catch (const std::exception& e)
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      if (error_cb_)
      {
        error_cb_(std::string("STT error: ") + e.what());
      }
    }

    std::remove(temp_path.c_str());
  }

  void clear_audio() override
  {
    audio_buf_.clear();
  }

  void request_response() override
  {
    commit_audio();
  }

  void cancel_response() override
  {
    // Cannot cancel in-flight local requests
  }

  void send_tool_result(const std::string& /*call_id*/,
                         const std::string& /*result*/) override
  {
    // Tool calls not supported in local streaming mode
  }

  void close() override
  {
    connected_.store(false);
  }

  bool is_connected() const override
  {
    return connected_.load();
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
  // ---------- E.2: Token-streaming LLM via Ollama ----------
  //
  // Uses Ollama's streaming API (stream: true) to get tokens incrementally.
  // Each line is a JSON object with the token in message.content.

  void process_text_streaming(const std::string& text)
  {
    std::string llm_endpoint = config_.llm_endpoint.empty()
                                   ? "http://localhost:11434"
                                   : config_.llm_endpoint;

    nlohmann::json payload;
    payload["model"] = config_.model.empty() ? "llama3" : config_.model;

    // Build messages with conversation history for multi-turn
    nlohmann::json messages = nlohmann::json::array();
    {
      std::lock_guard<std::mutex> lock(history_mutex_);
      for (const auto& msg : history_)
      {
        messages.push_back(msg);
      }
    }
    messages.push_back({{"role", "user"}, {"content", text}});
    payload["messages"] = messages;
    payload["stream"] = true;

    try
    {
      // Stream LLM response using chunked HTTP
      std::string full_response;
      SentenceSplitter splitter;

      stream_ollama_chat(llm_endpoint + "/api/chat", payload.dump(),
                         [&](const std::string& token)
      {
        full_response += token;

        // Deliver each token to text callback for real-time display
        {
          std::lock_guard<std::mutex> lock(cb_mutex_);
          if (text_cb_)
          {
            text_cb_(token);
          }
        }

        // E.3: Sentence-level TTS chunking
        auto sentences = splitter.feed(token);
        for (const auto& sentence : sentences)
        {
          synthesize_and_deliver(sentence);
        }
      });

      // Flush remaining text
      std::string remaining = splitter.flush();
      if (!remaining.empty())
      {
        synthesize_and_deliver(remaining);
      }

      // Track conversation history
      {
        std::lock_guard<std::mutex> lock(history_mutex_);
        history_.push_back({{"role", "user"}, {"content", text}});
        history_.push_back({{"role", "assistant"}, {"content", full_response}});

        // Keep history manageable
        if (history_.size() > 20)
        {
          history_.erase(history_.begin(), history_.begin() + 2);
        }
      }
    }
    catch (const std::exception& e)
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      if (error_cb_)
      {
        error_cb_(std::string("Local pipeline error: ") + e.what());
      }
    }
  }

  // Stream Ollama chat API, calling token_cb for each token received.
  static void stream_ollama_chat(const std::string& url,
                                 const std::string& body,
                                 std::function<void(const std::string&)> token_cb)
  {
    // Use libcurl for streaming
    CURL* curl = curl_easy_init();
    if (!curl)
    {
      throw std::runtime_error("Failed to init curl for streaming");
    }

    struct StreamCtx
    {
      std::function<void(const std::string&)> cb;
      std::string line_buffer;
    };
    StreamCtx ctx;
    ctx.cb = std::move(token_cb);

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION,
                     +[](char* ptr, size_t size, size_t nmemb, void* userdata) -> size_t
    {
      auto* ctx = static_cast<StreamCtx*>(userdata);
      std::size_t total = size * nmemb;
      ctx->line_buffer.append(ptr, total);

      // Process complete lines (Ollama sends one JSON per line)
      std::size_t pos;
      while ((pos = ctx->line_buffer.find('\n')) != std::string::npos)
      {
        std::string line = ctx->line_buffer.substr(0, pos);
        ctx->line_buffer.erase(0, pos + 1);

        if (line.empty())
        {
          continue;
        }

        try
        {
          auto json = nlohmann::json::parse(line);
          if (json.contains("message") && json["message"].contains("content"))
          {
            std::string token = json["message"]["content"].get<std::string>();
            if (!token.empty())
            {
              ctx->cb(token);
            }
          }
        }
        catch (...)
        {
          // Ignore malformed lines
        }
      }
      return total;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &ctx);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

    CURLcode res = curl_easy_perform(curl);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK)
    {
      throw std::runtime_error(std::string("Ollama streaming failed: ") +
                               curl_easy_strerror(res));
    }

    // Process any remaining data in the line buffer
    if (!ctx.line_buffer.empty())
    {
      try
      {
        auto json = nlohmann::json::parse(ctx.line_buffer);
        if (json.contains("message") && json["message"].contains("content"))
        {
          std::string token = json["message"]["content"].get<std::string>();
          if (!token.empty())
          {
            ctx.cb(token);
          }
        }
      }
      catch (...)
      {
      }
    }
  }

  // ---------- E.3: Incremental TTS via Kokoro ----------

  void synthesize_and_deliver(const std::string& sentence)
  {
    TTSConfig tts_config;
    tts_config.provider = "kokoro";
    tts_config.endpoint = config_.tts_endpoint.empty()
                              ? "http://localhost:8880"
                              : config_.tts_endpoint;
    tts_config.voice = config_.voice.empty() ? "af_heart" : config_.voice;

    std::string audio_path = "/tmp/neam_rt_tts_" +
                             std::to_string(reinterpret_cast<uintptr_t>(this)) +
                             "_" + std::to_string(tts_seq_++) + ".wav";

    try
    {
      auto tts = create_tts_provider(tts_config);
      tts->synthesize(sentence, audio_path);

      // Read audio and deliver via callback
      std::ifstream audio_file(audio_path, std::ios::binary);
      if (audio_file)
      {
        std::vector<uint8_t> audio_data(
            (std::istreambuf_iterator<char>(audio_file)),
            std::istreambuf_iterator<char>());
        {
          std::lock_guard<std::mutex> lock(cb_mutex_);
          if (audio_cb_)
          {
            audio_cb_(audio_data);
          }
        }
      }
      std::remove(audio_path.c_str());
    }
    catch (const std::exception& e)
    {
      std::lock_guard<std::mutex> lock(cb_mutex_);
      if (error_cb_)
      {
        error_cb_(std::string("TTS error: ") + e.what());
      }
    }
  }

  RealtimeVoiceConfig config_;
  std::atomic<bool> connected_{false};
  AudioBuffer audio_buf_;
  std::mutex cb_mutex_;
  TranscriptCallback transcript_cb_;
  AudioCallback audio_cb_;
  TextCallback text_cb_;
  ToolCallCallback tool_call_cb_;
  ErrorCallback error_cb_;

  // Conversation history for multi-turn
  std::mutex history_mutex_;
  std::vector<nlohmann::json> history_;

  // TTS sequence number for temp file names
  std::atomic<int> tts_seq_{0};
};
}  // namespace

std::unique_ptr<RealtimeVoiceSession> make_local_realtime(
    const RealtimeVoiceConfig& config)
{
  return std::make_unique<LocalStreamingSession>(config);
}
}  // namespace neamc::voice
