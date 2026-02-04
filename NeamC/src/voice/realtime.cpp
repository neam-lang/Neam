// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Real-time session factory
//

#include "neamc/voice/realtime.hpp"

#include <stdexcept>

#include "realtime_openai.hpp"
#include "realtime_gemini.hpp"
#include "realtime_local.hpp"

namespace neamc::voice
{
std::unique_ptr<RealtimeVoiceSession> create_realtime_session(
    const RealtimeVoiceConfig& config)
{
  const auto& provider = config.provider;
  if (provider == "openai" || provider.empty())
  {
    return make_openai_realtime(config);
  }
  if (provider == "gemini")
  {
    return make_gemini_realtime(config);
  }
  if (provider == "local" || provider == "ollama")
  {
    return make_local_realtime(config);
  }
  throw std::runtime_error("Unknown realtime voice provider: " + provider);
}
}  // namespace neamc::voice
