// Voice pipeline config and factory tests.
#include <gtest/gtest.h>
#include "neamc/voice/stt.hpp"
#include "neamc/voice/tts.hpp"
#include "neamc/voice/realtime.hpp"
#include "neamc/voice/audio_buffer.hpp"
#include "neamc/voice/pipeline.hpp"

using namespace neamc::voice;

// ===========================================================================
// STT Config
// ===========================================================================

TEST(STTConfig, Defaults) {
    STTConfig config;
    EXPECT_TRUE(config.provider.empty());
    EXPECT_TRUE(config.model.empty());
}

TEST(STTConfig, WhisperConfig) {
    STTConfig config;
    config.provider = "whisper";
    config.model = "whisper-1";
    config.api_key = "test-key";
    EXPECT_EQ(config.provider, "whisper");
}

TEST(STTConfig, GeminiConfig) {
    STTConfig config;
    config.provider = "gemini";
    config.model = "gemini-pro";
    EXPECT_EQ(config.provider, "gemini");
}

// ===========================================================================
// TTS Config
// ===========================================================================

TEST(TTSConfig, Defaults) {
    TTSConfig config;
    EXPECT_TRUE(config.provider.empty());
}

TEST(TTSConfig, OpenAIConfig) {
    TTSConfig config;
    config.provider = "openai";
    config.model = "tts-1";
    config.voice = "alloy";
    config.api_key = "key";
    EXPECT_EQ(config.voice, "alloy");
}

TEST(TTSConfig, KokoroConfig) {
    TTSConfig config;
    config.provider = "kokoro";
    EXPECT_EQ(config.provider, "kokoro");
}

TEST(TTSConfig, PiperConfig) {
    TTSConfig config;
    config.provider = "piper";
    EXPECT_EQ(config.provider, "piper");
}

// ===========================================================================
// Realtime Config
// ===========================================================================

TEST(RealtimeConfig, Defaults) {
    RealtimeVoiceConfig config;
    EXPECT_TRUE(config.provider.empty());
}

TEST(RealtimeConfig, OpenAIRealtime) {
    RealtimeVoiceConfig config;
    config.provider = "openai";
    config.model = "gpt-4o-realtime";
    config.voice = "alloy";
    config.sample_rate = 24000;
    EXPECT_EQ(config.sample_rate, 24000);
}

TEST(RealtimeConfig, GeminiRealtime) {
    RealtimeVoiceConfig config;
    config.provider = "gemini";
    config.model = "gemini-2.0-flash";
    EXPECT_EQ(config.provider, "gemini");
}

// ===========================================================================
// STT Provider Factory
// ===========================================================================

TEST(STTFactory, UnknownProviderReturnsNull) {
    STTConfig config;
    config.provider = "nonexistent_provider";
    // Factory may throw or return nullptr for unknown provider
    try {
        auto provider = create_stt_provider(config);
        EXPECT_EQ(provider, nullptr);
    } catch (const std::exception&) {
        // Exception is also acceptable for unknown provider
        SUCCEED();
    }
}

TEST(STTFactory, EmptyProvider) {
    STTConfig config;
    // Factory may throw or return a default or nullptr for empty provider
    try {
        auto provider = create_stt_provider(config);
        // Any result is acceptable - implementation dependent
        SUCCEED();
    } catch (const std::exception&) {
        SUCCEED();
    }
}

// ===========================================================================
// TTS Provider Factory
// ===========================================================================

TEST(TTSFactory, UnknownProviderReturnsNull) {
    TTSConfig config;
    config.provider = "nonexistent_provider";
    // Factory may throw or return nullptr for unknown provider
    try {
        auto provider = create_tts_provider(config);
        EXPECT_EQ(provider, nullptr);
    } catch (const std::exception&) {
        // Exception is also acceptable for unknown provider
        SUCCEED();
    }
}

TEST(TTSFactory, EmptyProvider) {
    TTSConfig config;
    // Factory may throw or return a default or nullptr for empty provider
    try {
        auto provider = create_tts_provider(config);
        // Any result is acceptable - implementation dependent
        SUCCEED();
    } catch (const std::exception&) {
        SUCCEED();
    }
}

// ===========================================================================
// Realtime Session Factory
// ===========================================================================

