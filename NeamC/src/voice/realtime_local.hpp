// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Local streaming pipeline header
//

#pragma once

#include <memory>

#include "neamc/voice/realtime.hpp"

namespace neamc::voice
{
std::unique_ptr<RealtimeVoiceSession> make_local_realtime(
    const RealtimeVoiceConfig& config);
}  // namespace neamc::voice
