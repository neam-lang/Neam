// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Piper TTS provider header
//

#pragma once

#include <memory>

#include "neamc/voice/tts.hpp"

namespace neamc::voice
{
std::unique_ptr<TTSProvider> make_piper_tts(const TTSConfig& config);
}  // namespace neamc::voice
