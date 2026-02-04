# Neam v0.4.1 — Voice Agents End-to-End Evaluation Checklist

**Version:** 0.4.1
**Feature Area:** Voice Agents (Batch STT/TTS, Real-Time Streaming, Local Pipelines)
**Date Prepared:** 2026-01-29
**Total Test Cases:** 120

---

## How to Use This Checklist

1. Open this file alongside a terminal (Windows CMD/PowerShell or macOS Terminal)
2. Work through each section in order — later sections depend on earlier ones passing
3. For each test case, record:
   - **Pass**: Expected behavior observed
   - **Fail**: Unexpected behavior or error
   - **Skip**: Cannot run (missing prerequisite, not applicable)
   - **Notes**: Any observations, warnings, or deviations
4. Sections are grouped by prerequisite level:
   - **Section 1**: No external dependencies (voice declaration compilation)
   - **Section 2–3**: Requires API keys (OpenAI, Gemini batch STT/TTS)
   - **Section 4**: Requires local services (whisper.cpp, Kokoro, Piper)
   - **Section 5–6**: Requires API keys (OpenAI/Gemini real-time streaming)
   - **Section 7**: Requires local services (local streaming pipeline)
   - **Section 8–10**: Cross-provider, integration, error handling

---

## Tester Information

| Field | Value |
|---|---|
| Tester Name | |
| Test Date | |
| OS / Version | |
| RAM | |
| OpenAI API Key Available? | Yes / No |
| Gemini API Key Available? | Yes / No |
| Ollama Version | |
| Ollama Model(s) Pulled | |
| whisper.cpp Server Running? | Yes / No |
| Kokoro TTS Server Running? | Yes / No |
| Piper TTS Server Running? | Yes / No |

---

## Environment Variables

Ensure the following are set before testing (as applicable):

```bash
# Cloud providers
export OPENAI_API_KEY="sk-..."
export GEMINI_API_KEY="..."

# Local services (defaults shown)
export WHISPER_CPP_URL="http://localhost:8080"
export KOKORO_URL="http://localhost:8880"
export PIPER_URL="http://localhost:5000"
export OLLAMA_URL="http://localhost:11434"
```

**Windows equivalent:**
```cmd
set OPENAI_API_KEY=sk-...
set GEMINI_API_KEY=...
set WHISPER_CPP_URL=http://localhost:8080
set KOKORO_URL=http://localhost:8880
set PIPER_URL=http://localhost:5000
set OLLAMA_URL=http://localhost:11434
```

---

## Section 1: Voice Pipeline Declaration & Compilation

### Prerequisites
- Neam v0.4.1 binaries installed (neamc, neam in PATH)

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-001 | Compile batch voice pipeline (OpenAI STT + TTS) | Create file with `agent` + `voice` block using `stt_provider: "openai"`, `tts_provider: "openai"`, compile with `neamc` | Compiles without error, `.neamb` file created | | |
| VTC-002 | Compile batch voice pipeline (Gemini STT + TTS) | Create file with `voice` block using `stt_provider: "gemini"`, `tts_provider: "gemini"`, compile | Compiles without error | | |
| VTC-003 | Compile batch voice pipeline (local whisper + Kokoro) | Create file with `stt_provider: "whisper-local"`, `tts_provider: "kokoro"`, compile | Compiles without error | | |
| VTC-004 | Compile batch voice pipeline (Piper TTS) | Create file with `tts_provider: "piper"`, compile | Compiles without error | | |
| VTC-005 | Compile realtime_voice (OpenAI) | Create file with `realtime_voice` block using `rt_provider: "openai"`, compile | Compiles without error | | |
| VTC-006 | Compile realtime_voice (Gemini) | Create file with `realtime_voice` block using `rt_provider: "gemini"`, compile | Compiles without error | | |
| VTC-007 | Compile realtime_voice (local) | Create file with `realtime_voice` block using `rt_provider: "local"`, compile | Compiles without error | | |
| VTC-008 | Compile voice pipeline with all optional fields | Include `stt_language`, `stt_format`, `tts_format`, `tts_speed`, `tts_instructions` | Compiles without error, all fields accepted | | |
| VTC-009 | Compile realtime_voice with VAD config | Include `vad_threshold`, `silence_duration_ms`, `rt_vad: "server"` | Compiles without error | | |
| VTC-010 | Compile invalid voice provider | Use `stt_provider: "nonexistent"`, compile | Compilation error with meaningful message about unsupported provider | | |

