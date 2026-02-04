// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Voice pipeline orchestrator (STT -> Agent -> TTS)
//

#pragma once

#include <string>

#include "neamc/voice/stt.hpp"
#include "neamc/voice/tts.hpp"

namespace neamc::vm
{
class VirtualMachine;
}

namespace neamc::voice
{
struct VoicePipelineResult
{
  std::string input_text;
  std::string response_text;
  std::string output_audio_path;
};

VoicePipelineResult run_voice_pipeline(vm::VirtualMachine& vm,
                                        const std::string& agent_name,
                                        const std::string& audio_input_path,
                                        const std::string& audio_output_path,
                                        const STTConfig& stt_config,
                                        const TTSConfig& tts_config);
}  // namespace neamc::voice
