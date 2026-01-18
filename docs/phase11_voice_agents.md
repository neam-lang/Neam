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

## Next Steps

- Wire runtime implementations for the new stdlib hooks.
- Add provider packages for STT/TTS and realtime APIs.
- Expand `std.voice.testing` with deterministic fixtures in test suites.
