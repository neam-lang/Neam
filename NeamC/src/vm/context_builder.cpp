//
// Neam v0.8 Phase 3 — Context Builder implementation
//

#include "neamc/vm/context_builder.hpp"

#include <algorithm>

namespace neamc::vm
{

int ContextBuilder::estimate_tokens(const std::string& text)
{
  return std::max(1, static_cast<int>(text.size() / 4));
}

AssembledContext ContextBuilder::build(const ObjClawAgent* agent,
                                       const SessionState& session,
                                       int max_context_tokens)
{
  AssembledContext ctx;

  // Build system prompt from agent config
  if (agent->system)
  {
    ctx.system_prompt = std::string(agent->system->chars, agent->system->length);
  }

  int system_tokens = estimate_tokens(ctx.system_prompt);
  int available_tokens = max_context_tokens - system_tokens;
  if (available_tokens < 0) available_tokens = 0;

  int max_turns = agent->max_history_turns;
  if (max_turns <= 0) max_turns = 100;  // default

  // Fill from newest to oldest until token budget exhausted or max_turns reached
  int accumulated_tokens = 0;
  int turns_added = 0;
  std::vector<std::pair<std::string, std::string>> selected;

  for (int i = static_cast<int>(session.history.size()) - 1; i >= 0; --i)
  {
    const auto& [role, content] = session.history[i];
    int msg_tokens = estimate_tokens(role) + estimate_tokens(content);

    if (accumulated_tokens + msg_tokens > available_tokens)
    {
      ctx.was_truncated = true;
      break;
    }
    if (turns_added >= max_turns)
    {
      ctx.was_truncated = true;
      break;
    }

    selected.emplace_back(role, content);
    accumulated_tokens += msg_tokens;
    ++turns_added;
  }

  // Reverse to chronological order
  std::reverse(selected.begin(), selected.end());
  ctx.messages = std::move(selected);
  ctx.token_estimate = system_tokens + accumulated_tokens;

  return ctx;
}

}  // namespace neamc::vm