---

## Section 2: Batch STT (Speech-to-Text)

### Prerequisites
- Section 1 passed
- Audio test file available (WAV or MP3 format)

### 2.1 OpenAI Whisper STT

**Prerequisites:** `OPENAI_API_KEY` set

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-011 | Transcribe audio with whisper-1 | Define voice pipeline with `stt_provider: "openai"`, `stt_model: "whisper-1"`, call `voice_transcribe(pipeline, audio_file)` | Returns non-empty transcription string | | |
| VTC-012 | Transcribe with gpt-4o-transcribe | Change `stt_model: "gpt-4o-transcribe"`, transcribe same file | Returns transcription (potentially lower WER than whisper-1) | | |
| VTC-013 | Transcribe with gpt-4o-mini-transcribe | Change `stt_model: "gpt-4o-mini-transcribe"`, transcribe | Returns transcription | | |
| VTC-014 | Transcribe with language hint | Add `stt_language: "en"`, transcribe | Returns English transcription | | |
| VTC-015 | Transcribe with verbose_json format | Add `stt_format: "verbose_json"`, transcribe | Returns detailed JSON with timestamps | | |
| VTC-016 | Transcribe non-existent file | Call `voice_transcribe(pipeline, "nonexistent.wav")` | Error message about file not found (does not crash) | | |
| VTC-017 | Transcribe with missing API key | Unset `OPENAI_API_KEY`, run | Authentication error (does not crash) | | |

### 2.2 Gemini STT

**Prerequisites:** `GEMINI_API_KEY` set

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-018 | Transcribe audio with Gemini | Define voice pipeline with `stt_provider: "gemini"`, `stt_model: "gemini-2.0-flash"`, transcribe | Returns non-empty transcription | | |
| VTC-019 | Transcribe WAV file | Use `.wav` audio file | Correct MIME type detected (`audio/wav`), transcription returned | | |
| VTC-020 | Transcribe MP3 file | Use `.mp3` audio file | Correct MIME type detected (`audio/mp3`), transcription returned | | |
| VTC-021 | Transcribe OGG file | Use `.ogg` audio file | Correct MIME type (`audio/ogg`), transcription returned | | |
| VTC-022 | Transcribe with timestamps | Enable `audio_timestamp: true` in config | Returns transcription with timing data | | |
| VTC-023 | Transcribe with missing API key | Unset `GEMINI_API_KEY`, run | Error about authentication (does not crash) | | |

### 2.3 Local Whisper.cpp STT

**Prerequisites:** whisper.cpp server running at `http://localhost:8080`

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-024 | Transcribe with local whisper.cpp | Define pipeline with `stt_provider: "whisper-local"`, transcribe | Returns transcription from local server | | |
| VTC-025 | Custom whisper.cpp endpoint | Set `stt_endpoint: "http://localhost:9090"`, transcribe | Connects to custom endpoint | | |
| VTC-026 | Whisper.cpp server unavailable | Stop server, attempt transcription | Connection refused error (does not crash) | | |
| VTC-027 | Transcribe with custom model | Set `stt_model: "base.en"`, transcribe | Model selection passed to server | | |

---

## Section 3: Batch TTS (Text-to-Speech)

### Prerequisites
- Section 1 passed

### 3.1 OpenAI TTS

