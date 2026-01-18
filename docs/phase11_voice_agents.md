# Phase 11 Voice Agents - Implementation Notes

This document tracks the Phase 11 voice agent surface added to the Neam
standard library. It mirrors the v1.0 specification for the voice agent stack
and provides a lightweight implementation plan for stdlib modules.

## Scope

The following stdlib namespaces now expose the Phase 11 scaffolding:

- `std.ai` (caps, events, traits, message helpers, realtime session helpers)
- `std.audio` (types, stream, utils, buffers, codecs, meter, traits)
- `std.speech` (STT/TTS contracts, events, caching, provider adapters)
- `std.vad` (detector + endpointing policy primitives)
- `std.voice` (agent/session/turn/playback/policy/transcript/consent/audit/taint/metrics/tracing)
- `std.realtime` (provider sessions, events, backpressure)
- `std.media` + `std.trust` (shared media utilities and taint tracking)

## Design Notes

- Interfaces are expressed as structured maps with helper constructors.
- Provider integrations are delegated to runtime hooks (e.g. `speech_stt_provider`).
- Capabilities validation is centralized in `std.ai.caps` to keep modality
  checks deterministic and provider-agnostic.

## Runtime Hooks

The following runtime builtins are required by the Phase 11 stdlib modules:

| Builtin | Module | Description |
| --- | --- | --- |
| `vad_create(sensitivity)` | `std.vad.detector` | Create a VAD instance. |
| `vad_detect_frame(vad, frame)` | `std.vad.detector` | Detect speech in a single frame. |
| `vad_process_stream(vad, stream)` | `std.vad.detector` | Process a streaming audio source. |
| `vad_reset_state(vad)` | `std.vad.detector` | Reset a VAD instance. |
| `speech_stt_provider(name)` | `std.speech.stt` | Resolve an STT provider by name. |
| `speech_stt_providers()` | `std.speech.stt` | Enumerate available STT providers. |
| `speech_tts_provider(name)` | `std.speech.tts` | Resolve a TTS provider by name. |
| `speech_tts_providers()` | `std.speech.tts` | Enumerate available TTS providers. |
| `voice_agent_create(builder)` | `std.voice.agent` | Instantiate a voice agent from a builder. |
| `voice_agent_listen(agent, input, output)` | `std.voice.agent` | Start the listen/speak loop. |
| `voice_agent_process_turn_impl(agent, stream)` | `std.voice.agent` | Process a single turn. |
| `voice_session_start(agent, input, output)` | `std.voice.agent` | Start a new voice session. |
| `voice_process_turn(agent, stream)` | `std.voice.agent` | Process one voice turn. |
| `voice_assert_*` | `std.voice.testing.assertions` | Voice test assertion helpers. |

## Provider Interfaces

Providers are expected to implement the following adapter shapes:

**STT Provider**

```neam
{
  transcribe: fn(stream: AudioStream) -> Stream<SttEvent>,
  supported_languages: fn() -> Vec<Language>,
  configure: fn(config: SttConfig) -> void
}
```

**TTS Provider**

```neam
{
  synthesize: fn(text: String) -> Stream<TtsEvent>,
  synthesize_ssml: fn(ssml: String) -> Stream<TtsEvent>,
  voices: fn() -> Vec<Voice>,
  configure: fn(config: TtsConfig) -> void
}
```

**Realtime Provider**

```neam
{
  connect: fn(config: RealtimeConfig) -> Session,
  send_audio: fn(frame: AudioFrame) -> void,
  send_text: fn(text: String) -> void,
  send_tool_result: fn(call_id: String, result: Value) -> void,
  events: fn() -> Stream<ModelEvent>,
  interrupt: fn() -> void,
  close: fn() -> void
}
```

## Provider Package Structure

Recommended package layout:

```
@neam/
├── provider-openai/
│   ├── package.json
│   ├── src/
│   │   ├── index.neam
│   │   ├── stt.neam
│   │   ├── tts.neam
│   │   └── realtime.neam
│   └── tests/
├── provider-anthropic/
│   ├── package.json
│   ├── src/
│   │   ├── index.neam
│   │   └── claude.neam
│   └── tests/
├── provider-deepgram/
│   ├── package.json
│   ├── src/
│   │   ├── index.neam
│   │   └── stt.neam
│   └── tests/
├── provider-elevenlabs/
│   ├── package.json
│   ├── src/
│   │   ├── index.neam
│   │   └── tts.neam
│   └── tests/
└── provider-azure/
    ├── package.json
    ├── src/
    │   ├── index.neam
    │   ├── stt.neam
    │   └── tts.neam
    └── tests/
```

Provider template example (`@neam/provider-openai/src/stt.neam`):

```neam
module neam.provider.openai.stt;

use std.speech.stt;
use std.audio.types;

pub fun create(api_key) {
  return {
    api_key: api_key,
    model: "whisper-1",

    transcribe: fn(stream) {
      // Implementation
    },

    supported_languages: fn() {
      return ["en", "es", "fr", "de", "it", "pt", "nl", "pl", "ru", "zh", "ja", "ko"];
    },

    configure: fn(config) {
      // Apply configuration
    }
  };
}
```

## Next Steps

- Wire runtime implementations for the new stdlib hooks.
- Add provider packages for STT/TTS and realtime APIs.
- Expand `std.voice.testing` with deterministic fixtures in test suites.
