//
// Neam v0.8 Phase 3 — Compaction Engine implementation
//

#include "neamc/vm/compaction.hpp"

#include <algorithm>
#include <sstream>

namespace neamc::vm
{

int CompactionEngine::estimate_tokens(const std::string& text)
{
  return std::max(1, static_cast<int>(text.size() / 4));
}

bool CompactionEngine::needs_compaction(const ObjClawAgent* agent,
                                        const SessionState& session,
                                        int max_context_tokens)
{
  (void)agent;
  int total_tokens = 0;
  for (const auto& [role, content] : session.history)
  {
    total_tokens += estimate_tokens(role) + estimate_tokens(content);
  }
  double threshold = max_context_tokens * 0.8;
  return total_tokens > static_cast<int>(threshold);
}

std::string CompactionEngine::format_turns_for_summary(
    const std::vector<std::pair<std::string, std::string>>& turns)
{
  std::ostringstream oss;
  for (const auto& [role, content] : turns)
  {
    oss << "[" << role << "]: " << content << "\n";
  }
  return oss.str();
}

std::string CompactionEngine::compact(
    const ObjClawAgent* agent,
    SessionState& session,
    std::function<std::string(const std::string&)> llm_summarize)
{
  if (session.history.size() < 3)
  {
    return {};  // Not enough to compact
  }

  session.is_compacting = true;

  // Split at 2/3 point: oldest 2/3 get summarized, newest 1/3 kept
  int split = static_cast<int>(session.history.size()) * 2 / 3;
  if (split < 1) split = 1;

  std::vector<std::pair<std::string, std::string>> old_turns(
      session.history.begin(), session.history.begin() + split);
  std::vector<std::pair<std::string, std::string>> recent_turns(
      session.history.begin() + split, session.history.end());

  // Format old turns and ask LLM to summarize
  std::string formatted = format_turns_for_summary(old_turns);
  std::string prompt =
      "Summarize the following conversation concisely, preserving key facts, "
      "decisions, and context needed for continuation:\n\n" + formatted;

  std::string summary;
  try
  {
    summary = llm_summarize(prompt);
  }
  catch (...)
  {
    session.is_compacting = false;
    return {};  // Failed to summarize — leave history intact
  }

  // Replace history: summary + recent turns
  session.history.clear();
  session.history.emplace_back("system", "[Session Summary] " + summary);
  session.history.insert(session.history.end(), recent_turns.begin(), recent_turns.end());

  session.is_compacting = false;
  return summary;
}

}  // namespace neamc::vm