**Prerequisites:** `OPENAI_API_KEY` set

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-028 | Synthesize with tts-1 model | Define pipeline with `tts_provider: "openai"`, `tts_model: "tts-1"`, `tts_voice: "alloy"`, call `voice_synthesize(pipeline, text, output_file)` | Audio file created, non-zero size | | |
| VTC-029 | Synthesize with tts-1-hd | Change `tts_model: "tts-1-hd"`, `tts_voice: "nova"` | Higher quality audio file created | | |
| VTC-030 | Synthesize with gpt-4o-mini-tts | Set `tts_model: "gpt-4o-mini-tts"`, `tts_instructions: "Speak warmly and clearly"` | Audio file with tone-controlled output | | |
| VTC-031 | Test all 13 voices | Test each: alloy, ash, ballad, cedar, coral, echo, fable, marin, nova, onyx, sage, shimmer, verse | Each voice produces distinct audio output | | |
| VTC-032 | Custom speed (0.5x) | Set `tts_speed: 0.5` | Slower audio output produced | | |
| VTC-033 | Custom speed (2.0x) | Set `tts_speed: 2.0` | Faster audio output produced | | |
| VTC-034 | WAV output format | Set `tts_format: "wav"` | Output file is valid WAV | | |
| VTC-035 | MP3 output format | Set `tts_format: "mp3"` | Output file is valid MP3 | | |
| VTC-036 | OPUS output format | Set `tts_format: "opus"` | Output file is valid OPUS | | |
| VTC-037 | PCM output format | Set `tts_format: "pcm"` | Output file is raw PCM data | | |
| VTC-038 | Synthesize empty text | Call `voice_synthesize(pipeline, "", output)` | Error or empty/minimal audio (does not crash) | | |
| VTC-039 | Missing API key | Unset `OPENAI_API_KEY`, synthesize | Authentication error (does not crash) | | |

### 3.2 Gemini TTS

**Prerequisites:** `GEMINI_API_KEY` set

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-040 | Synthesize with Gemini TTS | Define pipeline with `tts_provider: "gemini"`, `tts_model: "gemini-2.5-flash-preview-tts"`, `tts_voice: "Kore"` | Audio file created (PCM 24kHz converted to WAV) | | |
| VTC-041 | Synthesize with Puck voice | Set `tts_voice: "Puck"` | Audio file created with different voice | | |
| VTC-042 | Synthesize with Zephyr voice | Set `tts_voice: "Zephyr"` | Audio file created | | |
| VTC-043 | Synthesize with pro model | Set `tts_model: "gemini-2.5-pro-preview-tts"` | Higher quality audio output | | |
| VTC-044 | Missing API key | Unset `GEMINI_API_KEY`, synthesize | Error message (does not crash) | | |

### 3.3 Kokoro Local TTS

**Prerequisites:** Kokoro TTS server running at `http://localhost:8880`

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-045 | Synthesize with Kokoro | Define pipeline with `tts_provider: "kokoro"`, `tts_voice: "af_heart"`, synthesize | Audio file created from local server | | |
| VTC-046 | Kokoro with custom voice | Set `tts_voice: "af_bella"` | Different voice audio produced | | |
| VTC-047 | Kokoro with custom speed | Set `tts_speed: 1.5` | Faster speech output | | |
| VTC-048 | Kokoro server unavailable | Stop server, attempt synthesis | Connection refused error (does not crash) | | |

### 3.4 Piper Local TTS

**Prerequisites:** Piper TTS server running at `http://localhost:5000`

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-049 | Synthesize with Piper | Define pipeline with `tts_provider: "piper"`, synthesize | WAV audio file created from local server | | |
| VTC-050 | Custom Piper endpoint | Set `tts_endpoint: "http://localhost:6000"` | Connects to custom endpoint | | |
| VTC-051 | Piper server unavailable | Stop server, attempt synthesis | Connection error (does not crash) | | |

---

## Section 4: Batch Voice Pipeline (End-to-End STT + Agent + TTS)

