// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Gemini STT provider header
//

#pragma once

#include <memory>

#include "neamc/voice/stt.hpp"

namespace neamc::voice
{
std::unique_ptr<STTProvider> make_gemini_stt(const STTConfig& config);
}  // namespace neamc::voice
