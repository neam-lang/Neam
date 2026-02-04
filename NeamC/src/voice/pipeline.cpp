// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Voice pipeline implementation (STT -> Agent -> TTS)
//

#include "neamc/voice/pipeline.hpp"

#include <stdexcept>

#include "neamc/vm/vm.hpp"

namespace neamc::voice
{
VoicePipelineResult run_voice_pipeline(vm::VirtualMachine& vm,
                                        const std::string& agent_name,
                                        const std::string& audio_input_path,
                                        const std::string& audio_output_path,
                                        const STTConfig& stt_config,
                                        const TTSConfig& tts_config)
{
  // Step 1: Speech-to-Text
  auto stt = create_stt_provider(stt_config);
  std::string transcript = stt->transcribe(audio_input_path);

  // Step 2: Agent processing
  std::string response = vm.call_agent_internal(agent_name, transcript);

  // Step 3: Text-to-Speech
  auto tts = create_tts_provider(tts_config);
  tts->synthesize(response, audio_output_path);

  VoicePipelineResult result;
  result.input_text = std::move(transcript);
  result.response_text = std::move(response);
  result.output_audio_path = audio_output_path;
  return result;
}
}  // namespace neamc::voice