### Prerequisites
- STT and TTS providers working (Sections 2–3)
- LLM agent available (Ollama or OpenAI)

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-052 | Full pipeline: OpenAI STT → Agent → OpenAI TTS | Define agent + voice pipeline, call `voice_pipeline_run(pipeline, input_audio, output_audio)` | Input audio transcribed, agent processes text, response synthesized to output audio file | | |
| VTC-053 | Full pipeline: Gemini STT → Agent → Gemini TTS | Same flow with Gemini providers | Full pipeline completes successfully | | |
| VTC-054 | Full pipeline: Local whisper → Agent → Kokoro | Same flow with local providers | Full pipeline completes, no cloud API needed | | |
| VTC-055 | Cross-provider: Gemini STT → OpenAI Agent → Kokoro TTS | Mix STT/Agent/TTS from different providers | Pipeline completes with mixed providers | | |
| VTC-056 | Cross-provider: Local whisper → Gemini Agent → Piper TTS | Different mix of providers | Pipeline completes | | |
| VTC-057 | Pipeline with non-existent input audio | Use invalid input file path | Error about missing input (does not crash) | | |
| VTC-058 | Pipeline output directory does not exist | Specify output in non-existent directory | Error about invalid path (does not crash) | | |

---

## Section 5: OpenAI Real-Time Streaming Voice

### Prerequisites
- `OPENAI_API_KEY` set
- Network connectivity for WebSocket

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-059 | Connect to OpenAI Realtime | Define `realtime_voice` with `rt_provider: "openai"`, `rt_model: "gpt-4o-realtime-preview"`, call `realtime_connect(config)` | Session established, session ID returned | | |
| VTC-060 | Send text message | Call `realtime_send_text(session, "Hello, what is 2+2?")` | Text sent successfully | | |
| VTC-061 | Receive text response | Register `realtime_on(session, "response_text", callback)`, send text | Callback fires with agent's text response | | |
| VTC-062 | Receive audio response | Register `realtime_on(session, "response_audio", callback)` | Callback fires with base64 PCM16 audio chunks | | |
| VTC-063 | Send audio stream | Generate/load PCM16 24kHz audio, call `realtime_send_audio(session, audio_b64)` | Audio accepted by server | | |
| VTC-064 | Receive transcript from audio | Register `realtime_on(session, "transcript", callback)`, send audio | User speech transcribed, callback fires | | |
| VTC-065 | Commit audio buffer | Call `voice_commit_audio(session)` after sending audio | Buffer committed, processing starts | | |
| VTC-066 | Clear audio buffer | Call `voice_clear_audio(session)` | Buffer cleared successfully | | |
| VTC-067 | Request explicit response | Call `voice_request_response(session)` | Agent generates response on demand | | |
| VTC-068 | Barge-in / interruption | Send new audio while agent is speaking, call `realtime_interrupt(session)` | Current response cancelled, new input processed | | |
| VTC-069 | Close session | Call `realtime_close(session)` | WebSocket closed cleanly, resources freed | | |
| VTC-070 | Check session status | Call `voice_session_status(session)` during active session | Returns status indicating connected/active | | |
| VTC-071 | Session status after close | Call `voice_session_status(session)` after closing | Returns disconnected/closed status | | |

### 5.1 VAD (Voice Activity Detection) — OpenAI

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-072 | Default VAD settings | Connect with default config (`vad_threshold: 0.5`, `silence_duration_ms: 500`) | VAD functions normally, detects speech start/stop | | |
| VTC-073 | Sensitive VAD | Set `vad_threshold: 0.3`, `silence_duration_ms: 300` | Triggers more easily, shorter pause to end turn | | |
| VTC-074 | Relaxed VAD | Set `vad_threshold: 0.8`, `silence_duration_ms: 1000` | Requires louder speech, longer pause tolerance | | |
| VTC-075 | VAD speech started event | Register `realtime_on(session, "speech_started", callback)` | Callback fires when speech detected | | |
| VTC-076 | VAD speech stopped event | Register `realtime_on(session, "speech_stopped", callback)` | Callback fires when silence detected | | |

