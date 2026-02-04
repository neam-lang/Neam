// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Gemini Live provider header
//

#pragma once

#include <memory>

#include "neamc/voice/realtime.hpp"

namespace neamc::voice
{
std::unique_ptr<RealtimeVoiceSession> make_gemini_realtime(
    const RealtimeVoiceConfig& config);
}  // namespace neamc::voice