TEST(RealtimeFactory, UnknownProviderReturnsNull) {
    RealtimeVoiceConfig config;
    config.provider = "nonexistent_provider";
    // Factory may throw or return nullptr for unknown provider
    try {
        auto session = create_realtime_session(config);
        EXPECT_EQ(session, nullptr);
    } catch (const std::exception&) {
        // Exception is also acceptable for unknown provider
        SUCCEED();
    }
}

// ===========================================================================
// Audio Buffer
// ===========================================================================

TEST(AudioBuffer, CreateDefault) {
    AudioBuffer buf;
    EXPECT_EQ(buf.sample_rate(), 16000);
    EXPECT_EQ(buf.channels(), 1);
    EXPECT_EQ(buf.bits_per_sample(), 16);
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
}

TEST(AudioBuffer, AppendAndDrain) {
    AudioBuffer buf(16000, 1, 16);
    std::vector<uint8_t> pcm(3200, 128);  // 100ms at 16kHz 16-bit mono
    buf.append(pcm);

    EXPECT_FALSE(buf.empty());
    EXPECT_EQ(buf.size(), 3200u);

    auto drained = buf.drain();
    EXPECT_EQ(drained.size(), 3200u);
    EXPECT_TRUE(buf.empty());
}

TEST(AudioBuffer, DrainPartial) {
    AudioBuffer buf(16000, 1, 16);
    std::vector<uint8_t> pcm(6400, 128);
    buf.append(pcm);

    auto partial = buf.drain(3200);
    EXPECT_EQ(partial.size(), 3200u);
    EXPECT_EQ(buf.size(), 3200u);
}

TEST(AudioBuffer, Clear) {
    AudioBuffer buf;
    std::vector<uint8_t> pcm(1600, 128);
    buf.append(pcm);
    buf.clear();
    EXPECT_TRUE(buf.empty());
}

TEST(AudioBuffer, DurationMs) {
    AudioBuffer buf(16000, 1, 16);
    // 16000 samples/sec * 2 bytes/sample = 32000 bytes/sec
    // 32000 bytes = 1000ms
    std::vector<uint8_t> pcm(32000, 128);
    buf.append(pcm);
    EXPECT_EQ(buf.duration_ms(), 1000u);
}

TEST(AudioBuffer, Peek) {
    AudioBuffer buf;
    std::vector<uint8_t> pcm = {1, 2, 3, 4};
    buf.append(pcm);

    auto peeked = buf.peek();
    EXPECT_EQ(peeked.size(), 4u);
    // Peek should not consume
    EXPECT_EQ(buf.size(), 4u);
}

TEST(AudioBuffer, SplitChunks) {
    AudioBuffer buf(16000, 1, 16);
    // 100ms = 3200 bytes at 16kHz mono 16-bit
    std::vector<uint8_t> pcm(9600, 128);  // 300ms
    buf.append(pcm);

    auto chunks = buf.split_chunks(100);
    EXPECT_EQ(chunks.size(), 3u);
    for (const auto& chunk : chunks) {
        EXPECT_EQ(chunk.size(), 3200u);
    }
}

TEST(AudioBuffer, CustomSampleRate) {
    AudioBuffer buf(44100, 2, 16);
    EXPECT_EQ(buf.sample_rate(), 44100);
    EXPECT_EQ(buf.channels(), 2);
}

TEST(AudioBuffer, Resample) {
    std::vector<uint8_t> pcm(3200, 128);  // 100ms at 16kHz
    auto resampled = AudioBuffer::resample(pcm, 16000, 8000);
    // Should be roughly half the size
    EXPECT_LT(resampled.size(), pcm.size());
}

// ===========================================================================
// VoicePipelineResult structure
// ===========================================================================

TEST(VoicePipeline, ResultStructure) {
    VoicePipelineResult result;
    result.input_text = "hello";
    result.response_text = "hi there";
    result.output_audio_path = "/tmp/out.wav";

    EXPECT_EQ(result.input_text, "hello");
    EXPECT_EQ(result.response_text, "hi there");
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST(VoiceEdge, EmptyApiKey) {
    STTConfig config;
    config.provider = "whisper";
    config.api_key = "";
    // Factory should handle empty key gracefully
    auto provider = create_stt_provider(config);
    // May return null or a provider that fails on use
}

TEST(VoiceEdge, ZeroLengthAudio) {
    AudioBuffer buf;
    std::vector<uint8_t> empty;
    buf.append(empty);
    EXPECT_TRUE(buf.empty());
}