### 5.2 Function Calling — OpenAI Realtime

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-077 | Tool call event | Define tools in realtime config, ask agent to use tool | `tool_call` event fires with function name and arguments | | |
| VTC-078 | Return tool result | Call `realtime_tool_result(session, call_id, result)` | Agent incorporates tool result into response | | |

### 5.3 Voice Selection — OpenAI Realtime

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-079 | Voice: alloy | Set `rt_voice: "alloy"`, connect and get audio | Audio uses alloy voice | | |
| VTC-080 | Voice: coral | Set `rt_voice: "coral"` | Audio uses coral voice | | |
| VTC-081 | Voice: echo | Set `rt_voice: "echo"` | Audio uses echo voice | | |
| VTC-082 | Voice: sage | Set `rt_voice: "sage"` | Audio uses sage voice | | |
| VTC-083 | Voice: shimmer | Set `rt_voice: "shimmer"` | Audio uses shimmer voice | | |
| VTC-084 | Voice: breeze | Set `rt_voice: "breeze"` | Audio uses breeze voice | | |

---

## Section 6: Gemini Live Real-Time Streaming Voice

### Prerequisites
- `GEMINI_API_KEY` set
- Network connectivity for WebSocket

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-085 | Connect to Gemini Live | Define `realtime_voice` with `rt_provider: "gemini"`, `rt_model: "gemini-2.0-flash-live-001"`, call `realtime_connect(config)` | Session established via WebSocket, setupComplete received | | |
| VTC-086 | Send text message | Call `realtime_send_text(session, "Explain gravity briefly")` | Text sent as clientContent message | | |
| VTC-087 | Receive text response | Register response_text callback, send text | Callback fires with agent text | | |
| VTC-088 | Receive audio response | Register response_audio callback | Callback fires with PCM16 24kHz audio | | |
| VTC-089 | Send audio stream (16kHz PCM) | Send PCM16 16kHz audio chunks via `realtime_send_audio` | Audio accepted as realtimeInput | | |
| VTC-090 | Audio format: 16kHz input → 24kHz output | Send 16kHz input, receive output | Input at 16kHz, output at 24kHz (different sample rates handled) | | |
| VTC-091 | Barge-in support | Send new audio while agent is speaking | Agent interrupted, new input processed | | |
| VTC-092 | Close session | Call `realtime_close(session)` | WebSocket closed, resources freed | | |

### 6.1 Session Resumption — Gemini Live

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-093 | Receive session resumption token | Connect and interact, check for `sessionResumptionUpdate` | Handle token received during session | | |
| VTC-094 | Resume session with token | Close session, reconnect with saved handle token | Context restored, conversation continues | | |
| VTC-095 | Session expiry detection | Let session approach 10-minute limit | `check_session_expiry()` triggers, auto-reconnect attempted | | |

### 6.2 Activity Detection — Gemini Live

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-096 | Auto activity detection (default) | Connect with default config | Speech start/stop detected automatically | | |
| VTC-097 | High sensitivity | Set `startOfSpeechSensitivity: "HIGH"`, `endOfSpeechSensitivity: "HIGH"` | More responsive detection | | |
| VTC-098 | Custom silence duration | Set `silenceDurationMs: 800` | Longer pause before turn ends | | |

### 6.3 Voice Selection — Gemini Live

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-099 | Voice: Puck | Set voice to Puck, get audio response | Audio uses Puck voice | | |
| VTC-100 | Voice: Kore | Set voice to Kore | Audio uses Kore voice | | |

### 6.4 Tool Calling — Gemini Live

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-101 | Tool call during stream | Define tools, ask agent to use tool | `toolCall` message received with function details | | |
| VTC-102 | Return tool result | Send `toolResponse` with result | Agent incorporates result, continues response | | |
| VTC-103 | Tool cancellation on barge-in | Interrupt during tool execution | `toolCallCancellation` received | | |

