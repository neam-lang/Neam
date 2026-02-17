//
// Neam v0.8 Phase 3 — Context Builder
// Token-aware message assembly for claw agents
//

#pragma once

#include <string>
#include <vector>

#include "neamc/vm/claw_agent_type.hpp"

namespace neamc::vm
{

struct AssembledContext
{
  std::string system_prompt;
  std::vector<std::pair<std::string, std::string>> messages;  // role, content
  int token_estimate{0};
  bool was_truncated{false};
};

class ContextBuilder
{
public:
  // Build assembled context from session history, respecting token budget
  // and max_history_turns from the agent config.
  AssembledContext build(const ObjClawAgent* agent,
                         const SessionState& session,
                         int max_context_tokens = 128000);

  // Estimate tokens for a text string (chars/4 heuristic).
  static int estimate_tokens(const std::string& text);
};

}  // namespace neamc::vm
