//
// v0.8: Context builder & compaction engine — assembles LLM context from session state
//

#pragma once

#include <functional>
#include <string>
#include <vector>

#include "neamc/vm/session_manager.hpp"

namespace neamc::vm
{

struct ObjClawAgent;

struct AssembledContext
{
  std::string system_prompt;
  std::vector<std::pair<std::string, std::string>> messages;
};

class ContextBuilder
{
public:
  AssembledContext build(ObjClawAgent* agent, const Session& session)
  {
    AssembledContext ctx;
    if (agent)
    {
      // Placeholder: copy session history as messages
      ctx.messages = session.history;
    }
    return ctx;
  }
};

class CompactionEngine
{
public:
  bool needs_compaction(ObjClawAgent* /*agent*/, const Session& session)
  {
    // Placeholder: never triggers compaction
    (void)session;
    return false;
  }

  template <typename SummarizeFn>
  std::string compact(ObjClawAgent* /*agent*/, const Session& /*session*/, SummarizeFn /*fn*/)
  {
    return {};
  }
};

}  // namespace neamc::vm