---

## Section 7: Local Real-Time Streaming Pipeline

### Prerequisites
- whisper.cpp server at `http://localhost:8080`
- Ollama running with `llama3` model
- Kokoro TTS server at `http://localhost:8880`

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-104 | Connect to local streaming pipeline | Define `realtime_voice` with `rt_provider: "local"`, call `realtime_connect` | Session created with local services | | |
| VTC-105 | Send text, receive streaming response | Call `realtime_send_text(session, "Hello")` | LLM response streams token-by-token, TTS chunks follow | | |
| VTC-106 | Send audio, receive full pipeline response | Send 16kHz PCM audio | Audio → whisper.cpp STT → Ollama → Kokoro TTS → audio chunks | | |
| VTC-107 | Sentence-level TTS chunking | Send long query, observe output timing | TTS starts after first complete sentence (not after full LLM response) | | |
| VTC-108 | Multi-turn conversation | Send multiple queries in sequence | Context maintained across turns | | |
| VTC-109 | Model variant: mistral | Configure `rt_model: "mistral"` | Uses mistral model instead of llama3 | | |
| VTC-110 | Service unavailable: whisper.cpp | Stop whisper.cpp, send audio | Connection error for STT (does not crash) | | |
| VTC-111 | Service unavailable: Ollama | Stop Ollama, send text | Connection error for LLM (does not crash) | | |
| VTC-112 | Service unavailable: Kokoro | Stop Kokoro, send text | Connection error for TTS (does not crash, text response may still work) | | |

---

## Section 8: Audio Utilities

### Prerequisites
- Section 1 passed

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-113 | Resample 16kHz to 24kHz | Call `voice_audio_resample(audio_b64, 16000, 24000)` | Returns valid base64 PCM at 24kHz | | |
| VTC-114 | Resample 24kHz to 16kHz | Call `voice_audio_resample(audio_b64, 24000, 16000)` | Returns valid base64 PCM at 16kHz | | |
| VTC-115 | Engine-level transcription | Call `voice_engine_transcribe(engine, audio_path, options)` | Direct transcription bypassing pipeline | | |
| VTC-116 | Engine-level synthesis | Call `voice_engine_synthesize(engine, text, voice, options)` | Direct synthesis bypassing pipeline | | |

---

## Section 9: Error Handling & Edge Cases

### Prerequisites
- Sections 1–4 partially passed

| TC# | Test Case | Steps | Expected Result | Status | Notes |
|-----|-----------|-------|-----------------|--------|-------|
| VTC-117 | Invalid pipeline name in voice_transcribe | Call `voice_transcribe("nonexistent", audio)` | Error about unknown pipeline (does not crash) | | |
| VTC-118 | Invalid session ID in realtime_send_text | Call `realtime_send_text("invalid_session", text)` | Error about invalid session (does not crash) | | |
| VTC-119 | Double close session | Call `realtime_close(session)` twice | Second close is no-op or graceful error | | |
| VTC-120 | Send audio after session close | Close session, then call `realtime_send_audio` | Error about closed session (does not crash) | | |

---

## Results Summary

Fill in after completing all tests:

| Section | Total | Pass | Fail | Skip | Pass Rate |
|---------|-------|------|------|------|-----------|
| 1. Voice Pipeline Declaration & Compilation | 10 | | | | |
| 2. Batch STT (Speech-to-Text) | 17 | | | | |
| 3. Batch TTS (Text-to-Speech) | 24 | | | | |
| 4. Batch Voice Pipeline (End-to-End) | 7 | | | | |
| 5. OpenAI Real-Time Streaming | 26 | | | | |
| 6. Gemini Live Real-Time Streaming | 19 | | | | |
| 7. Local Real-Time Streaming Pipeline | 9 | | | | |
| 8. Audio Utilities | 4 | | | | |
| 9. Error Handling & Edge Cases | 4 | | | | |
| **TOTAL** | **120** | | | | |

