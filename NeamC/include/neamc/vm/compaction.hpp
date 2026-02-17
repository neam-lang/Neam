//
// Neam v0.8 Phase 3 — Compaction Engine
// Auto-detects high context usage and summarizes old turns via LLM
//

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "neamc/vm/claw_agent_type.hpp"

namespace neamc::vm
{

class CompactionEngine
{
public:
  // Check if session history exceeds 80% of max context tokens.
  bool needs_compaction(const ObjClawAgent* agent,
                        const SessionState& session,
                        int max_context_tokens = 128000);

  // Compact: take oldest 2/3 of history, summarize via LLM callback,
  // replace with a single summary message. Returns the summary text.
  // llm_summarize: callback that takes a prompt and returns LLM response.
  std::string compact(const ObjClawAgent* agent,
                      SessionState& session,
                      std::function<std::string(const std::string&)> llm_summarize);

private:
  std::string format_turns_for_summary(
      const std::vector<std::pair<std::string, std::string>>& turns);
  static int estimate_tokens(const std::string& text);
};

}  // namespace neamc::vm
