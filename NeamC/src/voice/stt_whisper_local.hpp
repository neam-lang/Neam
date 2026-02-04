// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Whisper.cpp local STT provider header
//

#pragma once

#include <memory>

#include "neamc/voice/stt.hpp"

namespace neamc::voice
{
std::unique_ptr<STTProvider> make_whisper_cpp_stt(const STTConfig& config);
}  // namespace neamc::voice