---

## Provider Coverage Matrix

| Feature | OpenAI | Gemini | Local (whisper.cpp) | Kokoro | Piper |
|---------|--------|--------|---------------------|--------|-------|
| **Batch STT** | whisper-1, gpt-4o-transcribe, gpt-4o-mini-transcribe | gemini-2.0-flash | whisper.cpp server | N/A | N/A |
| **Batch TTS** | tts-1, tts-1-hd, gpt-4o-mini-tts | gemini-2.5-flash/pro-preview-tts | N/A | kokoro | piper |
| **Real-Time Streaming** | gpt-4o-realtime, gpt-4o-realtime-mini | gemini-2.0-flash-live-001 | Ollama + whisper.cpp + Kokoro | N/A | N/A |
| **VAD** | Server-side (threshold 0.0–1.0) | Auto activity detection (HIGH/MED/LOW) | Manual (client-side chunks) | N/A | N/A |
| **Session Resumption** | Unlimited duration | Handle token (~10 min sessions) | No persistence | N/A | N/A |
| **Function Calling** | Yes (during stream) | Yes (during stream) | No | N/A | N/A |
| **Barge-In** | Yes (response.cancel) | Yes (new audio stream) | No | N/A | N/A |

---

## Audio Format Reference

| Context | Format | Sample Rate | Bit Depth | Channels |
|---------|--------|-------------|-----------|----------|
| OpenAI Realtime Input | PCM16 | 24 kHz | 16-bit | Mono |
| OpenAI Realtime Output | PCM16 | 24 kHz | 16-bit | Mono |
| Gemini Live Input | PCM16 LE | 16 kHz | 16-bit | Mono |
| Gemini Live Output | PCM16 LE | 24 kHz | 16-bit | Mono |
| Local Pipeline Input | PCM16 | 16 kHz | 16-bit | Mono |
| Local Pipeline Output | PCM16 | 16 kHz | 16-bit | Mono |
| Batch STT Input | WAV/MP3/OGG/FLAC | Varies | Varies | Varies |
| Batch TTS Output | WAV/MP3/OPUS/PCM | Varies | Varies | Mono |

---

## Severity Classification

| Severity | Definition | Example |
|----------|-----------|---------|
| **S1 — Blocker** | Cannot proceed, core voice functionality broken | WebSocket connection always fails, crash on voice_transcribe |
| **S2 — Critical** | Major voice feature broken, no workaround | Real-time streaming never receives audio, all TTS providers fail |
| **S3 — Major** | Feature broken but workaround exists | One specific voice/provider fails, others work |
| **S4 — Minor** | Cosmetic or non-functional issue | Slight audio artifacts, minor latency spike |
| **S5 — Enhancement** | Not a bug, improvement opportunity | Better error messages, additional voice options |

---

## Bug Report Template

```
TC#:        [e.g., VTC-059]
Severity:   [S1-S5]
Summary:    [One-line description]

Steps to Reproduce:
1. [Step 1]
2. [Step 2]
3. [Step 3]

Expected Result:
[What should happen]

Actual Result:
[What actually happened]

Error Output:
[Paste any error messages or stack traces]

Audio Details:
- Input Format: [e.g., PCM16 24kHz mono]
- Output Format: [e.g., WAV 16-bit]
- File Size: [if applicable]

Environment:
- OS: [e.g., Windows 11 23H2 / macOS 15.3]
- Terminal: [CMD / PowerShell / Terminal.app]
- Provider: [OpenAI / Gemini / Local]
- API Key Valid: [Yes / No]
- Local Services Running: [whisper.cpp / Kokoro / Piper / Ollama]
```

---

## Sign-Off

| Role | Name | Date | Signature |
|------|------|------|-----------|
| Tester | | | |
| Reviewer | | | |

---

**Neam v0.4.1** — Voice Agents Evaluation Checklist prepared by automated build pipeline.
