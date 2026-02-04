// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - OpenAI Realtime provider header
//

#pragma once

#include <memory>

#include "neamc/voice/realtime.hpp"

namespace neamc::voice
{
std::unique_ptr<RealtimeVoiceSession> make_openai_realtime(
    const RealtimeVoiceConfig& config);
}  // namespace neamc::voice
